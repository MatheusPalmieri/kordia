// Spectrum, onset detector and chord tracker.
#include <algorithm>
#include <complex>

#include "kordia/core.h"

namespace kordia {

static constexpr double PI = 3.14159265358979323846;

Spectrum::Spectrum(int n) : n_(n), win_(n), mag_(n / 2 + 1), rev_(n / 2), z_(n / 2), tw_(n / 2 + 1) {
    double s = 0;
    for (int i = 0; i < n; ++i) {   // np.hanning
        win_[i] = 0.5 - 0.5 * std::cos(2 * PI * i / (n - 1));
        s += win_[i];
    }
    norm_ = 2.0 / s;
    for (int k = 0; k <= n / 2; ++k) tw_[k] = std::polar(1.0, -2 * PI * k / n);
    const int h = n / 2;
    for (int i = 0, j = 0; i < h; ++i) {
        rev_[i] = j;
        int bit = h >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
    }
}

// Real FFT through a complex one of half the size, tables ready beforehand: on the audio thread nothing is allocated.
const std::vector<double>& Spectrum::operator()(const double* x) {
    const int h = n_ / 2;
    for (int i = 0; i < h; ++i) z_[rev_[i]] = {x[2 * i] * win_[2 * i], x[2 * i + 1] * win_[2 * i + 1]};
    for (int size = 2; size <= h; size *= 2)
        for (int start = 0; start < h; start += size)
            for (int k = 0; k < size / 2; ++k) {
                auto& a = z_[start + k];
                auto& b = z_[start + k + size / 2];
                auto t = b * tw_[k * (n_ / size)];
                b = a - t;
                a += t;
            }
    for (int k = 0; k <= h; ++k) {
        auto a = z_[k % h], b = std::conj(z_[(h - k) % h]);
        auto X = 0.5 * (a + b) + std::complex<double>(0, -0.5) * tw_[k] * (a - b);
        mag_[k] = std::sqrt(X.real() * X.real() + X.imag() * X.imag()) * norm_;
    }
    return mag_;
}

static std::vector<int> band_bins(int n, double sr, double fmin, double fmax) {
    std::vector<int> b;
    for (int i = 0; i <= n / 2; ++i) {
        double f = i * sr / n;   // np.fft.rfftfreq
        if (f >= fmin && f <= fmax) b.push_back(i);
    }
    return b;
}

// ------------------------------------------------------------------ onsets
OnsetDetector::OnsetDetector(double sr) : band_(band_bins(P::ONSET_WIN, sr, P::ONSET_FMIN, P::ONSET_FMAX)) {
    prev_.resize(band_.size());
    lm_.resize(band_.size());
}

double OnsetDetector::median() {
    size_t n = 0;
    for (double v : hist_) sorted_[n++] = v;
    std::sort(sorted_.begin(), sorted_.begin() + n);
    return n % 2 ? sorted_[n / 2] : 0.5 * (sorted_[n / 2 - 1] + sorted_[n / 2]);
}

std::optional<Onset> OnsetDetector::process(const std::vector<double>& mag, double t, double rms_db) {
    for (size_t i = 0; i < band_.size(); ++i) lm_[i] = std::log1p(P::CHROMA_GAMMA * mag[band_[i]]);
    double f = 0.0;
    if (has_prev_) {
        for (size_t i = 0; i < band_.size(); ++i) f += std::max(lm_[i] - prev_[i], 0.0);
        f /= 10.0;
    }
    std::swap(prev_, lm_);
    has_prev_ = true;
    double thr = (hist_.empty() ? 0.0 : median()) * P::ONSET_K + P::ONSET_DELTA;
    if (t - last_t_ < P::ONSET_MASK_TIME) thr = std::max(thr, P::ONSET_MASK * last_flux_);   // own ringing
    hist_.push_back(f);
    if ((int)hist_.size() > P::ONSET_MEDIAN_FRAMES) hist_.pop_front();
    bool rising = f > prev_flux_;
    prev_flux_ = flux = f;
    if (f > thr && rising && rms_db > P::SILENCE_DB && t - last_t_ >= P::ONSET_REFRACTORY) {
        last_t_ = t;
        last_flux_ = f;
        return Onset{t, rms_db, f};
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ chords
ChordTracker::ChordTracker(double sr, std::optional<int> key_pc)
    : band_(band_bins(P::CHROMA_WIN, sr, P::CHROMA_FMIN, P::CHROMA_FMAX)), T_(templates()), prior_(key_prior(key_pc)) {
    std::vector<long long> midi;
    for (int b : band_) midi.push_back(round_even(69 + 12 * std::log2(b * sr / P::CHROMA_WIN / 440.0)));
    long long lo = *std::min_element(midi.begin(), midi.end()), hi = *std::max_element(midi.begin(), midi.end());
    for (long long m : midi) semi_.push_back((int)(m - lo));
    for (long long s = 0; s <= hi - lo; ++s) semi_pc_.push_back((int)floor_mod(s + lo, 12));
    semis_.resize(semi_pc_.size());
    post = prior_;
    evidence.fill(1.0 / 25);
}

ChordTracker::Result ChordTracker::process(const std::vector<double>& mag, bool silent, double p_change) {
    std::fill(semis_.begin(), semis_.end(), 0.0);
    for (size_t i = 0; i < band_.size(); ++i)
        semis_[semi_[i]] = std::max(semis_[semi_[i]], std::log1p(P::CHROMA_GAMMA * mag[band_[i]]));
    std::array<double, 12> c{};
    for (size_t s = 0; s < semis_.size(); ++s) c[semi_pc_[s]] += semis_[s];
    last_chroma = c;
    double norm = 0;
    for (double v : c) norm += v * v;
    norm = std::sqrt(norm);
    std::array<double, 25> s{};
    if (!silent && norm > 1e-6)
        for (int i = 0; i < 24; ++i) {
            double d = 0;
            for (int k = 0; k < 12; ++k) d += T_[i][k] * (c[k] / norm);
            s[i] = d;
        }
    s[24] = P::HMM_N_SCORE;
    double smax = *std::max_element(s.begin(), s.end());
    double ps = 0, fs = 0;
    std::array<double, 25> frame;
    for (int i = 0; i < 25; ++i) {
        post[i] = ((1 - p_change) * post[i] + p_change * prior_[i]) * std::exp(P::HMM_BETA * (s[i] - smax));
        ps += post[i];
        frame[i] = std::exp(P::CONF_BETA * (s[i] - smax));
        fs += frame[i];
    }
    int idx = 0;
    for (int i = 0; i < 25; ++i) {
        post[i] /= ps;
        evidence[i] = (1 - P::CONF_SMOOTH) * evidence[i] + P::CONF_SMOOTH * frame[i] / fs;
        if (post[i] > post[idx]) idx = i;
    }
    return {idx, evidence[idx]};
}

}  // namespace kordia
