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

double Synth::release(const Voice& v) const {
    if (v.rel >= 0) return v.rel;
    return v.inst == Inst::Bass ? P::BASS_RELEASE : v.inst == Inst::Pad ? P::PAD_RELEASE : 0.0;
}

void Synth::render(double t0, int n, double* left, double* right) {
    if (n > (int)bass_.size()) n = (int)bass_.size();   // callers use blocks far below 8192
    std::fill(bass_.begin(), bass_.begin() + n, 0.0);
    std::fill(pad_.begin(), pad_.begin() + n, 0.0);
    std::fill(click_.begin(), click_.begin() + n, 0.0);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.off >= 0 && t0 > v.off + release(v); }),
                  voices_.end());
    static const double detune[3] = {0.996, 1.0, 1.004};
    for (const auto& v : voices_) {
        double f = hz(v.pitch), amp = v.vel / 127.0, rel = release(v);
        const std::vector<double>* tab = v.inst == Inst::Pad ? &tables_.at((int)std::max(1.0, std::min(48.0, sr_ / 2 / f / 1.1))) : nullptr;
        for (int s = 0; s < n; ++s) {
            double t = t0 + s / sr_, dt = t - v.on;
            if (v.inst == Inst::Click) {
                if (dt >= 0 && dt < 0.06)
                    click_[s] += P::CLICK_LEVEL * amp * std::exp(-dt / 0.012) * std::sin(2 * PI * (v.pitch == 77 ? 1500 : 1000) * dt);
                continue;
            }
            double env, osc;
            if (v.inst == Inst::Bass) {
                env = std::clamp(dt / 0.008, 0.0, 1.0) * (0.7 + 0.3 * std::exp(-std::max(dt, 0.0) / 0.5));
                double ph = 2 * PI * f * t;
                osc = std::tanh(2.2 * std::sin(ph)) / std::tanh(2.2) + 0.25 * std::sin(2 * ph);
            } else {
                double a = std::clamp(dt / P::PAD_ATTACK, 0.0, 1.0);
                env = 0.5 - 0.5 * std::cos(PI * a);   // smooth swell
                osc = 0;
                for (double c : detune) osc += lookup(*tab, f * c * t);
                osc /= 3;
            }
            if (v.off >= 0) {
                double r = std::clamp((t - v.off) / std::max(rel, 1e-3), 0.0, 1.0);
                env *= (1 - r) * (1 - r);
            }
            (v.inst == Inst::Bass ? bass_ : pad_)[s] += (v.inst == Inst::Bass ? P::BASS_LEVEL : P::PAD_LEVEL) * amp * env * osc;
        }
    }
    double eb = 0, ep = 0;
    for (int s = 0; s < n; ++s) {
        double x = pad_[s];   // low-pass (direct form II transposed, as scipy lfilter)
        double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        pad_[s] = y;
        eb += bass_[s] * bass_[s];
        ep += y * y;
        double wet[2];
        for (int c = 0; c < 2; ++c) {
            double acc = 0;
            for (auto& cb : ch_[c].combs) acc += cb.process(y);
            acc *= 0.12;
            for (auto& ap : ch_[c].aps) acc = ap.process(acc);
            wet[c] = acc;
        }
        double t = t0 + s / sr_;
        double a = std::clamp((t - expr_t_) / 0.05, 0.0, 1.0);
        double gain = expr_from_ + (expr_to_ - expr_from_) * a;
        double dry = bass_[s] + P::PAD_DRY * y;
        left[s] = std::tanh((dry + P::PAD_WET * wet[0]) * gain) + click_[s];   // soft limiter
        right[s] = std::tanh((dry + P::PAD_WET * wet[1]) * gain) + click_[s];
    }
    if (n > 0 && (t0 + (n - 1) / sr_ - expr_t_) / 0.05 >= 1) expr_from_ = expr_to_;
    if (n > 0) bass_rms = std::sqrt(eb / n), pad_rms = std::sqrt(ep / n);
}

}  // namespace kordia
