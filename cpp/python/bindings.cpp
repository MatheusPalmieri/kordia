// kordia_core: Python bindings so the lab (scenarios, metrics, reports, session replay) runs the C++ core.
// run_offline() mirrors acompanhante.pipeline.run() and returns the same pieces as its Result.
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "kordia/core.h"

namespace py = pybind11;
using namespace kordia;

static constexpr double SR = 22050.0;

static RunConfig cfg_from(const py::dict& d) {
    RunConfig c;
    auto str = [&](const char* k) -> std::optional<std::string> {
        if (!d.contains(k) || d[k].is_none()) return std::nullopt;
        return d[k].cast<std::string>();
    };
    if (auto m = str("modo")) c.modo = *m;
    c.tom = str("tom");
    c.progressao = str("progressao");
    if (d.contains("bpm") && !d["bpm"].is_none()) c.bpm = d["bpm"].cast<double>();
    if (d.contains("clique") && !d["clique"].is_none()) c.clique = d["clique"].cast<bool>();
    return c;
}

static py::object opt_label(int chord) { return chord < 0 ? py::object(py::none()) : py::object(py::str(label(chord))); }
static py::object opt_mode(Mode m) { return m == Mode::Silence ? py::object(py::none()) : py::object(py::str(mode_name(m))); }

static py::dict event_dict(const Event& e) {
    static const char* types[] = {"on", "off", "expr"};
    static const char* insts[] = {"bass", "pad", "click", "all"};
    py::dict d;
    d["t"] = e.t;
    d["type"] = types[(int)e.type];
    d["inst"] = insts[(int)e.inst];
    d["pitch"] = e.pitch;
    d["vel"] = e.vel;
    if (e.type == EvType::Expr) d["value"] = e.value;
    if (e.rel >= 0) d["rel"] = e.rel;
    return d;
}

static py::dict run_offline(py::array_t<double, py::array::c_style | py::array::forcecast> audio_in, const py::dict& cfg,
                            double lookahead) {
    std::vector<double> a(audio_in.data(), audio_in.data() + audio_in.size());
    RunConfig c = cfg_from(cfg);
    double fb_gain = 0.0;
    long long fb_delay = 0;
    if (cfg.contains("feedback") && !cfg["feedback"].is_none()) {
        py::dict fb = cfg["feedback"].cast<py::dict>();
        fb_gain = fb["gain"].cast<double>();
        fb_delay = (long long)(fb["delay"].cast<double>() * SR);
    }
    Processor proc(c, lookahead, SR);
    size_t n = a.size(), blocks = n / P::HOP;
    std::vector<double> out(n * 2, 0.0), L(P::HOP), R(P::HOP);
    std::vector<double> t, db, flux, bpm, tconf, conf, expr;
    std::vector<int> onset, heard, state, vel;
    std::vector<std::string> level;
    {
    py::gil_scoped_release release;   // pure C++ from here: other Python threads may run
    for (size_t k = 0; k < blocks; ++k) {
        size_t s = k * P::HOP;
        proc.render(s / SR + lookahead, P::HOP, L.data(), R.data());
        for (int i = 0; i < P::HOP; ++i) out[2 * (s + i)] = L[i], out[2 * (s + i) + 1] = R[i];
        if (fb_gain != 0.0 && (long long)s >= fb_delay)
            for (int i = 0; i < P::HOP; ++i) {
                size_t j = s - fb_delay + i;
                a[s + i] += fb_gain * 0.5 * (out[2 * j] + out[2 * j + 1]);
            }
        proc.analyze(a.data() + s);
        const Frame& f = proc.frame;
        t.push_back(f.t), db.push_back(f.db), flux.push_back(f.flux), onset.push_back(f.onset), bpm.push_back(f.bpm);
        tconf.push_back(f.tempo_conf), heard.push_back(f.heard), conf.push_back(f.conf), state.push_back((int)f.state);
        level.push_back(level_name(f.level)), expr.push_back(f.expr), vel.push_back(f.vel);
    }
    }
    py::dict log;
    log["t"] = py::array(py::cast(t));
    log["db"] = py::array(py::cast(db));
    log["flux"] = py::array(py::cast(flux));
    log["onset"] = py::array_t<bool>(py::array(py::cast(onset)));
    log["bpm"] = py::array(py::cast(bpm));
    log["tempo_conf"] = py::array(py::cast(tconf));
    log["heard"] = py::array(py::cast(heard));
    log["conf"] = py::array(py::cast(conf));
    log["state"] = py::array(py::cast(state));
    log["level"] = py::array(py::cast(level));
    log["expr"] = py::array(py::cast(expr));
    log["vel"] = py::array(py::cast(vel));
    py::list events, changes, onsets, decisions;
    for (const auto& e : proc.acc().events) events.append(event_dict(e));
    for (const auto& ch : proc.acc().changes) changes.append(py::make_tuple(ch.t, opt_label(ch.chord), opt_mode(ch.mode)));
    for (const auto& o : proc.onset_list) onsets.append(py::make_tuple(o.first, o.second));
    for (const auto& d : proc.decisions)
        decisions.append(py::make_tuple(d.t, state_name(d.state), opt_label(d.chord), opt_mode(d.mode), d.reason));
    py::dict r;
    r["audio"] = py::array_t<double>({(py::ssize_t)n}, a.data());
    r["accomp"] = py::array_t<double>({(py::ssize_t)n, (py::ssize_t)2}, out.data());
    r["log"] = log;
    r["events"] = events;
    r["changes"] = changes;
    r["onsets"] = onsets;
    r["decisions"] = decisions;
    return r;
}

