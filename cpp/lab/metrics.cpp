// Compare a run against the scenario ground truth; the offline run itself.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "lab.h"

namespace lab {

static constexpr double END_TOL = 0.3;   // s of grace after a true end (note-off lands on the bar line, +/- tracking error)

Result run(std::vector<double> a, const LabConfig& cfg, double lookahead, double sr, const Layers& layers, double bass_gain) {
    Processor proc(cfg, lookahead, sr);
    proc.synth().layers = layers;
    proc.synth().bass_gain = bass_gain;
    long long fb_delay = (long long)(cfg.fb_delay * sr);
    size_t n = a.size(), blocks = n / P::HOP;
    Result r;
    r.accomp.assign(n * 2, 0.0);
    r.log.reserve(blocks);
    std::vector<double> L(P::HOP), R(P::HOP);
    for (size_t k = 0; k < blocks; ++k) {
        size_t s = k * P::HOP;
        proc.render(s / sr + lookahead, P::HOP, L.data(), R.data());
        for (int i = 0; i < P::HOP; ++i) r.accomp[2 * (s + i)] = L[i], r.accomp[2 * (s + i) + 1] = R[i];
        if (cfg.fb_gain != 0.0 && (long long)s >= fb_delay)
            for (int i = 0; i < P::HOP; ++i) {
                size_t j = s - (size_t)fb_delay + i;
                a[s + i] += cfg.fb_gain * 0.5 * (r.accomp[2 * j] + r.accomp[2 * j + 1]);
            }
        proc.analyze(a.data() + s);
        r.log.push_back(proc.frame);
    }
    r.audio = std::move(a);
    r.events = proc.acc().events;
    r.changes = proc.acc().changes;
    r.onsets = proc.onset_list;
    r.decisions = proc.decisions;
    return r;
}

std::vector<double> time_grid(double duration) {
    std::vector<double> g((size_t)std::ceil(duration / DT));
    for (size_t i = 0; i < g.size(); ++i) g[i] = (double)i * DT;
    return g;
}

// index of the last x <= value, clipped to [0, n-1]
template <typename It, typename Key>
static size_t last_at(It first, size_t n, double value, Key key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (key(first[(std::ptrdiff_t)mid]) <= value) lo = mid + 1;
        else hi = mid;
    }
    return lo == 0 ? 0 : lo - 1;
}

std::vector<int> truth_labels(const Truth& truth, const std::vector<double>& g) {
    std::vector<int> labels(g.size());
    for (size_t i = 0; i < g.size(); ++i)
        labels[i] = truth.chords[last_at(truth.chords.begin(), truth.chords.size(), g[i], [](const Seg& s) { return s.s; })].lab;
    return labels;
}

Timeline timeline(const std::vector<Change>& changes, const std::vector<double>& g) {
    Timeline tl{std::vector<int>(g.size(), -1), std::vector<Mode>(g.size(), Mode::Silence)};
    if (changes.empty()) return tl;
    for (size_t i = 0; i < g.size(); ++i) {
        if (g[i] < changes.front().t) continue;
        const Change& c = changes[last_at(changes.begin(), changes.size(), g[i], [](const Change& x) { return x.t; })];
        tl.lab[i] = c.chord, tl.mode[i] = c.mode;
    }
    return tl;
}

std::vector<long long> runs(const std::vector<char>& mask) {
    std::vector<long long> lengths;
    long long n = 0;
    for (char m : mask) {
        if (m) ++n;
        else if (n) lengths.push_back(n), n = 0;
    }
    if (n) lengths.push_back(n);
    return lengths;
}

static size_t lower(const std::vector<double>& x, double v) { return (size_t)(std::lower_bound(x.begin(), x.end(), v) - x.begin()); }
static double nan() { return std::nan(""); }

