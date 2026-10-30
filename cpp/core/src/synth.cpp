// Streaming stereo synth: worship pad, bass, click, Freeverb-style reverb, soft limiter.
// Real-time safe after construction: tables are precomputed, voices are reserved, no allocation in render().
#include <algorithm>

#include "kordia/core.h"

namespace kordia {

static constexpr double PI = 3.14159265358979323846;
static constexpr int TABLE = 2048;
static double hz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

double Synth::Comb::process(double x) {
    // y[n] = x[n] + g * ((1-d) y[n-D] + d y[n-D-1]); buf holds the last D+1 outputs
    int next = i == D ? 0 : i + 1;
    double yD = buf[next], yD1 = buf[i];
    double y = x + g * ((1 - d) * yD + d * yD1);
    buf[i] = y;
    i = next;
    return y;
}

double Synth::Allpass::process(double x) {
    double y = -g * x + xb[i] + g * yb[i];   // x[n-D], y[n-D]
    xb[i] = x;
    yb[i] = y;
    i = i + 1 == D ? 0 : i + 1;
    return y;
}

Synth::Synth(double sr) : sr_(sr) {
    voices_.reserve(512);
    for (auto* v : {&bass_, &pad_, &bright_, &click_, &t_, &sig_, &trem_, &vib_}) v->resize(8192);
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

static double lookup(const double* tab, double phase) {   // phase >= 0: time only moves forward
    double x = (phase - (double)(long long)phase) * TABLE;
    int i = (int)x;
    double frac = x - i;
    return tab[i] * (1 - frac) + tab[(i + 1) & (TABLE - 1)] * frac;
}

// A sine by rotation: only the first sample pays for sin and cos, and one block is too short for it to drift.
struct Sine {
    double c = 0, s = 0, dc = 1, ds = 0;
    Sine() = default;
    Sine(double w, double phase) : c(std::cos(phase)), s(std::sin(phase)), dc(std::cos(w)), ds(std::sin(w)) {}
    double next() {
        double v = s, nc = c * dc - s * ds;
        s = s * dc + c * ds, c = nc;
        return v;
    }
};

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

static double swell(double dt, double attack) {
    return dt >= attack ? 1.0 : 0.5 - 0.5 * std::cos(PI * std::max(dt / attack, 0.0));
}

// One block of pad layer k for one note into sig_: osc * envelope * level. The sines rotate; see Sine.
bool Synth::pad_layer(int k, double f, double on, double t0, int n, const double* tab) {
    const double w = 2 * PI * f / sr_, ph = 2 * PI * f * t0;
    double* out = sig_.data();
    const double* t = t_.data();
    if (k == 0) {   // worship: 3 detuned band-limited saws, smooth swell
        static const double c[3] = {0.996, 1.0, 1.004};
        for (int s = 0; s < n; ++s) {
            double o = 0;
            for (double ci : c) o += lookup(tab, f * ci * t[s]);
            out[s] = P::PAD_LEVEL * swell(t[s] - on, P::PAD_ATTACK) * (o / 3);
        }
        return true;
    }
    if (k == 1) {   // organ: drawbar harmonics, quick attack, 6.5 Hz tremolo
        static const int h[6] = {1, 2, 3, 4, 6, 8};
        static const double weight[6] = {1.0, 0.8, 0.6, 0.5, 0.3, 0.2};
        Sine osc[6];
        double wt[6];
        int m = 0;
        for (int i = 0; i < 6; ++i)
            if (h[i] * f < sr_ / 2) osc[m] = Sine(h[i] * w, h[i] * ph), wt[m++] = weight[i];
        for (int s = 0; s < n; ++s) {
            double o = 0;
            for (int i = 0; i < m; ++i) o += wt[i] * osc[i].next();
            out[s] = P::ORGAN_LEVEL * std::clamp((t[s] - on) / P::ORGAN_ATTACK, 0.0, 1.0) * (o / 3.4 * trem_[s]);
        }
        return false;
    }
    if (k == 2) {   // electric piano: struck, bell partials decay fast, body decays slow
        Sine s1(w, ph), s2(2 * w, 2 * ph), s3(3 * w, 3 * ph);
        const double d0 = std::max(t[0] - on, 0.0), step = 1 / sr_;
        double e2 = std::exp(-d0 / 0.4), e3 = std::exp(-d0 / 0.2), body = std::exp(-d0 / 1.2);
        const double r2 = std::exp(-step / 0.4), r3 = std::exp(-step / 0.2), rb = std::exp(-step / 1.2);
        for (int s = 0; s < n; ++s) {
            double dt = t[s] - on;
            double o = s1.next() + 0.35 * e2 * s2.next() + 0.15 * e3 * s3.next();
            out[s] = P::PIANO_LEVEL * std::clamp(dt / 0.004, 0.0, 1.0) * (0.35 + 0.65 * body) * o;
            if (dt > 0) e2 *= r2, e3 *= r3, body *= rb;
        }
        return false;
    }
    if (k == 3) {   // ambient guitar pad: soft triangle + octave, very slow swell
        Sine osc[5], octave(2 * w, 2 * ph);
        double wt[5];
        int m = 0;
        for (int h = 1; h <= 9; h += 2)
            if (h * f < sr_ / 2) osc[m] = Sine(h * w, h * ph), wt[m++] = ((h / 2) % 2 == 0 ? 1.0 : -1.0) / (h * h);
        for (int s = 0; s < n; ++s) {
            double o = 0;
            for (int i = 0; i < m; ++i) o += wt[i] * osc[i].next();
            o = o * (8 / (PI * PI)) + 0.35 * octave.next();
            out[s] = P::GTRPAD_LEVEL * swell(t[s] - on, P::GTRPAD_ATTACK) * o;
        }
        return true;
    }
    // strings: 5 detuned saws with 5 Hz vibrato (phase integral of f(1 + 0.003 sin))
    static const double c[5] = {0.993, 0.997, 1.0, 1.003, 1.007};
    for (int s = 0; s < n; ++s) {
        double o = 0;
        for (double ci : c) o += lookup(tab, f * ci * vib_[s]);
        out[s] = P::STRINGS_LEVEL * swell(t[s] - on, P::STRINGS_ATTACK) * (o / 5);
    }
    return true;
}

void Synth::render(double t0, int n, double* left, double* right) {
    if (n > (int)bass_.size()) n = (int)bass_.size();   // callers use blocks far below 8192
    if (n <= 0) return;
    std::fill(bass_.begin(), bass_.begin() + n, 0.0);
    std::fill(pad_.begin(), pad_.begin() + n, 0.0);
    std::fill(bright_.begin(), bright_.begin() + n, 0.0);
    std::fill(click_.begin(), click_.begin() + n, 0.0);
    for (int s = 0; s < n; ++s) t_[s] = t0 + s / sr_;
    if (layers[1] > 0 || layers_from_[1] > 0)
        for (int s = 0; s < n; ++s) trem_[s] = 1 + 0.12 * std::sin(2 * PI * 6.5 * t_[s]);
    if (layers[4] > 0 || layers_from_[4] > 0)
        for (int s = 0; s < n; ++s) vib_[s] = t_[s] - 0.003 / (2 * PI * 5) * std::cos(2 * PI * 5 * t_[s]);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.off >= 0 && t0 > v.off + release(v); }),
                  voices_.end());
    static const double TANH_DRIVE = std::tanh(2.2);
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
            if (bass_gain <= 0 && bass_from_ <= 0) continue;
            Sine osc(2 * PI * f / sr_, 2 * PI * f * t0);
            double gain = bass_from_, step = (bass_gain - bass_from_) / n;
            for (int s = 0; s < n; ++s) {
                double t = t_[s], dt = t - v.on;
                double env = std::clamp(dt / 0.008, 0.0, 1.0) * (0.7 + 0.3 * std::exp(-std::max(dt, 0.0) / 0.5));
                double sine = osc.s, sine2 = 2 * osc.s * osc.c;   // sin(2x) = 2 sin(x) cos(x)
                osc.next();
                double wave = std::tanh(2.2 * sine) / TANH_DRIVE + 0.25 * sine2;
                if (v.off >= 0) {
                    double r = std::clamp((t - v.off) / std::max(rel, 1e-3), 0.0, 1.0);
                    env *= (1 - r) * (1 - r);
                }
                gain += step;
                bass_[s] += gain * P::BASS_LEVEL * amp * env * wave;
            }
            continue;
        }
        const double* tab = tables_.at((int)std::max(1.0, std::min(48.0, sr_ / 2 / f / 1.1))).data();
        for (int k = 0; k < P::N_LAYERS; ++k) {
            if (layers[k] <= 0 && layers_from_[k] <= 0) continue;
            double lr = v.rel >= 0 ? v.rel : LAYER_RELEASE[k];
            double* bus = pad_layer(k, f, v.on, t0, n, tab) ? pad_.data() : bright_.data();
            double gain = layers_from_[k], step = (layers[k] - layers_from_[k]) / n;
            for (int s = 0; s < n; ++s) {
                double sig = sig_[s];
                if (v.off >= 0) {
                    double r = std::clamp((t_[s] - v.off) / std::max(lr, 1e-3), 0.0, 1.0);
                    sig *= (1 - r) * (1 - r);
                }
                gain += step;
                bus[s] += gain * amp * sig;
            }
        }
    }
    layers_from_ = layers;
    bass_from_ = bass_gain;
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
        double a = std::clamp((t_[s] - expr_t_) / 0.05, 0.0, 1.0);
        double gain = expr_from_ + (expr_to_ - expr_from_) * a;
        double dry = bass_[s] + P::PAD_DRY * pad;
        left[s] = std::tanh((dry + P::PAD_WET * wet[0]) * gain) + click_[s];   // soft limiter
        right[s] = std::tanh((dry + P::PAD_WET * wet[1]) * gain) + click_[s];
    }
    if ((t_[n - 1] - expr_t_) / 0.05 >= 1) expr_from_ = expr_to_;
    bass_rms = std::sqrt(eb / n), pad_rms = std::sqrt(ep / n);
}

}  // namespace kordia
