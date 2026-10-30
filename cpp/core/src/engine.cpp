// Decision engine: state machine, confidence ladder, stop rules, prediction scheduling (Python: engine.py).
#include "kordia/core.h"

namespace kordia {

Engine::Engine(Grid& tracker, ProgressionMemory& memory, std::optional<int> key, bool metro, double lookahead)
    : key_pc(key), metronome(metro), tracker_(tracker), memory_(memory), lookahead_(lookahead), frame_(P::HOP / 22050.0) {
    forget();
}

void Engine::forget() {
    state = State::Ocioso;
    level = Level::Perdido;
    last_onset.reset();
    iois_.clear();
    pending_.reset();
    doubt_since_.reset();
    confirm_at_ = 0.0;
    expected_.reset();
    prev_chord_.reset();
    changed_at_ = 0.0;
    misses_ = 0;
    contra_.reset();
    contra_since_ = 0.0;
    last_change_t_ = -INF;
    heard_.clear();
    last_heard_ = -1;
    disagree_since_.reset();
    root5_tc_.reset();
    reason.clear();
    stop_at_ = 0.0;
    memory_.reset();
    tracker_.reset();
}

std::optional<Output> Engine::set(int chord, Mode mode, double t, const char* why) {
    if (cur.first == chord && cur.second == mode) return std::nullopt;
    cur = {chord, mode};
    if (why) reason = why;
    return Output{chord, mode, t};
}

double Engine::stop_gap() const {
    if (held() && tracker_.has_period()) return P::HOLD_STOP_BARS * bar();   // a late entry is not the end
    double ioi = 0.0;
    for (double v : iois_) ioi = std::max(ioi, v);
    ioi = std::min(ioi, bar());
    double base = tracker_.has_period() ? P::STOP_BEATS * tracker_.period : P::STOP_DEFAULT_SECONDS;
    return std::max(base, P::STOP_IOI_FACTOR * ioi);
}

double Engine::change_time(double t) const {
    return (last_onset && t - *last_onset < 0.4) ? *last_onset : t;
}

std::optional<Output> Engine::step(double t, const std::optional<Onset>& onset, int idx, double conf,
                                   const std::array<double, 25>& evidence) {
    if (idx == N_IDX || conf < P::CONF_LOW)
        level = conf < P::CONF_LOW ? Level::Perdido : Level::Duvida;
    else
        level = conf >= P::CONF_HIGH ? Level::Confiante : Level::Duvida;
    if (onset) {
        if (last_onset) {
            iois_.push_back(onset->t - *last_onset);
            if (iois_.size() > 8) iois_.pop_front();
        }
        last_onset = onset->t;
    }

    if (state == State::Ocioso) {
        if (level == Level::Confiante && last_onset && t - *last_onset < 1.0) {
            state = State::Ouvindo;
            return reactive_change(t, idx);
        }
        return std::nullopt;
    }

    double gap = t - *last_onset;
    if (state != State::Encerrando && gap > stop_gap()) {
        state = State::Encerrando;
        pending_.reset();
        double end = tracker_.has_period() ? tracker_.next_downbeat(*last_onset) : t;
        stop_at_ = std::max(t, end);
    }
    if (state == State::Encerrando) {
        if (!onset) {
            if (gap > P::FORGET_BARS * bar()) {   // (> HOLD_STOP_BARS: a held pattern stops before it forgets)
                auto out = set(-1, Mode::Silence, t, "silêncio longo: fim");
                forget();
                reason = "silêncio longo: esqueci padrão e andamento";
                return out;
            }
            if (t + lookahead_ >= stop_at_) return set(-1, Mode::Silence, std::max(t, stop_at_), "você parou: fim do compasso");
            return std::nullopt;
        }
        state = memory_.locked ? State::Travado : State::Ouvindo;   // resumed: it was a pause
        confirm_at_ = tracker_.has_period() ? t + P::LOCK_CONFIRM_BEATS * tracker_.period : t;
    }

    // confidence ladder
    if (level == Level::Confiante)
        doubt_since_.reset();
    else if (!doubt_since_)
        doubt_since_ = t;
    if (state == State::Fallback) {
        if (level != Level::Confiante) return std::nullopt;
        state = State::Ouvindo;
    } else if (doubt_since_ && t - *doubt_since_ > P::DOUBT_TO_FALLBACK_BARS * bar() &&
               !(held() && state == State::Travado)) {   // a held pattern beats the tonic
        state = State::Fallback;
        memory_.unlock();
        reason = "dúvida por mais de 1 compasso";
        pending_.reset();
        return tonic_out(t, "perdido");
    }

    if (state == State::Travado) return locked(t, idx);
    return reactive(t, idx, evidence);
}

std::optional<int> Engine::current_key() const {
    if (key_pc && key_fits(*key_pc, heard_)) return key_pc;
    return detect_key(heard_);
}

std::optional<Output> Engine::tonic_out(double t, const std::string& why) {
    auto k = current_key();
    if (!k) {
        if (key_pc)   // configured key contradicted and nothing better yet: its tonic would be wrong
            return set(-1, Mode::Silence, t, why + "; o tom configurado não bate com o que ouvi: silêncio");
        return set(-1, Mode::Silence, t, why + "; ainda sem tom: silêncio");
    }
    return set(*k, Mode::Tonic, t, why + ": pad na tônica (" + label(*k) + ")");
}

// Hold mode: mistakes, late entries and doubts never break the pattern; only a confident disagreement sustained
// for HOLD_CONTRA_BARS (the musician really moved on) makes it relearn.
std::optional<Output> Engine::held_check(double t, int idx, bool late) {
    if (level != Level::Confiante || idx == N_IDX) return std::nullopt;
    if ((cur.first == idx && cur.second == Mode::Full) || late) {
        disagree_since_.reset();
        return std::nullopt;
    }
    if (!disagree_since_) {
        disagree_since_ = t;
        return std::nullopt;
    }
    if (t - *disagree_since_ < P::HOLD_CONTRA_BARS * bar()) return std::nullopt;
    disagree_since_.reset();
    memory_.correct(idx);
    memory_.unlock();
    state = State::Ouvindo;
    tracker_.observe_change(change_time(t));
    if (learn) return reactive_change(t, idx);
    return set(idx, Mode::Full, t, "você mudou de verdade (2 compassos diferentes do padrão): ouvi " + label(idx) + ", reaprendendo");
}

std::optional<Output> Engine::reactive(double t, int idx, const std::array<double, 25>& evidence) {
    if (pending_ && t + lookahead_ + frame_ >= pending_->second) {
        auto [chord, when] = *pending_;
        pending_.reset();
        return set(chord, Mode::Full, std::max(t, when), "metrônomo: troca no tempo forte");
    }
    if (level == Level::Confiante) {
        // a chord change is born from a strum: ignore drifts between strums (noise, voice, decay)
        bool fresh = last_onset && t - *last_onset < P::CHANGE_AFTER_ONSET;
        bool fresh_chord = learn ? idx != last_heard_ : !(cur.first == idx && cur.second == Mode::Full);
        if (fresh && fresh_chord && !(pending_ && pending_->first == idx)) return reactive_change(t, idx);
        return std::nullopt;
    }
    if (learn) return std::nullopt;
    // major/minor ambiguity on the same root: play root + fifth, no third
    // (if we already play one of the two, holding it is the safer choice and avoids retriggering)
    int b = 0;
    for (int i = 1; i < 24; ++i)
        if (evidence[i] > evidence[b]) b = i;
    int a = b == 0 ? 1 : 0;
    for (int i = 0; i < 24; ++i)
        if (i != b && evidence[i] > evidence[a]) a = i;
    if (level == Level::Duvida && a % 12 == b % 12 && evidence[a] + evidence[b] >= P::CONF_HIGH && cur.first != a &&
        cur.first != b)
    {
        root5_tc_ = change_time(t);
        return set(b, Mode::Root5, t, "dúvida " + label(a) + "/" + label(b) + ": sem terça");
    }
    return std::nullopt;
}

std::optional<Output> Engine::reactive_change(double t, int idx) {
    heard_.push_back(idx);
    if (heard_.size() > 12) heard_.pop_front();
    last_heard_ = idx;
    double tc = change_time(t);
    if (cur.second == Mode::Root5 && cur.first >= 0 && cur.first % 12 == idx % 12 && root5_tc_)
        tc = *root5_tc_;   // same chord, now with its third: it started when the root did
    if (tracker_.has_period()) {
        tracker_.observe_change(tc);
        // changes land on (or just before) a beat; detection lag and off-beat strums only push them later
        bool blip = tc - last_change_t_ < tracker_.period;
        memory_.push(idx, (long long)std::floor(tracker_.beat_phase(tc) + 0.3), blip);
        last_change_t_ = tc;
        if (memory_.locked) {
            state = State::Travado;
            confirm_at_ = t + P::LOCK_CONFIRM_BEATS * tracker_.period;
            expected_.reset();
            misses_ = 0;
        }
    }
    if (learn && state != State::Travado) return tonic_out(t, "aprendendo o padrão");   // hold a quiet tonic
    if (metronome && tracker_.has_period()) {
        double ph = tracker_.beat_phase(tc);
        long long k = (long long)std::floor(ph);
        while (tracker_.bar_pos(k) % 2) --k;
        if (ph - k > P::METRO_LATE_BEATS) {   // early (or mid-bar): wait for the next strong beat
            k += 2;
            pending_ = std::make_pair(idx, tracker_.time_of_beat(k));
            reason = "metrônomo: " + label(idx) + " agendado para o tempo forte";
            return std::nullopt;
        }
    }
    pending_.reset();
    std::string why = "ouvi " + label(idx) + (state == State::Travado ? " — padrão reconhecido, travei" : "");
    return set(idx, Mode::Full, t, why);
}

std::optional<Output> Engine::locked(double t, int idx) {
    auto pred = memory_.predict();
    double period = tracker_.period;
    if (pred) {
        auto [nxt, beat] = *pred;
        double tc = tracker_.time_of_beat(beat);
        // the musician changed early: follow (except in metronome mode, where the grid rules)
        bool early = !metronome && level == Level::Confiante && idx == nxt && cur.first != nxt && t >= confirm_at_ &&
                     t >= tc - period;
        if (t + lookahead_ + frame_ >= tc || early) {
            double when = early ? t : std::max(t, tc);
            if (early)   // the musician is ahead of the stored grid: learn the real start (fixes a systematic offset)
                beat = (long long)std::floor(tracker_.beat_phase(change_time(t)) + 0.3);
            memory_.advance(beat);
            confirm_at_ = when + P::LOCK_CONFIRM_BEATS * period + 0.1;
            expected_ = nxt;
            prev_chord_ = cur.first >= 0 ? std::optional<int>(cur.first) : std::nullopt;
            changed_at_ = when;
            contra_.reset();
            return set(nxt, Mode::Full, when, early ? "previsto pelo padrão (você antecipou)" : "previsto pelo padrão");
        }
    }
    if (t < confirm_at_) return std::nullopt;
    // still hearing the previous chord shortly after a predicted change = musician is late, not a contradiction
    bool late = prev_chord_ && idx == *prev_chord_ && t < changed_at_ + P::LOCK_LATE_BEATS * period;
    if (hold) return held_check(t, idx, late);
    bool same = cur.first == idx && cur.second == Mode::Full;
    if (level == Level::Confiante && idx != N_IDX && !same && !late) {
        // a confident disagreement must persist before it overrides the pattern (one bad frame is noise)
        if (!contra_ || *contra_ != idx) {
            contra_ = idx;
            contra_since_ = t;
        } else if (t - contra_since_ >= P::LOCK_CONTRA_BEATS * period) {
            contra_.reset();
            if (cur.first >= 0 && idx % 12 == cur.first % 12)   // same root, other third: keep pattern, drop third
                return set(cur.first, Mode::Root5, t, "dúvida maior/menor (" + label(idx) + "): sem terça");
            memory_.correct(idx);   // the audio wins: forget the lock and relearn
            memory_.unlock();
            state = State::Ouvindo;
            tracker_.observe_change(change_time(t));
            if (learn) return reactive_change(t, idx);
            return set(idx, Mode::Full, t, "contradição sustentada: ouvi " + label(idx) + ", destravei");
        }
    } else {
        contra_.reset();
    }
    if (expected_) {
        if (idx == *expected_) {
            misses_ = 0;
        } else if (!late && idx != N_IDX && level != Level::Perdido) {
            if (++misses_ >= P::LOCK_MAX_MISSES) {
                memory_.unlock();
                state = State::Ouvindo;
                reason = "várias discordâncias fracas: destravei";
            }
        }
        expected_.reset();
    }
    return std::nullopt;
}

}  // namespace kordia
