// Streaming stereo synth: worship pad, bass, click, Freeverb-style reverb, soft limiter (Python: synth.py).
// Real-time safe after construction: tables are precomputed, voices are reserved, no allocation in render().
#include <algorithm>

#include "kordia/core.h"

namespace kordia {

static constexpr double PI = 3.14159265358979323846;
static constexpr int TABLE = 2048;
static double hz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

double Synth::Comb::process(double x) {
    // y[n] = x[n] + g * ((1-d) y[n-D] + d y[n-D-1]); buf holds the last D+1 outputs
    int L = D + 1;
    double yD = buf[(i + 1) % L], yD1 = buf[i];
    double y = x + g * ((1 - d) * yD + d * yD1);
    buf[i] = y;
    i = (i + 1) % L;
    return y;
}

double Synth::Allpass::process(double x) {
    double y = -g * x + xb[i] + g * yb[i];   // x[n-D], y[n-D]
    xb[i] = x;
    yb[i] = y;
    i = (i + 1) % D;
    return y;
}

Synth::Synth(double sr) : sr_(sr) {
    voices_.reserve(512);
    bass_.resize(8192);
    pad_.resize(8192);
    bright_.resize(8192);
    click_.resize(8192);
    // 2nd-order Butterworth low-pass (same as scipy.signal.butter(2, fc / (sr/2)))
    double K = std::tan(PI * P::PAD_CUTOFF / sr), r2 = std::sqrt(2.0), norm = 1 / (1 + r2 * K + K * K);
    b0_ = K * K * norm, b1_ = 2 * b0_, b2_ = b0_;
    a1_ = 2 * (K * K - 1) * norm, a2_ = (1 - r2 * K + K * K) * norm;
    static const int combs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, aps[] = {556, 441, 341, 225};
    double k = sr / 44100.0;
    for (int c = 0; c < 2; ++c) {
        int extra = c ? 23 : 0;
        for (int d : combs) {
            int D = (int)((d + extra) * k);
            ch_[c].combs.push_back({D, P::REVERB_ROOM, P::REVERB_DAMP, std::vector<double>(D + 1, 0.0)});
        }
        for (int d : aps) {
            int D = (int)((d + extra) * k);
            ch_[c].aps.push_back({D, 0.5, std::vector<double>(D, 0.0), std::vector<double>(D, 0.0)});
        }
    }
    for (int m = 24; m <= 108; ++m) saw(hz(m));   // precompute every table a note can need
}

const std::vector<double>& Synth::saw(double f) {
    int nh = (int)std::max(1.0, std::min(48.0, sr_ / 2 / f / 1.1));
    auto it = tables_.find(nh);
    if (it != tables_.end()) return it->second;
    std::vector<double> t(TABLE, 0.0);
    for (int i = 0; i < TABLE; ++i)
        for (int h = 1; h <= nh; ++h) t[i] += std::sin(2 * PI * h * i / TABLE) / h;
    for (double& v : t) v *= 2 / PI;
    return tables_.emplace(nh, std::move(t)).first->second;
}

static double lookup(const std::vector<double>& tab, double phase) {
    double x = (phase - std::floor(phase)) * TABLE;
    int i = (int)x;
    double frac = x - i;
    return tab[i] * (1 - frac) + tab[(i + 1) % TABLE] * frac;
}

void Synth::handle(const Event& e) {
    if (e.type == EvType::On) {
        voices_.push_back({e.inst, e.pitch, e.vel, e.t, e.inst == Inst::Click ? e.t + 0.06 : -1.0, -1.0});
    } else if (e.type == EvType::Off) {
        for (auto& v : voices_)
            if (v.off < 0 && v.inst == e.inst && v.pitch == e.pitch) v.off = e.t, v.rel = e.rel;
    } else {
        expr_from_ = expr_to_, expr_to_ = e.value, expr_t_ = e.t;
    }
}

static const double LAYER_RELEASE[P::N_LAYERS] = {P::PAD_RELEASE, P::ORGAN_RELEASE, P::PIANO_RELEASE,
                                                   P::GTRPAD_RELEASE, P::STRINGS_RELEASE};

double Synth::release(const Voice& v) const {
    if (v.rel >= 0) return v.rel;
    if (v.inst == Inst::Pad) {
        double r = -1;
        for (int k = 0; k < P::N_LAYERS; ++k)
            if (layers[k] > 0) r = std::max(r, LAYER_RELEASE[k]);
        return r < 0 ? P::PAD_RELEASE : r;
    }
    return v.inst == Inst::Bass ? P::BASS_RELEASE : 0.0;
}

static double swell(double dt, double attack) { return 0.5 - 0.5 * std::cos(PI * std::clamp(dt / attack, 0.0, 1.0)); }

// One sample of pad layer k for one note: osc * envelope * level; `warm` layers go through the low-pass.
double Synth::pad_layer(int k, double f, double t, double dt, const std::vector<double>* tab, bool& warm) const {
    warm = true;
    if (k == 0) {   // worship: 3 detuned band-limited saws, smooth swell
        static const double c[3] = {0.996, 1.0, 1.004};
        double o = 0;
        for (double ci : c) o += lookup(*tab, f * ci * t);
        return P::PAD_LEVEL * swell(dt, P::PAD_ATTACK) * (o / 3);
    }
    if (k == 1) {   // organ: drawbar harmonics, quick attack, 6.5 Hz tremolo
        warm = false;
        static const int h[6] = {1, 2, 3, 4, 6, 8};
        static const double w[6] = {1.0, 0.8, 0.6, 0.5, 0.3, 0.2};
        double o = 0;
        for (int i = 0; i < 6; ++i)
            if (h[i] * f < sr_ / 2) o += w[i] * std::sin(2 * PI * h[i] * f * t);
        o = o / 3.4 * (1 + 0.12 * std::sin(2 * PI * 6.5 * t));
        return P::ORGAN_LEVEL * std::clamp(dt / P::ORGAN_ATTACK, 0.0, 1.0) * o;
    }
    if (k == 2) {   // electric piano: struck, bell partials decay fast, body decays slow
        warm = false;
        double d = std::max(dt, 0.0);
        double o = std::sin(2 * PI * f * t) + 0.35 * std::exp(-d / 0.4) * std::sin(2 * PI * 2 * f * t) +
                   0.15 * std::exp(-d / 0.2) * std::sin(2 * PI * 3 * f * t);
        double env = std::clamp(dt / 0.004, 0.0, 1.0) * (0.35 + 0.65 * std::exp(-d / 1.2));
        return P::PIANO_LEVEL * env * o;
    }
    if (k == 3) {   // ambient guitar pad: soft triangle + octave, very slow swell
        double o = 0;
        for (int h = 1; h <= 9; h += 2)
            if (h * f < sr_ / 2) o += ((h / 2) % 2 == 0 ? 1.0 : -1.0) / (h * h) * std::sin(2 * PI * h * f * t);
        o = o * (8 / (PI * PI)) + 0.35 * std::sin(2 * PI * 2 * f * t);
        return P::GTRPAD_LEVEL * swell(dt, P::GTRPAD_ATTACK) * o;
    }
    // strings: 5 detuned saws with 5 Hz vibrato (phase integral of f(1 + 0.003 sin))
    static const double c[5] = {0.993, 0.997, 1.0, 1.003, 1.007};
    double vib = t - 0.003 / (2 * PI * 5) * std::cos(2 * PI * 5 * t), o = 0;
    for (double ci : c) o += lookup(*tab, f * ci * vib);
    return P::STRINGS_LEVEL * swell(dt, P::STRINGS_ATTACK) * (o / 5);
}

void Synth::render(double t0, int n, double* left, double* right) {
    if (n > (int)bass_.size()) n = (int)bass_.size();   // callers use blocks far below 8192
    std::fill(bass_.begin(), bass_.begin() + n, 0.0);
    std::fill(pad_.begin(), pad_.begin() + n, 0.0);
    std::fill(bright_.begin(), bright_.begin() + n, 0.0);
    std::fill(click_.begin(), click_.begin() + n, 0.0);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.off >= 0 && t0 > v.off + release(v); }),
                  voices_.end());
    for (const auto& v : voices_) {
        double f = hz(v.pitch), amp = v.vel / 127.0, rel = release(v);
        if (v.inst == Inst::Click) {
            for (int s = 0; s < n; ++s) {
                double dt = t0 + s / sr_ - v.on;
                if (dt >= 0 && dt < 0.06)
                    click_[s] += P::CLICK_LEVEL * amp * std::exp(-dt / 0.012) * std::sin(2 * PI * (v.pitch == 77 ? 1500 : 1000) * dt);
            }
            continue;
        }
        if (v.inst == Inst::Bass) {
            if (bass_gain <= 0) continue;
            for (int s = 0; s < n; ++s) {
                double t = t0 + s / sr_, dt = t - v.on;
                double env = std::clamp(dt / 0.008, 0.0, 1.0) * (0.7 + 0.3 * std::exp(-std::max(dt, 0.0) / 0.5));
                double ph = 2 * PI * f * t;
                double osc = std::tanh(2.2 * std::sin(ph)) / std::tanh(2.2) + 0.25 * std::sin(2 * ph);
                if (v.off >= 0) {
                    double r = std::clamp((t - v.off) / std::max(rel, 1e-3), 0.0, 1.0);
                    env *= (1 - r) * (1 - r);
                }
                bass_[s] += bass_gain * P::BASS_LEVEL * amp * env * osc;
            }
            continue;
        }
        const std::vector<double>* tab = &tables_.at((int)std::max(1.0, std::min(48.0, sr_ / 2 / f / 1.1)));
        for (int k = 0; k < P::N_LAYERS; ++k) {
            if (layers[k] <= 0) continue;
            double lr = v.rel >= 0 ? v.rel : LAYER_RELEASE[k];
            for (int s = 0; s < n; ++s) {
                double t = t0 + s / sr_, dt = t - v.on;
                bool warm;
                double sig = pad_layer(k, f, t, dt, tab, warm);
                if (v.off >= 0) {
                    double r = std::clamp((t - v.off) / std::max(lr, 1e-3), 0.0, 1.0);
                    sig *= (1 - r) * (1 - r);
                }
                (warm ? pad_ : bright_)[s] += layers[k] * amp * sig;
            }
        }
    }
    double eb = 0, ep = 0;
    for (int s = 0; s < n; ++s) {
        double x = pad_[s];   // low-pass on the warm layers (direct form II transposed, as scipy lfilter)
        double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        double pad = y + bright_[s];
        eb += bass_[s] * bass_[s];
        ep += pad * pad;
        double wet[2];
        for (int c = 0; c < 2; ++c) {
            double acc = 0;
            for (auto& cb : ch_[c].combs) acc += cb.process(pad);
            acc *= 0.12;
            for (auto& ap : ch_[c].aps) acc = ap.process(acc);
            wet[c] = acc;
        }
        double t = t0 + s / sr_;
        double a = std::clamp((t - expr_t_) / 0.05, 0.0, 1.0);
        double gain = expr_from_ + (expr_to_ - expr_from_) * a;
        double dry = bass_[s] + P::PAD_DRY * pad;
        left[s] = std::tanh((dry + P::PAD_WET * wet[0]) * gain) + click_[s];   // soft limiter
        right[s] = std::tanh((dry + P::PAD_WET * wet[1]) * gain) + click_[s];
    }
    if (n > 0 && (t0 + (n - 1) / sr_ - expr_t_) / 0.05 >= 1) expr_from_ = expr_to_;
    if (n > 0) bass_rms = std::sqrt(eb / n), pad_rms = std::sqrt(ep / n);
}

}  // namespace kordia
