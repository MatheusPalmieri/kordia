// 'When to change' layer.
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
    long long idx = round_even(ph);
    return std::abs(ph - idx) <= tol && bar_pos(idx) % 2 == 0;
}

void Grid::observe_change(double t) {
    if (!has_period()) return;
    double ph = beat_phase(t);
    long long idx = round_even(ph);
    if (std::abs(ph - idx) > 0.3) return;
    votes_.push_back(floor_mod(idx - bar_offset, P::BEATS_PER_BAR));
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
        bar_offset = floor_mod(bar_offset + best, P::BEATS_PER_BAR);
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
    next_index = round_even((next_beat - *first_onset) / p);
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
    T_ = templates();
    slots_.reserve(P::GRID_HISTORY + 1);
    labels_.reserve(P::GRID_HISTORY + 1);
    next_.reserve(P::GRID_MAX_SLOTS);
    cycle.reserve(P::GRID_MAX_SLOTS);
    known.reserve(P::GRID_MAX_SLOTS);
    lengths.reserve(P::GRID_MAX_SLOTS);
    reset();
}

void ProgressionMemory::reset() {
    seq_.clear();
    known = fixed_;
    lengths.assign(known.size(), P::BEATS_PER_BAR);
    locked = false;
    pos = -1;
    slots_.clear();
    labels_.clear();
    last_slot_.reset();
    cycle.clear();
    misses_ = 0;
    corrected = false;
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
        // durations of the newest closed lap, rotated to `cyc`: the song's first chord may be cut short
        // (a two-bar tonic that closes the cycle shows up as one bar when the song starts on it)
        for (int j = 0; j < p; ++j) {
            int i = n - 1 - p + (j - k + 1 + p) % p;
            lengths.push_back((int)std::max<long long>(half, round_even((seq_[i + 1].second - seq_[i].second) / (double)half) * half));
        }
        locked = true, pos = k - 1;   // the current chord is cyc[k-1]
        return;
    }
}

// ------------------------------------------------------------------ click grid
static bool same_chord(int a, int b) {   // the third is what one reading gets wrong; the sum of the laps decides it
    return a != N_IDX && b != N_IDX && a % 12 == b % 12;
}

int ProgressionMemory::read(const Chroma& c) const {
    bool any = false;
    for (double v : c) any = any || v != 0.0;
    if (!any) return N_IDX;
    int best = 0;
    double top = -INF;
    for (int i = 0; i < 24; ++i) {
        double d = 0;
        for (int k = 0; k < 12; ++k) d += T_[i][k] * c[k];
        if (d > top) top = d, best = i;
    }
    return best;
}

void ProgressionMemory::append(const Chroma& c) {
    int label = read(c);
    if (slots_.empty() && label == N_IDX) return;   // the song has not started: an empty lap counts against every cycle
    slots_.push_back(c);
    labels_.push_back(label);
    if ((int)slots_.size() > P::GRID_HISTORY) {
        slots_.erase(slots_.begin());
        labels_.erase(labels_.begin());
    }
}

int ProgressionMemory::push_slot(long long slot, const Chroma& chroma, int max_misses) {
    if (last_slot_)
        for (long long k = 0; k < slot - *last_slot_ - 1; ++k) append(Chroma{});
    double norm = 0;
    for (double v : chroma) norm += v * v;
    norm = std::sqrt(norm);
    Chroma unit = chroma;
    if (norm > 0)
        for (double& v : unit) v /= norm;
    append(unit);
    last_slot_ = slot;
    corrected = false;
    int heard = labels_.empty() ? N_IDX : labels_.back();
    if (locked) {
        bool miss = heard != N_IDX && !same_chord(heard, cycle[(size_t)floor_mod(slot, (long long)cycle.size())]);
        misses_ = miss ? misses_ + 1 : 0;
        if (misses_ >= max_misses) unlock();
        else adopt((int)cycle.size());
    } else {
        int size = find_cycle();
        if (size && adopt(size)) {
            locked = true;
            misses_ = 0;
        }
    }
    return heard;
}

int ProgressionMemory::find_cycle() const {
    int n = (int)labels_.size();
    for (int size = P::BEATS_PER_BAR / P::SLOT_BEATS; size <= P::GRID_MAX_SLOTS; size += P::BEATS_PER_BAR / P::SLOT_BEATS) {
        int tail = std::min(size, n - size);
        if (tail < round_even((P::GRID_LOCK_LAPS - 1) * size)) continue;
        int hits = 0;
        bool recent = true;
        for (int i = 0; i < tail; ++i) {
            bool same = same_chord(labels_[n - tail + i], labels_[n - tail - size + i]);
            hits += same;
            if (i >= tail - P::GRID_TAIL) recent = recent && same;
        }
        bool two_roots = false;
        int root = -1;
        for (int i = n - size; i < n; ++i) {
            if (labels_[i] == N_IDX) continue;
            if (root >= 0 && labels_[i] % 12 != root) two_roots = true;
            root = labels_[i] % 12;
        }
        if (hits >= P::GRID_MATCH * tail && recent && two_roots) return size;
    }
    return 0;
}

bool ProgressionMemory::adopt(int size) {
    std::array<Chroma, P::GRID_MAX_SLOTS> votes{};
    int n = (int)slots_.size();
    for (int back = 0; back < std::min(P::GRID_LAPS * size, n); ++back) {
        auto& v = votes[(size_t)floor_mod(*last_slot_ - back, size)];
        const Chroma& c = slots_[(size_t)(n - 1 - back)];
        for (int k = 0; k < 12; ++k) v[k] += c[k];
    }
    next_.clear();
    for (int i = 0; i < size; ++i) next_.push_back(read(votes[(size_t)i]));
    for (int i = 0; i < size; ++i)
        if (next_[i] == N_IDX) next_[i] = next_[(i + size - 1) % size];   // never played in these laps: the previous goes on
    int first = -1;
    bool has_n = false;
    for (int i = 0; i < size; ++i) {
        has_n = has_n || next_[i] == N_IDX;
        if (first < 0 && next_[i] != next_[(i + size - 1) % size]) first = i;
    }
    if (first < 0 || has_n) return false;
    corrected = !cycle.empty() && cycle != next_;
    long long now = *last_slot_ + 1;
    int offset = (int)floor_mod(now - first, size), start = 0;
    known.clear();
    lengths.clear();
    for (int k = 0; k < size; ++k) {
        int c = next_[(first + k) % size];
        if (known.empty() || known.back() != c) {
            known.push_back(c);
            lengths.push_back(0);
            if (k <= offset) pos = (int)known.size() - 1, start = k;
        }
        lengths.back() += P::SLOT_BEATS;
    }
    cycle = next_;
    seq_.clear();
    seq_.push_back({known[pos], (now - offset + start) * P::SLOT_BEATS});
    return true;
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
