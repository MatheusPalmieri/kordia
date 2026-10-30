// Synthetic acoustic guitar (Karplus-Strong) and chord shapes. Only used to build test scenarios, so it may be offline.
#include <algorithm>
#include <cmath>
#include <map>

#include "lab.h"

namespace lab {

int chord_transpose(int idx, int n) {
    int root = idx % 12;
    return (int)floor_mod(root + n, 12) + (idx >= 12 ? 12 : 0);
}

std::vector<int> voicing(const std::string& shape, int capo) {
    // open voicings (MIDI notes, low to high); others are built below
    static const std::map<std::string, std::vector<int>> OPEN = {
        {"C", {48, 52, 55, 60, 64}}, {"G", {43, 47, 50, 55, 59, 67}}, {"Am", {45, 52, 57, 60, 64}},
        {"F", {41, 48, 53, 57, 60, 65}}, {"Em", {40, 47, 52, 55, 59, 64}}, {"E", {40, 47, 52, 56, 59, 64}},
        {"Bb", {46, 53, 58, 62, 65}}, {"D", {50, 57, 62, 66}}, {"A", {45, 52, 57, 61, 64}},
        {"Dm", {50, 57, 62, 65}}, {"Bm", {47, 54, 59, 62, 66}}, {"Am/G", {43, 52, 57, 60, 64}},
        // worship shapes (exam)
        {"D/F#", {42, 50, 57, 62, 66}}, {"Em7", {40, 47, 52, 55, 62, 67}}, {"Cadd9", {48, 52, 55, 62, 67}},
        {"G/B", {47, 50, 55, 59, 67}}, {"Dsus4", {50, 57, 62, 67}}};
    std::vector<int> notes;
    if (auto it = OPEN.find(shape); it != OPEN.end()) {
        notes = it->second;
    } else {   // E-shape barre on the low E string, or A-shape on the A string
        int idx = chord_index(shape), r = idx % 12, third = idx >= 12 ? 15 : 16;
        std::vector<int> barre = r >= 4 ? std::vector<int>{0, 7, 12, third, 19, 24} : std::vector<int>{0, 7, 12, third, 19};
        int base = r >= 4 ? 40 + (int)floor_mod(r - 4, 12) : 45 + (int)floor_mod(r - 9, 12);
        for (int s : barre) notes.push_back(base + s);
    }
    for (int& n : notes) n += capo;
    return notes;
}

std::vector<double> pluck(double midi, double dur, double amp, double bright, Rng& rng, double t60) {
    double f = 440.0 * std::pow(2.0, (midi - 69) / 12);
    double D = SR / f;
    int N = (int)std::floor(D - 0.5 - 0.1);
    double frac = D - 0.5 - N, c = (1 - frac) / (1 + frac);
    if (t60 == 0.0) t60 = interp(midi, {40, 76}, {3.5, 1.2});
    double d = std::pow(0.001, 1.0 / (f * t60));
    // H = Da / (Da - d z^-N L Na), L = (1+z^-1)/2, allpass A = (c + z^-1)/(1 + c z^-1): one IIR
    double a_n = -(d * (0.5 * c)), a_n1 = -(d * (0.5 * c + 0.5)), a_n2 = -(d * 0.5);
    size_t n = (size_t)(dur * SR);
    std::vector<double> burst((size_t)std::min<long long>(N, (long long)n));
    for (double& v : burst) v = rng.uniform(-1, 1);
    double mean = np_mean(burst);
    for (double& v : burst) v -= mean;   // DC would ring forever in the loop
    // softer strums are darker: one-pole lowpass on the excitation
    burst = lfilter({bright}, {1, -(1 - bright)}, burst);

    // lfilter([1, c], a, x) with a mostly zero `a`: the same transposed form, keeping only the nonzero terms in a ring
    std::vector<double> y(n), ring((size_t)(N - 1), 0.0);
    double z0 = 0.0, zn = 0.0, zn1 = 0.0;
    size_t p = 0;
    for (size_t i = 0; i < n; ++i) {
        double x = i < burst.size() ? burst[i] : 0.0;
        double yn = z0 + x;
        y[i] = yn * amp;
        z0 = ring[p] + x * c - yn * c;
        ring[p] = zn - yn * a_n;
        p = p + 1 == ring.size() ? 0 : p + 1;
        zn = zn1 - yn * a_n1;
        zn1 = 0.0 - yn * a_n2;
    }
    return y;
}

Guitar::Guitar(double duration, uint32_t seed, double cents) : out((size_t)(duration * SR) + SR, 0.0), rng_(seed) {
    Rng tune(seed + 1);   // own generator: cents = 0 keeps every scenario identical
    for (double& t : tuning) t = tune.normal(0.0, cents / 100);
}

void Guitar::cut(size_t index, long long at) {
    Ringing r = std::move(ringing_[index]);
    ringing_.erase(ringing_.begin() + (std::ptrdiff_t)index);
    long long len = (long long)r.y.size(), k = std::max(0LL, at - r.start), fade = std::min(220LL, len - k);
    if (fade <= 0) return;
    auto ramp = linspace(0, 1, (int)fade);
    for (long long i = 0; i < fade; ++i) out[(size_t)(r.start + k + i)] -= r.y[(size_t)(k + i)] * ramp[(size_t)i];
    for (long long i = k + fade; i < len; ++i) out[(size_t)(r.start + i)] -= r.y[(size_t)i];
}

void Guitar::mute(double t) {
    long long at = (long long)(t * SR);
    while (!ringing_.empty()) cut(0, at);
}

void Guitar::add(int string, double t, std::vector<double> y) {
    long long at = (long long)(t * SR);
    for (size_t i = 0; i < ringing_.size(); ++i)
        if (ringing_[i].string == string) { cut(i, at); break; }
    y.resize(std::min(y.size(), out.size() - (size_t)at));
    for (size_t i = 0; i < y.size(); ++i) out[(size_t)at + i] += y[i];
    ringing_.push_back({string, at, std::move(y)});
}

void Guitar::strum(double t, const std::vector<int>& notes, char direction, double amp, double spread) {
    std::vector<std::pair<int, int>> pairs;   // 'D' = low to high over all strings; 'U' = high to low over the top 3-4
    for (size_t i = 0; i < notes.size(); ++i) pairs.push_back({6 - (int)notes.size() + (int)i, notes[i]});
    if (direction == 'U') {
        std::reverse(pairs.begin(), pairs.end());
        pairs.resize(std::min(pairs.size(), (size_t)rng_.integers(3, 5)));
    }
    double bright = std::clamp(0.35 + 0.6 * amp, 0.2, 1.0);
    for (size_t i = 0; i < pairs.size(); ++i) {
        auto [s, m] = pairs[i];
        double a = amp * (direction == 'U' ? 0.8 : 1.0) * rng_.uniform(0.85, 1.0);
        double at = t + (double)i * spread * rng_.uniform(0.7, 1.3);
        add(s, at, pluck(m + tuning[(size_t)s], 4.0, 0.3 * a, bright, rng_));
    }
}

void Guitar::single(double t, int midi, double amp) {
    add(midi < 45 ? 0 : 1, t, pluck(midi, 3.0, 0.35 * amp, 0.8, rng_));
}

void Guitar::chk(double t, double amp) {   // percussive: band-limited noise with a fast decay, no clear pitch
    mute(t);
    size_t n = (size_t)(0.08 * SR);
    std::vector<double> noise(n);
    for (double& v : noise) v = rng_.normal();
    noise = lfilter({1, 0, -1}, {1, -1.6, 0.7}, noise);   // crude band-pass ~ 300-2500 Hz
    size_t at = (size_t)(long long)(t * SR);
    for (size_t i = 0; i < n && at + i < out.size(); ++i)
        out[at + i] += amp * 0.15 * noise[i] * std::exp(-(double)i / (0.025 * SR));
}

}  // namespace lab
