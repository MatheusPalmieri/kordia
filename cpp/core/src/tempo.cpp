// 'When to change' layer (Python: tempo.py).
#include <algorithm>
#include <cctype>

#include "kordia/core.h"

namespace kordia {

// ------------------------------------------------------------------ Grid
void Grid::reset() {
    period = 0.0;
    next_beat = 0.0;
    next_index = 0;
    bar_offset = 0;
    first_onset.reset();
    onsets_.clear();
    hits_.clear();
    pending_.clear();
    votes_.clear();
}

double Grid::confidence() const {
    if (hits_.empty()) return 0.0;
    int n = 0;
    for (bool h : hits_) n += h;
    return (double)n / hits_.size();
}

double Grid::next_downbeat(double t) const {
    long long idx = (long long)std::floor(beat_phase(t) + 0.25) + 1;   // a strum slightly early belongs to its beat
    while (bar_pos(idx) != 0) ++idx;
    return time_of_beat(idx);
}

bool Grid::is_strong(double t, double tol) const {
    double ph = beat_phase(t);
    long long idx = pyround(ph);
    return std::abs(ph - idx) <= tol && bar_pos(idx) % 2 == 0;
}

void Grid::observe_change(double t) {
    if (!has_period()) return;
    double ph = beat_phase(t);
    long long idx = pyround(ph);
    if (std::abs(ph - idx) > 0.3) return;
    votes_.push_back(pymod(idx - bar_offset, P::BEATS_PER_BAR));
    if (votes_.size() > 8) votes_.pop_front();
    // Counter.most_common(1): highest count, ties broken by first appearance
    long long best = 0;
    int best_n = 0;
    std::vector<long long> seen;
    for (long long v : votes_) {
        if (std::find(seen.begin(), seen.end(), v) != seen.end()) continue;
        seen.push_back(v);
        int n = (int)std::count(votes_.begin(), votes_.end(), v);
        if (n > best_n) best = v, best_n = n;
    }
    if (best != 0 && best_n >= 3) {
        bar_offset = pymod(bar_offset + best, P::BEATS_PER_BAR);
        votes_.clear();
    }
}

void Grid::advance(double t) {
    while (has_period() && t >= next_beat) {
        pending_.push_back(next_beat);
        next_beat += period;
        ++next_index;
    }
    double win = has_period() ? P::PLL_WINDOW * period : 0.0;
    while (!pending_.empty() && t >= pending_.front() + win) {
        double b = pending_.front();
        pending_.pop_front();
        bool hit = false;
        for (double o : onsets_) hit |= std::abs(o - b) <= win;
        hits_.push_back(hit);
        if ((int)hits_.size() > P::TEMPO_CONF_BEATS) hits_.pop_front();
    }
}

void Grid::start(double t0, double p) {
    period = p;
    long long k = (long long)std::ceil((now - t0) / p);
    next_beat = t0 + k * p;
    next_index = pyround((next_beat - *first_onset) / p);
}

// ------------------------------------------------------------------ TempoTracker
TempoTracker::TempoTracker(double frame) : frame_(frame), env_max_((size_t)(P::TEMPO_ENV_SECONDS / frame)) {
    now = 0.0;
    reset();
}

void TempoTracker::reset() {
    if (frozen_) return;   // a fixed BPM survives the end of a song
    Grid::reset();
    env_.clear();
    frames_ = 0;
    disagree_ = 0;
    if (seeded()) apply_seed();   // the click grid keeps its phase across songs
}

void TempoTracker::seed(double bpm, double t0) {
    seed_bpm_ = bpm, seed_t0_ = t0;
    now = 0.0;
    apply_seed();
}

void TempoTracker::apply_seed() {
    period = 60.0 / seed_bpm_;
    first_onset = seed_t0_;
    long long k = std::max(0LL, (long long)std::ceil((now - seed_t0_) / period));
    next_beat = seed_t0_ + k * period;
    next_index = k;
}

void TempoTracker::update(double t, double flux, const std::optional<Onset>& onset) {
    now = t;
    env_.push_back(flux);
    if (env_.size() > env_max_) env_.pop_front();
    ++frames_;
    if (onset) {
        if (!first_onset) first_onset = onset->t;
        onsets_.push_back(onset->t);
        if (onsets_.size() > 64) onsets_.pop_front();
        if (has_period() && !frozen_) pll(onset->t);
    }
    if (!frozen_ && frames_ % P::TEMPO_ACF_EVERY == 0 && (int)onsets_.size() >= P::TEMPO_MIN_ONSETS) acf();
    advance(t);
}

void TempoTracker::pll(double to) {
    double prev = next_beat - period;
    double nearest = std::abs(to - prev) < std::abs(to - next_beat) ? prev : next_beat;
    double e = to - nearest;
    if (std::abs(e) > P::PLL_WINDOW * period) return;   // off-beat strum: ignored
    next_beat += P::PLL_ALPHA * e;
    period = std::clamp(period + P::PLL_BETA * e, 60 / P::TEMPO_BPM_MAX, 60 / P::TEMPO_BPM_MIN);
}

void TempoTracker::acf() {
    std::vector<double> recent;
    for (double o : onsets_)
        if (o > now - P::TEMPO_ENV_SECONDS) recent.push_back(o);
    if ((int)recent.size() < P::TEMPO_MIN_ONSETS) return;
    std::vector<double> e(env_.begin(), env_.end());
    double mean = 0;
    for (double v : e) mean += v;
    mean /= e.size();
    for (double& v : e) v -= mean;
    long long n = (long long)e.size();
    long long lmin = (long long)(60 / P::TEMPO_BPM_MAX / frame_);
    long long lmax = std::min((long long)(60 / P::TEMPO_BPM_MIN / frame_), n / 2);
    if (lmax <= lmin + 2) return;
    std::vector<double> acfv;
    int best = 0;
    double best_score = -INF;
    for (long long lag = lmin; lag <= lmax; ++lag) {
        double d = 0;
        for (long long i = 0; i < n - lag; ++i) d += e[i] * e[i + lag];
        acfv.push_back(d);
        double bpm = 60 / (lag * frame_);
        double w = std::exp(-0.5 * std::pow(std::log2(bpm / P::TEMPO_PRIOR_BPM) / P::TEMPO_PRIOR_OCTAVES, 2));
        double score = std::max(d, 0.0) * w;
        if (score > best_score) best_score = score, best = (int)(lag - lmin);
    }
    if (best_score <= 0) return;
    double lag = (double)(lmin + best);   // parabolic interpolation on the raw ACF for sub-frame lag
    if (best > 0 && best < (int)acfv.size() - 1) {
        double a = acfv[best - 1], b = acfv[best], c = acfv[best + 1];
        double denom = a - 2 * b + c;
        if (denom < 0) lag += 0.5 * (a - c) / denom;
    }
    double p = lag * frame_;
    if (!has_period()) {
        start(best_phase(recent, p), p);
    } else if (std::abs(p - period) / period > P::TEMPO_RESET_DEV) {
        if (++disagree_ >= 3) period = p, disagree_ = 0;
    } else {
        disagree_ = 0;
    }
}

double TempoTracker::best_phase(const std::vector<double>& r, double p) const {
    double best = r[0];
    int best_s = -1;
    for (double o : r) {
        int s = 0;
        for (double x : r) {
            double d = (x - o) / p;
            s += std::abs(d - std::nearbyint(d)) < 0.12;
        }
        if (s > best_s) best = o, best_s = s;
    }
    return best;
}

// ------------------------------------------------------------------ FixedGrid
FixedGrid::FixedGrid(double bpm, std::optional<double> start) : fixed_period_(60.0 / bpm), start_(start) { reset(); }

void FixedGrid::reset() {
    Grid::reset();
    if (start_) {   // anchored: the grid keeps its phase across songs
        first_onset = *start_;
        period = fixed_period_;
        long long k = std::max(0LL, (long long)std::ceil((now - *start_) / period));
        next_beat = *start_ + k * period;
        next_index = k;
    }
}

void FixedGrid::update(double t, double, const std::optional<Onset>& onset) {
    now = t;
    if (onset) {
        if (!first_onset) {
            first_onset = onset->t;
            start(onset->t, fixed_period_);
        }
        onsets_.push_back(onset->t);
        if (onsets_.size() > 64) onsets_.pop_front();
    }
    advance(t);
}

// ------------------------------------------------------------------ ProgressionMemory
ProgressionMemory::ProgressionMemory(const std::optional<std::string>& progression) {
    if (progression) {
        size_t i = 0;
        const std::string& s = *progression;
        while (i < s.size()) {
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            size_t j = i;
            while (j < s.size() && !std::isspace((unsigned char)s[j])) ++j;
            if (j > i) fixed_.push_back(chord_index(s.substr(i, j - i)));
            i = j;
        }
    }
    reset();
}

void ProgressionMemory::reset() {
    seq_.clear();
    known = fixed_;
    lengths.assign(known.size(), P::BEATS_PER_BAR);
    locked = false;
    pos = -1;
}

void ProgressionMemory::push(int idx, long long beat, bool blip) {
    if (!seq_.empty() && (blip || beat - seq_.back().second < 1) && !locked) seq_.pop_back();   // blip, not progression
    if (!seq_.empty() && seq_.back().first == idx) return;
    seq_.push_back({idx, beat});
    if (seq_.size() > 256) seq_.erase(seq_.begin());   // only the tail is ever used
    if (!locked) try_lock();
}

void ProgressionMemory::correct(int idx) {
    if (seq_.empty()) return;
    seq_.back().first = idx;
    if (seq_.size() > 1 && seq_[seq_.size() - 2].first == idx) seq_.pop_back();   // same chord all along
}

void ProgressionMemory::try_lock() {
    std::vector<int> L;
    for (auto& s : seq_) L.push_back(s.first);
    int n = (int)L.size();
    // 1) re-lock onto a known cycle: last two chords are consecutive in it
    if (!known.empty() && n >= 2) {
        int m = (int)known.size();
        for (int i = 0; i < m; ++i)
            if (known[i] == L[n - 2] && known[(i + 1) % m] == L[n - 1]) {
                locked = true, pos = (i + 1) % m;
                return;
            }
    }
    // 2) decorated mode, first chord: align on it
    if (!fixed_.empty() && n == 1) {
        auto it = std::find(known.begin(), known.end(), L[0]);
        if (it != known.end()) {
            locked = true, pos = (int)(it - known.begin());
            return;
        }
    }
    // 3) learn: a lap of p chords came back (its first k chords repeat); smallest p wins
    for (int p = P::CYCLE_MIN; p <= P::CYCLE_MAX; ++p) {
        int k = std::max(P::CYCLE_MIN, (p + 1) / 2);
        if (n < p + k) continue;
        if (!std::equal(L.end() - k, L.end(), L.end() - k - p)) continue;
        std::vector<int> cyc(L.end() - k - p, L.end() - k);   // one complete (closed) lap
        std::vector<int> u = cyc;
        std::sort(u.begin(), u.end());
        if (std::unique(u.begin(), u.end()) - u.begin() < 2) continue;
        known = cyc;
        lengths.clear();
        const int half = P::BEATS_PER_BAR / 2;
        for (int i = n - k - p; i < n - k; ++i)
            lengths.push_back((int)std::max<long long>(half, pyround((seq_[i + 1].second - seq_[i].second) / (double)half) * half));
        locked = true, pos = k - 1;   // the current chord is cyc[k-1]
        return;
    }
}

std::optional<std::pair<int, long long>> ProgressionMemory::predict() const {
    if (!locked || seq_.empty()) return std::nullopt;
    int nxt = (pos + 1) % (int)known.size();
    return std::make_pair(known[nxt], seq_.back().second + lengths[pos]);
}

void ProgressionMemory::advance(long long beat) {
    pos = (pos + 1) % (int)known.size();
    seq_.push_back({known[pos], beat});
    if (seq_.size() > 256) seq_.erase(seq_.begin());
}

}  // namespace kordia
