// The whole system for one block at a time.
#include <algorithm>
#include <stdexcept>

#include "kordia/core.h"

namespace kordia {

static std::optional<int> key_of(const RunConfig& c) {
    return c.tom ? std::optional<int>(note_pc(*c.tom)) : std::nullopt;
}

static std::unique_ptr<Grid> make_tracker(const RunConfig& c, double lookahead, double frame) {
    if (c.modo == "metronomo") {
        if (!c.bpm) throw std::invalid_argument("modo metronomo exige bpm");
        // with an audible click the musician follows the click: the grid starts right away, not at the first strum
        return std::make_unique<FixedGrid>(*c.bpm, c.clique || c.contagem ? std::optional<double>(0.5 + lookahead) : std::nullopt);
    }
    return std::make_unique<TempoTracker>(frame);
}

static const RunConfig& checked(const RunConfig& c) {
    if (c.modo == "decorado" && !c.progressao) throw std::invalid_argument("modo decorado exige progressao");
    return c;
}

Processor::Processor(const RunConfig& cfg, double lookahead, double sr, double out_sr)
    : click(cfg.clique),
      sr_(sr),
      lookahead_(lookahead),
      buf_(P::CHROMA_WIN, 0.0),
      spec_onset_(P::ONSET_WIN),
      spec_chroma_(P::CHROMA_WIN),
      tracker_(make_tracker(checked(cfg), lookahead, P::HOP / sr)),
      memory_(cfg.progressao),   // a progression also works together with a fixed BPM
      engine_(*tracker_, memory_, key_of(cfg), cfg.modo == "metronomo", lookahead),
      onsets_(sr),
      chords_(sr, key_of(cfg)),
      acc_(key_of(cfg)),
      synth_(out_sr > 0 ? out_sr : sr) {
    engine_.set_frame(P::HOP / sr);
    const bool anchored = cfg.clique || cfg.contagem;
    if (cfg.modo != "metronomo" && anchored && cfg.bpm_inicial)
        if (auto* tt = dynamic_cast<TempoTracker*>(tracker_.get())) tt->seed(*cfg.bpm_inicial, 0.5 + lookahead);
    if (cfg.contagem && tracker_->has_period()) count_end_ = 0.5 + lookahead + P::BEATS_PER_BAR * tracker_->period;
    engine_.hold = cfg.manter_padrao;
    engine_.learn = cfg.aprender_na_tonica;
    engine_.grid = anchored && tracker_->has_period() && !cfg.progressao;
    evs_.reserve(64);
}

void Processor::analyze(const double* x) {
    double ms = 0;
    for (int i = 0; i < P::HOP; ++i) ms += x[i] * x[i];
    double db = 10 * std::log10(ms / P::HOP + 1e-12);   // raw level: silence and dynamics are absolute
    if (db > P::SILENCE_DB) agc_level_ += (db > agc_level_ ? P::AGC_UP : P::AGC_DOWN) * (db - agc_level_);
    agc_gain_db = std::clamp(P::AGC_TARGET_DB - agc_level_, P::AGC_MIN_DB, P::AGC_MAX_DB);
    const double g = std::pow(10.0, agc_gain_db / 20);
    std::copy(buf_.begin() + P::HOP, buf_.end(), buf_.begin());
    for (int i = 0; i < P::HOP; ++i) buf_[P::CHROMA_WIN - P::HOP + i] = x[i] * g;
    s_ += P::HOP;
    double t = s_ / sr_;

    auto onset = onsets_.process(spec_onset_(buf_.data() + P::CHROMA_WIN - P::ONSET_WIN), t - P::ONSET_WIN / 2.0 / sr_, db);
    Grid& tracker = *tracker_;
    tracker.update(t, onsets_.flux, onset);
    if (onset) {
        onset_list.push_back({onset->t, onset->db});
        dyn_.strum(onset->db);
        last_onset_t_ = onset->t;
        last_onset_strong_ = tracker.has_period() && tracker.is_strong(onset->t);
    }
    double p_change = t - last_onset_t_ < P::HMM_ONSET_BOOST_TIME
                          ? (last_onset_strong_ ? P::HMM_P_CHANGE_STRONG : P::HMM_P_CHANGE_ONSET)
                          : P::HMM_P_CHANGE;
    auto [idx, conf] = chords_.process(spec_chroma_(buf_.data()), db < P::SILENCE_DB, p_change);

    auto closed = engine_.grid ? close_slot(t, db) : std::nullopt;
    bool counting = tracker.has_period() && t < count_end_ - P::COUNT_EARLY_BEATS * tracker.period;
    std::optional<Output> decision;
    if (!counting) decision = engine_.step(t, onset, idx, conf, chords_.evidence, closed);
    int vel = dyn_.velocity();
    evs_.clear();
    if (decision) {
        acc_.apply(*decision, vel, evs_);
        decisions.push_back({std::nearbyint(decision->t * 1e3) / 1e3, engine_.state, decision->chord, decision->mode,
                             engine_.reason});
        ++n_decisions;
    }
    double frame_dur = P::HOP / sr_;
    if ((engine_.state == State::Ouvindo || engine_.state == State::Travado) && tracker.confidence() >= 0.5) {
        // bass re-attack on the predicted beat that falls in the next rendered block
        long long bi = (long long)std::ceil(tracker.beat_phase(t + lookahead_));
        double tb = tracker.time_of_beat(bi);
        if (tb < t + lookahead_ + frame_dur) acc_.beat(tb, tracker.bar_pos(bi) % 2 == 0, tracker.period, vel, evs_);
    }
    if ((click || t + lookahead_ + P::HOP / sr_ < count_end_) && tracker.has_period()) {
        auto* tt = dynamic_cast<TempoTracker*>(tracker_.get());
        bool guide = !tt || tracker.frozen() || tt->seeded();   // the click is the guide: always on
        bool active = engine_.state == State::Ouvindo || engine_.state == State::Travado || engine_.state == State::Fallback;
        if (guide || (active && tracker.confidence() >= 0.5)) {
            long long bi = (long long)std::ceil(tracker.beat_phase(t + lookahead_));
            double tb = tracker.time_of_beat(bi);
            if (tb < t + lookahead_ + frame_dur && tb > last_click_t_ + 0.5 * tracker.period) {
                last_click_t_ = tb;
                acc_.click(tb, tracker.bar_pos(bi) == 0, click_vel, evs_);
            }
        }
    }
    double expr = dyn_.expression(t, engine_.last_onset, tracker.period);
    if (engine_.hold && engine_.state == State::Travado) expr = 1.0;   // a held pattern does not fade between strums
    acc_.expression(t, expr, evs_);
    for (const auto& e : evs_) synth_.handle(e);

    auto bpm = tracker.bpm();
    frame = {t, db, onsets_.flux, onset.has_value(), bpm ? *bpm : std::nan(""), tracker.confidence(), idx, conf,
             engine_.state, engine_.level, expr, vel};
}

std::optional<Engine::Slot> Processor::close_slot(double t, double db) {
    double now = t - P::SLOT_LAG;
    long long index = (long long)std::floor(tracker_->beat_phase(now) / P::SLOT_BEATS);
    std::optional<Engine::Slot> closed;
    if (!slot_ || index != *slot_) {
        if (slot_) closed = Engine::Slot{*slot_, slot_sum_};
        slot_ = index;
        slot_sum_.fill(0.0);
    }
    if (now - tracker_->time_of_beat(index * P::SLOT_BEATS) >= P::SLOT_SKIP && db >= P::SILENCE_DB)
        for (int k = 0; k < 12; ++k) slot_sum_[k] += chords_.last_chroma[k];
    return closed;
}

int Processor::count_left() const {
    if (!tracker_->has_period()) return 0;
    return (int)std::clamp(std::ceil((count_end_ - s_ / sr_) / tracker_->period), 0.0, (double)P::BEATS_PER_BAR);
}

void Processor::freeze_tempo(bool on) {
    if (auto* tt = dynamic_cast<TempoTracker*>(tracker_.get())) tt->set_frozen(on);
}

void Processor::set_bpm(double bpm) {
    if (auto* fg = dynamic_cast<FixedGrid*>(tracker_.get())) fg->set_bpm(bpm);
    else if (auto* tt = dynamic_cast<TempoTracker*>(tracker_.get())) tt->set_bpm(bpm);
}

std::optional<int> Processor::next_chord() const {
    auto p = memory_.locked ? memory_.predict() : std::nullopt;
    return p ? std::optional<int>(p->first) : std::nullopt;
}

std::optional<int> Processor::beat_in_bar() const {
    if (!tracker_->has_period()) return std::nullopt;
    return (int)tracker_->bar_pos((long long)std::floor(tracker_->beat_phase(s_ / sr_))) + 1;
}

}  // namespace kordia