Metrics evaluate(const Result& res, const Truth& truth) {
    auto g = time_grid(truth.duration);
    auto tl = truth_labels(truth, g);
    auto [lab, mode] = timeline(res.changes, g);
    size_t n = g.size(), frames = res.log.size();
    std::vector<double> log_t(frames);
    for (size_t i = 0; i < frames; ++i) log_t[i] = res.log[i].t;
    std::vector<char> conf(n), is_chord(n), sounding(n), correct(n);
    std::vector<State> state(n);
    std::vector<double> bpm_true(n), beat(n);
    for (size_t i = 0; i < n; ++i) {
        size_t li = std::min(lower(log_t, g[i]), frames - 1);
        conf[i] = res.log[li].level == Level::Confiante;
        state[i] = res.log[li].state;
        bpm_true[i] = interp(g[i], truth.tempo_t, truth.tempo_bpm);
        beat[i] = 60.0 / bpm_true[i];
        is_chord[i] = tl[i] != N_IDX && tl[i] != PAUSE;
        sounding[i] = mode[i] != Mode::Silence;
        correct[i] = is_chord[i] && lab[i] == tl[i] && mode[i] == Mode::Full;
    }
    auto share = [&](auto&& where, auto&& what) {
        size_t total = 0, hits = 0;
        for (size_t i = 0; i < n; ++i)
            if (where(i)) ++total, hits += what(i) ? 1 : 0;
        return total ? (double)hits / (double)total : nan();
    };
    auto chord = [&](size_t i) { return is_chord[i] != 0; };
    auto right = [&](size_t i) { return correct[i] != 0; };
    Metrics m;
    auto real = [&](const char* k, double v) { m.push_back({k, {Val::Real, round_digits(v, 3)}}); };
    auto integer = [&](const char* k, long long v) { m.push_back({k, {Val::Int, (double)v}}); };
    real("acc_total", share(chord, right));
    real("acc_confiante", share([&](size_t i) { return chord(i) && conf[i]; }, right));
    real("acc_nao_confiante", share([&](size_t i) { return chord(i) && !conf[i]; }, right));
    real("cobertura", share(chord, [&](size_t i) { return sounding[i] != 0; }));
    real("tempo_nao_confiante", share(chord, [&](size_t i) { return !conf[i]; }));

    // change latency
    auto bpm_at = [&](double t) { return interp(t, truth.tempo_t, truth.tempo_bpm); };
    std::vector<double> lats, lat_beats, reactive, on_lock;
    long long missed = 0;
    for (const auto& c : truth.changes) {
        double b = 60.0 / bpm_at(c.t);
        std::optional<double> cand;
        for (const auto& ch : res.changes)
            if (c.t - b <= ch.t && ch.t <= c.t + 2 * b && ch.chord == c.to && ch.mode == Mode::Full) { cand = ch.t; break; }
        if (!cand) { ++missed; continue; }
        double ms = (*cand - c.t) * 1000;
        lats.push_back(ms);
        (res.log[std::min(lower(log_t, *cand), frames - 1)].state == State::Travado ? on_lock : reactive).push_back(ms);
        lat_beats.push_back((*cand - c.t) / b);
    }
    real("lat_media_ms", lats.empty() ? nan() : np_mean(lats));
    real("lat_p95_ms", lats.empty() ? nan() : percentile(lats, 95));
    real("lat_media_pulso", lat_beats.empty() ? nan() : np_mean(lat_beats));
    real("lat_reativo_ms", reactive.empty() ? nan() : np_mean(reactive));
    real("lat_travado_ms", on_lock.empty() ? nan() : np_mean(on_lock));
    integer("trocas_perdidas", missed);
    integer("trocas_total", (long long)truth.changes.size());

    // tempo
    std::vector<double> err_play, rel(frames);
    for (size_t i = 0; i < frames; ++i) {
        size_t bi = std::min(lower(g, log_t[i]), n - 1);
        double est = res.log[i].bpm, err = std::abs(est - bpm_true[bi]);
        if (is_chord[bi] && !std::isnan(est)) err_play.push_back(err);
        rel[i] = std::isnan(est) ? 1.0 : err / bpm_true[bi];
    }
    real("bpm_erro_medio", err_play.empty() ? nan() : np_mean(err_play));
    double first = truth.chords.front().s;
    for (const auto& s : truth.chords)
        if (s.lab != N_IDX && s.lab != PAUSE) { first = s.s; break; }
    double lock = nan();
    size_t frames2s = (size_t)(2.0 / (log_t[1] - log_t[0]));
    for (size_t i = lower(log_t, first); i < frames; ++i) {
        bool ok = true;
        for (size_t k = i; k < std::min(frames, i + frames2s) && ok; ++k) ok = rel[k] < 0.04;
        if (ok) { lock = log_t[i] - first; break; }
    }
    real("t_trava_tempo_s", lock);

    // false triggers: sounding where the truth is silence (grace after each end)
    std::vector<char> forbidden(n);
    for (size_t i = 0; i < n; ++i) forbidden[i] = tl[i] == N_IDX;
    for (double e : truth.ends)
        for (size_t i = 0; i < n; ++i)
            if (g[i] >= e && g[i] < e + END_TOL) forbidden[i] = 0;
    long long false_frames = 0, false_on = 0;
    for (size_t i = 0; i < n; ++i) false_frames += sounding[i] && forbidden[i];
    for (const auto& ev : res.events)
        if (ev.type == EvType::On) false_on += forbidden[std::min((size_t)(long long)(ev.t / DT), n - 1)];
    real("falso_disparo_s", (double)false_frames * DT);
    integer("falso_disparo_n", false_on);

    // confidently wrong (outside the first beat after each true change, which is latency)
    std::vector<char> near(n, 0), wrong(n);
    for (const auto& c : truth.changes) {
        double b = 60.0 / bpm_at(c.t);
        for (size_t i = 0; i < n; ++i)
            if (g[i] >= c.t && g[i] < c.t + b) near[i] = 1;
    }
    for (size_t i = 0; i < n; ++i) wrong[i] = is_chord[i] && conf[i] && mode[i] == Mode::Full && lab[i] != tl[i] && !near[i];
    double half_beat = 0.5 * median(beat);
    long long confident_wrong = 0;
    for (long long r : runs(wrong)) confident_wrong += (double)r * DT >= half_beat;
    integer("errado_confiante_n", confident_wrong);

    // stopping: last sound after each end, relative to the end (bar line)
    std::vector<double> stops;
    std::vector<char> forgot;
    for (double e : truth.ends) {
        double bar = 4 * 60.0 / bpm_at(e), nxt = truth.duration;
        for (const auto& s : truth.chords)
            if (s.s > e && s.lab != N_IDX && s.lab != PAUSE) { nxt = s.s; break; }
        std::optional<double> last_sound;
        for (size_t i = 0; i < n; ++i)
            if (g[i] >= e - bar && g[i] < nxt && sounding[i]) last_sound = g[i];
        stops.push_back(last_sound ? *last_sound + DT - e : -bar);
        if (nxt < truth.duration) {
            char any = 0;
            for (size_t i = 0; i < n; ++i) any |= g[i] >= e && g[i] < nxt && state[i] == State::Ocioso;
            forgot.push_back(any);
        }
    }
    real("parada_s", stops.empty() ? nan() : *std::max_element(stops.begin(), stops.end()));
    bool stopped = !stops.empty() && std::all_of(stops.begin(), stops.end(), [](double s) { return -0.25 <= s && s <= END_TOL + 0.05; });
    m.push_back({"parou_no_compasso", {Val::Bool, stopped ? 1.0 : 0.0}});
    if (forgot.empty()) m.push_back({"esqueceu", {}});
    else m.push_back({"esqueceu", {Val::Bool, std::all_of(forgot.begin(), forgot.end(), [](char f) { return f != 0; }) ? 1.0 : 0.0}});

    // recovery after surprising changes
    std::vector<double> rec;
    for (const auto& c : truth.changes) {
        if (!c.surprise) continue;
        double b = 60.0 / bpm_at(c.t), seg_end = 0;
        for (const auto& s : truth.chords)
            if (s.s <= c.t && c.t < s.e) { seg_end = s.e; break; }
        double back = seg_end;
        for (size_t i = 0; i < n; ++i)
            if (g[i] >= c.t && g[i] < seg_end && lab[i] == c.to && mode[i] == Mode::Full) { back = g[i]; break; }
        rec.push_back((back - c.t) / b);
    }
    real("recuperacao_pulsos", rec.empty() ? nan() : *std::max_element(rec.begin(), rec.end()));

    // dynamics: strum gain per bar vs. velocity of the notes started in that bar
    std::vector<double> gains, vels;
    for (const auto& [t0, gain] : truth.intensity) {
        double bar = 4 * 60.0 / bpm_at(t0);
        std::vector<double> v;
        for (const auto& ev : res.events)
            if (ev.type == EvType::On && t0 <= ev.t && ev.t < t0 + bar) v.push_back((double)ev.vel);
        if (!v.empty()) gains.push_back(gain), vels.push_back(np_mean(v));
    }
    bool varied = gains.size() > 2 && std_dev(gains) > 0 && std_dev(vels) > 0;
    real("corr_dinamica", varied ? corrcoef(gains, vels) : nan());
    return m;
}

const Val& metric_val(const Metrics& m, const std::string& key) {
    for (const auto& [k, v] : m)
        if (k == key) return v;
    throw std::invalid_argument("métrica desconhecida: " + key);
}

double metric(const Metrics& m, const std::string& key) {
    const Val& v = metric_val(m, key);
    if (v.kind == Val::None) throw std::invalid_argument("métrica vazia: " + key);
    return v.v;
}

std::string val_csv(const Val& v) {
    switch (v.kind) {
        case Val::None: return "";
        case Val::Bool: return v.v != 0 ? "True" : "False";
        case Val::Int: return std::to_string((long long)v.v);
        default: return shortest_repr(v.v);
    }
}

std::string val_json(const Val& v) {
    switch (v.kind) {
        case Val::None: return "null";
        case Val::Bool: return v.v != 0 ? "true" : "false";
        case Val::Int: return std::to_string((long long)v.v);
        default: return std::isnan(v.v) ? "NaN" : shortest_repr(v.v);
    }
}

}  // namespace lab