static py::dict params() {
    py::dict d;
#define K(name) d[#name] = P::name
    K(HOP); K(ONSET_WIN); K(CHROMA_WIN); K(ONSET_FMIN); K(ONSET_FMAX); K(ONSET_MEDIAN_FRAMES); K(ONSET_K); K(ONSET_DELTA);
    K(ONSET_REFRACTORY); K(SILENCE_DB); K(TEMPO_ENV_SECONDS); K(TEMPO_ACF_EVERY); K(TEMPO_BPM_MIN); K(TEMPO_BPM_MAX);
    K(TEMPO_PRIOR_BPM); K(TEMPO_PRIOR_OCTAVES); K(TEMPO_MIN_ONSETS); K(PLL_WINDOW); K(PLL_ALPHA); K(PLL_BETA);
    K(TEMPO_RESET_DEV); K(TEMPO_CONF_BEATS); K(BEATS_PER_BAR); K(CHROMA_FMIN); K(CHROMA_FMAX); K(CHROMA_GAMMA);
    K(TEMPLATE_HARMONICS); K(TEMPLATE_DECAY); K(HMM_BETA); K(HMM_N_SCORE); K(HMM_P_CHANGE); K(HMM_P_CHANGE_ONSET);
    K(HMM_P_CHANGE_STRONG); K(HMM_ONSET_BOOST_TIME); K(KEY_PRIOR_OUT); K(CONF_BETA); K(CONF_SMOOTH); K(CONF_HIGH);
    K(CONF_LOW); K(CHANGE_AFTER_ONSET); K(DOUBT_TO_FALLBACK_BARS); K(STOP_BEATS); K(STOP_IOI_FACTOR);
    K(STOP_DEFAULT_SECONDS); K(FORGET_BARS); K(LOCK_CONFIRM_BEATS); K(LOCK_MAX_MISSES); K(LOCK_CONTRA_BEATS);
    K(LOCK_LATE_BEATS); K(CYCLE_MIN); K(CYCLE_MAX); K(METRO_LATE_BEATS); K(DYN_DB_LOW); K(DYN_DB_HIGH); K(DYN_VEL_LOW);
    K(DYN_VEL_HIGH); K(DYN_SMOOTH); K(DYN_SILENCE_DECAY); K(BASS_LOW); K(PAD_ROOT_LOW); K(PAD_SHIMMER); K(PAD_ATTACK);
    K(PAD_RELEASE); K(PAD_CUTOFF); K(PAD_LEVEL); K(PAD_DRY); K(PAD_WET); K(REVERB_ROOM); K(REVERB_DAMP); K(BASS_LEVEL);
    K(BASS_RELEASE); K(CLICK_LEVEL); K(STOP_FADE);
#undef K
    return d;
}

PYBIND11_MODULE(kordia_core, m) {
    m.doc() = "Kordia C++ core (same algorithm as the Python reference in acompanhante/)";
    m.def("run_offline", &run_offline, py::arg("audio"), py::arg("cfg"), py::arg("lookahead") = 0.0);
    m.def("params", &params);
}
