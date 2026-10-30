// Tests: each module in isolation, regression on the scenarios (thresholds start loose; tighten them as the system
// improves and say why in the commit), the exam, and the reference values that keep the lab measuring the same songs.
// Usage: kordia_tests [part of a test name]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "../app/AudioCheck.h"
#include "../app/SessionReport.h"
#include "lab.h"

using namespace lab;

// Every allocation of this thread: the audio path must not make any.
static thread_local long long allocations = 0;
void* operator new(std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ------------------------------------------------------------------ harness
struct Test { const char* name; void (*fn)(); };
static std::vector<Test>& registry() { static std::vector<Test> t; return t; }
struct Reg { Reg(const char* n, void (*f)()) { registry().push_back({n, f}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) throw std::runtime_error(std::string(__FILE__ ":") + std::to_string(__LINE__) + ": " #cond); } while (0)
#define NEAR(a, b, tol) CHECK(std::abs((a) - (b)) <= (tol))

namespace kordia {
struct EngineProbe {   // the safety fallback, without going through a whole song
    static void hear(Engine& e, const std::vector<std::string>& chords) { for (const auto& c : chords) e.heard_.push_back(chord_index(c)); }
    static std::optional<Output> fallback(Engine& e, double t) { return e.tonic_out(t, "teste"); }
};
}  // namespace kordia

static std::vector<double> to_double(const std::vector<float>& x) { return {x.begin(), x.end()}; }
static std::vector<int> indices(const std::vector<std::string>& chords) {
    std::vector<int> v;
    for (const auto& c : chords) v.push_back(chord_index(c));
    return v;
}

// (t, onset magnitudes, chroma magnitudes, dB) the way the processor computes them
template <typename F>
static void frames(const std::vector<double>& audio, F f) {
    Spectrum so(P::ONSET_WIN), sc(P::CHROMA_WIN);
    std::vector<double> buf(P::CHROMA_WIN, 0.0);
    for (size_t k = 0; k < audio.size() / P::HOP; ++k) {
        const double* x = audio.data() + k * P::HOP;
        std::copy(buf.begin() + P::HOP, buf.end(), buf.begin());
        std::copy(x, x + P::HOP, buf.end() - P::HOP);
        double ms = 0;
        for (int i = 0; i < P::HOP; ++i) ms += x[i] * x[i];
        f((double)((k + 1) * P::HOP) / SR, so(buf.data() + P::CHROMA_WIN - P::ONSET_WIN), sc(buf.data()), 10 * std::log10(ms / P::HOP + 1e-12));
    }
}

// the processor one block at a time, as the live app calls it
static void process(Processor& proc, const double* x, double lookahead = 0.0) {
    double L[P::HOP], R[P::HOP];
    proc.render(proc.time() + lookahead, P::HOP, L, R);
    proc.analyze(x);
}

static std::vector<Event> clicks(Processor& proc) {
    std::vector<Event> out;
    for (const auto& e : proc.acc().events)
        if (e.inst == Inst::Click) out.push_back(e);
    return out;
}

static LabConfig cfg_c() { LabConfig c; c.tom = "C"; return c; }

// ------------------------------------------------------------------ reference: the lab measures the same songs
// Taken from NumPy 2.5 / SciPy 1.18, the libraries that generated the audio of every version in docs/resultados.md.
TEST(rng_matches_numpy) {
    Rng r(310);
    double last = 0;
    for (int i = 0; i < 2000; ++i) last = r.normal();
    CHECK(last == 0.92144685663972914);
    CHECK(r.uniform(-1, 1) == -0.48794897839323315);
    for (int i = 0; i < 4; ++i) r.uniform(-1, 1);
    for (int want : {4, 4, 3, 3, 4, 3, 4, 3, 4}) CHECK(r.integers(3, 5) == want);
    for (int want : {3, 1, 0, 5, 5}) CHECK(r.integers(7) == want);
    CHECK(r.uniform(0.7, 1.3) == 0.83322956098168932);
    CHECK(r.integers(4) == 3);                          // rng.choice over 4 values
    CHECK(r.normal(0, 0.01) == 0.002108567272486745);
}

TEST(rng_sum_of_a_million_normals_matches_numpy) {
    Rng r(0);
    std::vector<double> x(1000000);
    for (double& v : x) v = r.normal();
    CHECK(np_sum(x.data(), x.size()) == 998.57064943862133);
}

TEST(resample_matches_scipy) {
    std::vector<double> x(5000);
    for (int i = 0; i < 5000; ++i) x[(size_t)i] = std::sin(i * .01) + i * 1e-4;
    auto y = resample_poly(x, 160, 147);
    CHECK(y.size() == 5443);
    NEAR(y[777], 0.82646564302862868, 1e-14);
    NEAR(y.back(), 0.030509417239310839, 1e-14);
    auto z = resample_poly(x, 1000, 1059);
    CHECK(z.size() == 4722);
    NEAR(z[777], 1.012739241340479, 1e-14);
}

TEST(pluck_matches_scipy) {
    Rng r(3);
    auto y = pluck(52, 1.0, 1.0, 1.0, r);
    CHECK(y[0] == -0.85502888494973472 && y[5000] == -0.017896838136454929 && y.back() == -0.0069656934406415625);
}

TEST(scenario_audio_is_unchanged) {
    // FNV-1a of each scenario's samples: bit identical to the audio every version was measured on. If this fails the
    // synthetic songs changed, and no number can be compared with docs/resultados.md any more.
    static const std::map<std::string, uint64_t> expected = {
#include "scenario_hashes.inc"
    };
    std::vector<std::string> wrong;
    std::mutex lock;
    parallel_for(scenarios().size(), [&](size_t i) {
        const Scenario& s = scenarios()[i];
        Song song = build_scenario(s.name);
        uint64_t h = 0xcbf29ce484222325ULL;
        const auto* p = (const unsigned char*)song.audio.data();
        for (size_t k = 0; k < song.audio.size() * sizeof(float); ++k) h = (h ^ p[k]) * 0x100000001b3ULL;
        if (expected.at(s.name) != h) {
            std::lock_guard<std::mutex> guard(lock);
            wrong.push_back(s.name + " " + std::to_string(h));
        }
    });
    for (const auto& w : wrong) std::cout << "  " << w << "\n";
    CHECK(wrong.empty());
}

// ------------------------------------------------------------------ modules
TEST(pluck_tuning) {
    for (int midi : {45, 52, 64}) {
        Rng r(0);
        auto y = pluck(midi, 1.0, 1.0, 1.0, r);
        double best = -INF;
        int lag = 0;
        for (int l = 20; l < 400; ++l) {
            double ac = 0;
            for (int i = 2000; i + l < 6000; ++i) ac += y[(size_t)i] * y[(size_t)(i + l)];
            if (ac > best) best = ac, lag = l;
        }
        CHECK(std::abs(lag - SR / (440 * std::pow(2.0, (midi - 69) / 12.0))) <= 1.0);
    }
}

TEST(scenario_truth_is_coherent) {
    Song s = build_scenario("pausa_e_fim");
    const auto& segs = s.truth.chords;
    CHECK(segs[0].s == 0 && std::abs(segs.back().e - s.truth.duration) < 1e-6);
    for (size_t i = 0; i + 1 < segs.size(); ++i) CHECK(std::abs(segs[i].e - segs[i + 1].s) < 1e-6);
    CHECK(std::any_of(segs.begin(), segs.end(), [](const Seg& x) { return x.lab == PAUSE; }));   // the short pause
    CHECK(s.truth.ends.size() == 2);                                                             // long silence + final end
    CHECK(s.audio.size() == (size_t)(s.truth.duration * SR));
}

TEST(truth_file_round_trip) {
    Song s = build_scenario("acorde_fora_do_tom");
    Truth back = truth_from_json(Json::parse(truth_to_json(s.truth)));
    CHECK(back.duration == s.truth.duration && back.chords.size() == s.truth.chords.size() && back.changes.size() == s.truth.changes.size());
    CHECK(back.changes[1].from == s.truth.changes[1].from && back.chords.back().lab == N_IDX);
    CHECK(back.run.modo == "decorado" && *back.run.progressao == "C G Am F");
    int surprises = 0;
    for (const auto& c : back.changes) surprises += c.surprise;
    CHECK(surprises >= 3);
}

TEST(spectrum_matches_a_plain_dft) {
    const int n = 64;
    Spectrum spectrum(n);
    std::vector<double> x(n);
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        x[i] = std::sin(0.7 * i) + 0.3 * std::cos(2.1 * i + 1) + 0.1 * (i % 5);
        sum += 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (n - 1));
    }
    const auto& mag = spectrum(x.data());
    for (int k = 0; k <= n / 2; ++k) {
        std::complex<double> X = 0;
        for (int i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (n - 1));
            X += std::polar(x[i] * w, -2 * 3.14159265358979323846 * k * i / n);
        }
        NEAR(mag[k], std::abs(X) * 2 / sum, 1e-12);
    }
}

TEST(ring_keeps_the_newest_in_order) {
    Ring<int, 3> r;
    for (int i = 0; i < 5; ++i) r.push_back(i);
    CHECK(r.size() == 3 && r.front() == 2 && r[2] == 4);
    r.pop_front();
    int sum = 0;
    for (int v : r) sum += v;
    CHECK(sum == 7);
    r.clear();
    CHECK(r.empty());
}

// One song through the live path, as LiveEngine runs it: after the Processor exists, nothing may be allocated.
static long long allocations_in(const std::string& scenario, LabConfig cfg, double out_sr) {
    Song song = build_scenario(scenario);
    std::vector<double> audio = to_double(song.audio);
    const double lookahead = 0.02;
    Processor proc(cfg, lookahead, SR, out_sr);
    proc.synth().layers = {1, 1, 1, 1, 1};
    std::vector<double> L(8192), R(8192);
    const long long before = allocations;
    for (size_t k = 0; k + 1 < audio.size() / P::HOP; ++k) {
        proc.render(proc.time() + lookahead, 480, L.data(), R.data());
        proc.analyze(audio.data() + k * P::HOP);
        proc.trim_history();
        proc.next_chord(), proc.beat_in_bar(), proc.count_left(), proc.engine().current_key();   // what publish() reads
        int recent[16];
        proc.memory().progress(), proc.memory().recent_chords(recent, 16);
    }
    return allocations - before;
}

TEST(audio_path_does_not_allocate) {
    LabConfig free_play;
    free_play.manter_padrao = free_play.aprender_na_tonica = true;
    LabConfig click = free_play;
    click.modo = "metronomo", click.bpm = 120.0, click.clique = click.contagem = true;
    LabConfig known;
    known.modo = "decorado", known.progressao = "C G Am F";
    CHECK(allocations_in("estavel_120", free_play, 48000.0) == 0);
    CHECK(allocations_in("estavel_120", LabConfig{}, 44100.0) == 0);     // chords change by ear, reasons with text
    CHECK(allocations_in("pausa_e_fim", free_play, 48000.0) == 0);       // stops and forgets
    CHECK(allocations_in("troca_de_tom", LabConfig{}, 48000.0) == 0);    // unlocks and relearns
    CHECK(allocations_in("estavel_120", click, 48000.0) == 0);           // click grid
    CHECK(allocations_in("estavel_120", known, 48000.0) == 0);
}

TEST(layer_change_does_not_click) {
    Synth synth(44100.0);
    synth.handle({0.0, EvType::On, Inst::Pad, 60, 100});
    const int n = 480;
    std::vector<double> L((size_t)n), R((size_t)n);
    auto jump = [&](double t0) {
        double last = L[(size_t)n - 1], worst = 0;
        synth.render(t0, n, L.data(), R.data());
        for (int i = 0; i < n; ++i) worst = std::max(worst, std::abs(L[(size_t)i] - last)), last = L[(size_t)i];
        return worst;
    };
    double t = 0;
    for (; t < 2.0; t += n / 44100.0) jump(t);
    synth.layers[1] = 1.0;   // organ on at full level, with the note already sounding
    double during = jump(t);
    t += n / 44100.0;
    CHECK(during <= 1.5 * jump(t));   // no worse than the organ's own slope, a block later
}

TEST(onsets_count_and_timing) {
    Guitar g(5.0, 1);
    std::vector<double> times;
    for (int i = 0; i < 8; ++i) times.push_back(0.5 + 0.5 * i);
    for (int i = 0; i < 8; ++i) g.strum(times[(size_t)i], voicing("G"), i % 2 == 0 ? 'D' : 'U', 0.8);
    OnsetDetector od(SR);
    std::vector<double> found;
    frames(g.out, [&](double t, const std::vector<double>& mo, const std::vector<double>&, double db) {
        if (auto o = od.process(mo, t - P::ONSET_WIN / 2.0 / SR, db)) found.push_back(o->t);
    });
    CHECK(found.size() == times.size());
    for (size_t i = 0; i < found.size(); ++i) CHECK(std::abs(found[i] - times[i]) < 0.05);
}

TEST(chord_classifier_recognizes_triads) {
    for (const char* name : {"C", "G", "Am", "F", "Em", "E", "Bb", "Dm", "D", "Bm"}) {
        Guitar g(1.5, 2);
        g.strum(0.1, voicing(name), 'D', 1.0);
        ChordTracker ct(SR, std::nullopt);
        int idx = N_IDX;
        std::vector<double> head(g.out.begin(), g.out.begin() + SR);
        frames(head, [&](double, const std::vector<double>&, const std::vector<double>& mc, double db) {
            idx = ct.process(mc, db < P::SILENCE_DB, P::HMM_P_CHANGE_ONSET).idx;
        });
        CHECK(label(idx) == name);
    }
}

TEST(key_prior_never_blocks) {
    auto w = key_prior(0);
    CHECK(w[(size_t)chord_index("E")] > 0 && w[(size_t)chord_index("C")] > w[(size_t)chord_index("E")]);
}

// Eighth-note 'strums' (louder on the beat) with a tempo per beat.
static void feed_clicks(Grid& tracker, const std::vector<double>& bpms) {
    double t = 0.5;
    std::vector<std::pair<double, double>> events;
    for (double bpm : bpms) {
        double beat = 60 / bpm;
        events.push_back({t, 1.0}), events.push_back({t + beat / 2, 0.5});
        t += beat;
    }
    events.push_back({1e9, 0.0});
    double frame = (double)P::HOP / SR;
    size_t next = 0;
    for (int k = 0; k < (int)(t / frame); ++k) {
        double now = (k + 1) * frame, flux = 0.0;
        std::optional<Onset> onset;
        if (events[next].first <= now) {
            onset = Onset{events[next].first, -20, events[next].second * 5};
            flux = events[next].second * 5;
            ++next;
        }
        tracker.update(now, flux, onset);
    }
}

TEST(tempo_constant_and_eighths) {
    TempoTracker tr((double)P::HOP / SR);
    feed_clicks(tr, std::vector<double>(32, 100.0));
    CHECK(std::abs(*tr.bpm() - 100) < 2 && tr.confidence() > 0.8);
}

TEST(tempo_follows_ramp) {
    TempoTracker tr((double)P::HOP / SR);
    feed_clicks(tr, linspace(100, 125, 64));
    CHECK(std::abs(*tr.bpm() - 125) < 125 * 0.05);
}

TEST(fixed_grid) {
    FixedGrid g(120, std::nullopt);
    g.update(1.0, 1.0, Onset{1.0, -20, 1});
    CHECK(std::abs(g.time_of_beat(4) - 3.0) < 1e-9 && g.bar_pos(4) == 0);
}

using Predicted = std::pair<int, long long>;

TEST(memory_learns_cycle_and_predicts) {
    ProgressionMemory m(std::nullopt);
    auto prog = indices({"C", "G", "Am", "F"});
    for (int i = 0; i < 5; ++i) m.push(prog[(size_t)(i % 4)], 4 * i);
    CHECK(!m.locked);            // C G Am F C: one chord of the second lap is not enough
    m.push(prog[1], 20);         // ... G: the lap came back
    CHECK(m.locked && m.predict() == Predicted(prog[2], 24));
    m.unlock();
    m.push(prog[2], 24);         // G -> Am are consecutive in the known cycle: re-lock
    CHECK(m.locked && m.predict() == Predicted(prog[3], 28));
}

TEST(memory_decorated_aligns_on_first_chord) {
    ProgressionMemory m(std::string("C G Am F"));
    m.push(chord_index("Am"), 0);
    CHECK(m.locked && m.predict() == Predicted(chord_index("F"), 4));
}

TEST(cycle_durations_come_from_the_newest_lap) {
    // C | Bb | F | C: the tonic lasts 2 bars, but the song's first one is cut to 1 (found by the exam: 0/24)
    ProgressionMemory m(std::nullopt);
    int c = chord_index("C"), bb = chord_index("Bb"), f = chord_index("F");
    for (auto [chord, beat] : std::vector<std::pair<int, int>>{{c, 8}, {bb, 12}, {f, 16}, {c, 20}, {bb, 28}}) m.push(chord, beat);
    CHECK(m.locked && m.known == std::vector<int>({c, bb, f}) && m.lengths == std::vector<int>({8, 4, 4}));
    CHECK(m.predict() == Predicted(f, 32));
}

// Half bars closing with a clean reading of each chord; "" reads as nothing was played.
static long long slots(ProgressionMemory& mem, const std::vector<std::string>& chords, long long start = 0) {
    for (size_t i = 0; i < chords.size(); ++i) {
        Chroma chroma{};
        if (!chords[i].empty())
            for (int pc : chord_pcs(chord_index(chords[i]))) chroma[(size_t)pc] = 1.0;
        mem.push_slot(start + (long long)i, chroma, 3);
    }
    return start + (long long)chords.size();
}

static std::vector<std::string> lap_of(const std::vector<std::string>& chords) {   // one chord per bar = two half bars
    std::vector<std::string> v;
    for (const auto& c : chords) v.push_back(c), v.push_back(c);
    return v;
}

static std::vector<std::string> join(std::initializer_list<std::vector<std::string>> parts) {
    std::vector<std::string> v;
    for (const auto& p : parts) v.insert(v.end(), p.begin(), p.end());
    return v;
}

static std::vector<std::string> cut(const std::vector<std::string>& v, size_t a, size_t b) {
    return {v.begin() + (std::ptrdiff_t)a, v.begin() + (std::ptrdiff_t)std::min(b, v.size())};
}

TEST(grid_keeps_a_chord_that_was_misread_while_learning) {
    // Session 15:53: B was heard late and left out, F# C# Ebm stayed forever. On the grid it stays in the cycle.
    ProgressionMemory m(std::nullopt);
    auto lap = lap_of({"F#", "C#", "Ebm", "B"});
    auto first = join({cut(lap, 0, 6), {"B", "F#"}});     // second half of the B bar read as F#
    long long n = slots(m, join({first, lap, cut(lap, 0, 4)}));   // (the newest slots must agree before it locks)
    CHECK(m.locked && m.cycle.size() == 8);
    slots(m, join({cut(lap, 4, 8), lap, lap}), n);
    CHECK(m.known == indices({"F#", "C#", "Ebm", "B"}));
}

TEST(grid_changes_land_on_the_downbeat) {
    // Session 15:53: the change was dated when confidence came, 2 beats late. The grid dates it by the bar.
    ProgressionMemory m(std::nullopt);
    auto lap = lap_of({"C", "G", "Am", "F"});
    long long n = slots(m, join({lap, lap}));
    CHECK(m.locked && m.predict() == Predicted(chord_index("G"), (n + 2) * P::SLOT_BEATS));
}

TEST(grid_does_not_take_a_long_chord_for_a_cycle) {
    ProgressionMemory m(std::nullopt);
    slots(m, lap_of({"F#", "Abm", "Abm", "Abm"}));   // Abm held for 3 bars: was a "1 bar cycle" (session 22:50)
    CHECK(!m.locked);
}

TEST(grid_learns_the_long_cycle_after_a_false_lap) {
    // C G C G Am F: the two first bars repeat, then the lap goes on
    ProgressionMemory m(std::nullopt);
    auto lap = lap_of({"C", "G", "C", "G", "Am", "F"});
    long long n = slots(m, cut(lap, 0, 8));
    CHECK(m.locked && m.cycle.size() == 4);          // false lap: nothing better to go on yet
    slots(m, join({cut(lap, 8, lap.size()), lap}), n);
    CHECK(m.locked && m.cycle.size() == 12);
}

TEST(grid_silence_before_the_song_does_not_count) {
    ProgressionMemory m(std::nullopt);
    auto lap = lap_of({"C", "F"});
    slots(m, join({{"", "", ""}, lap, lap}));
    CHECK(m.locked);
}

TEST(grid_progress_shows_what_is_missing_to_lock) {
    // Real session of 30/10, 18:40: the second half of bar 2 misheard, then the second lap comes right.
    ProgressionMemory m(std::nullopt);
    auto lap = lap_of({"F#", "C#", "Ebm", "B"});
    auto first = lap;
    first[3] = "F#m";
    long long n = slots(m, join({first, cut(lap, 0, 6)}));
    auto p = m.progress();
    CHECK(!m.locked && p.size == 8 && p.tail == 6 && p.hits == 5 && p.roots == 4 && !p.recent);   // the misheard slot is still in the last 3
    CHECK(m.slot_labels().size() == 14 && m.slots_heard == 14 && m.slot_labels()[3] == chord_index("F#m"));
    CHECK(m.slot_scores()[0] > 0.5 && m.slot_scores()[0] <= 1.0001);
    slots(m, cut(lap, 6, 8), n);
    CHECK(m.locked);

    ProgressionMemory empty(std::nullopt);
    CHECK(empty.progress().size == 0);
    slots(empty, lap_of({"C"}));
    CHECK(empty.progress().size == 0);   // one chord is no lap yet
}

TEST(free_play_lists_the_chords_in_order) {
    ProgressionMemory m(std::nullopt);
    for (int i = 0; i < 20; ++i) m.push(i % 2 ? chord_index("G") : chord_index("C"), 4 * i);
    int out[4];
    CHECK(m.recent_chords(out, 4) == 4 && out[2] == chord_index("C") && out[3] == chord_index("G"));
}

TEST(silence_and_noise_play_nothing) {
    Rng rng(0);
    std::vector<double> noise((size_t)SR * 4);
    for (double& v : noise) v = rng.normal() * 1e-3;
    Result res = run(noise, cfg_c());
    for (const auto& e : res.events) CHECK(e.type == EvType::Expr);
}

TEST(pipeline_is_causal) {
    // changing the future must not change any output before it
    Song s = build_scenario("estavel_120");
    std::vector<double> audio(s.audio.begin(), s.audio.begin() + 14 * SR), other = audio;
    size_t cut_at = 8 * SR;
    Rng rng(1);
    for (size_t i = cut_at; i < other.size(); ++i) other[i] = rng.normal() * 0.1;
    Result a = run(audio, cfg_c()), b = run(other, cfg_c());
    double tc = (double)(cut_at / P::HOP * P::HOP) / SR;   // last block boundary before the change
    auto before = [&](const Result& r) {
        std::vector<Event> v;
        for (const auto& e : r.events)
            if (e.t < tc) v.push_back(e);
        return v;
    };
    auto ea = before(a), eb = before(b);
    CHECK(ea.size() == eb.size() && !ea.empty());
    for (size_t i = 0; i < ea.size(); ++i) CHECK(ea[i].t == eb[i].t && ea[i].pitch == eb[i].pitch && ea[i].vel == eb[i].vel && ea[i].type == eb[i].type);
    size_t n = (size_t)(tc * SR) * 2;
    CHECK(std::equal(a.accomp.begin(), a.accomp.begin() + (std::ptrdiff_t)n, b.accomp.begin()));
}

TEST(midi_file_is_valid) {
    std::vector<Event> ev = {{0.0, EvType::On, Inst::Bass, 36, 100}, {1.0, EvType::Off, Inst::Bass, 36, 0}, {0.5, EvType::Expr, Inst::All, 0, 0, 0.5}};
    fs::path p = fs::temp_directory_path() / "kordia_test.mid";
    write_midi(ev, p);
    std::ifstream f(p, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    fs::remove(p);
    CHECK(d.substr(0, 4) == "MThd" && d.substr(14, 4) == "MTrk");
    uint32_t len = 0;
    for (int i = 18; i < 22; ++i) len = len << 8 | (unsigned char)d[(size_t)i];
    CHECK(len == d.size() - 22 && d.substr(d.size() - 3) == std::string("\xFF\x2F\x00", 3));
}

TEST(report_is_written_as_svg) {
    Song s = build_scenario("estavel_120");
    Result res = run(to_double(s.audio), s.truth.run);
    fs::path p = fs::temp_directory_path() / "kordia_test.svg";
    plot(res, p, &s.truth, "teste <1>");
    std::ifstream f(p);
    std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    fs::remove(p);
    CHECK(d.rfind("<svg", 0) == 0 && d.find("</svg>") != std::string::npos && d.find("teste &lt;1>") != std::string::npos);
    CHECK(d.find(">Am<") != std::string::npos && d.find("nan") == std::string::npos);
}

TEST(lookahead_emits_predicted_changes_early) {
    // predicted changes must be decided `lookahead` before they are due, so they sound on time
    Song s = build_scenario("estavel_120");
    auto audio = to_double(s.audio);
    constexpr double L = 0.12;
    Processor proc(cfg_c(), L);
    std::vector<double> early;
    for (size_t k = 0; k < audio.size() / P::HOP; ++k) {
        size_t before = proc.acc().changes.size();
        process(proc, audio.data() + k * P::HOP, L);
        const auto& ch = proc.acc().changes;
        if (ch.size() > before && proc.engine().state == State::Travado && ch.back().mode == Mode::Full) early.push_back(ch.back().t - proc.time());
    }
    CHECK(early.size() >= 4 && median(early) >= L - 2.0 * P::HOP / SR);
}

TEST(click_follows_tempo_and_fixed_grid_starts_at_start) {
    // free mode clicks on the tracked beats once the tempo is known; with a fixed BPM + click the grid starts at Iniciar
    Song s = build_scenario("estavel_120");
    auto audio = to_double(s.audio);
    LabConfig free_click = cfg_c();
    free_click.clique = true;
    Processor proc(free_click);
    for (int k = 0; k < 10 * SR / P::HOP; ++k) process(proc, audio.data() + (size_t)k * P::HOP);
    auto found = clicks(proc);
    CHECK(found.size() >= 8);
    std::vector<double> gaps;
    for (size_t i = 1; i < found.size(); ++i) gaps.push_back(found[i].t - found[i - 1].t);
    CHECK(std::abs(median(gaps) - 0.5) < 0.02);   // 120 BPM

    LabConfig fixed;
    fixed.modo = "metronomo", fixed.bpm = 100, fixed.clique = true;
    Processor grid(fixed);
    std::vector<double> silence(P::HOP, 0.0);
    for (int k = 0; k < 2 * SR / P::HOP; ++k) process(grid, silence.data());   // silence: still clicks
    auto on_grid = clicks(grid);
    CHECK(std::abs(on_grid[0].t - 0.5) < 1e-6 && on_grid[0].pitch == 77 && on_grid.size() == 3);
}

TEST(click_before_playing_in_follow_mode) {
    LabConfig cfg = cfg_c();
    cfg.clique = true, cfg.bpm_inicial = 100;
    Processor proc(cfg);
    std::vector<double> silence(P::HOP, 0.0);
    for (int k = 0; k < (int)(2.5 * SR) / P::HOP; ++k) process(proc, silence.data());   // nobody playing: the click already runs
    auto found = clicks(proc);
    CHECK(std::abs(found[0].t - 0.5) < 1e-6);
    for (size_t i = 1; i < found.size(); ++i) CHECK(std::abs(found[i].t - found[i - 1].t - 0.6) < 1e-6);
}

TEST(session_folder_loads) {
    fs::path dir = fs::temp_directory_path() / "kordia_test_session";
    fs::create_directories(dir);
    write_text(dir / "config.json", "{\"run\": {\"modo\": \"livre\", \"tom\": \"C\", \"progressao\": null}, \"lookahead\": 0.1}");
    write_wav(dir / "entrada.wav", std::vector<double>(P::HOP, 0.1), 1, SR);
    Session s = load_session(dir);
    fs::remove_all(dir);
    CHECK(s.audio.size() == P::HOP && std::abs(s.audio[0] - 0.1) < 1e-3 && *s.cfg.tom == "C" && !s.cfg.progressao && s.lookahead == 0.1 && s.sr == SR);
}

TEST(json_reads_what_the_app_writes) {
    Json j = Json::parse("{\"a\": 1.5e2, \"b\": [true, null, \"x\\n\\u00e7\"], \"c\": {\"d\": -2}, \"e\": NaN}");
    CHECK(j.get("a")->n == 150 && j.get("b")->a[0].b && j.get("b")->a[1].kind == Json::Null && j.get("b")->a[2].s == "x\n\xC3\xA7");
    CHECK(j.get("c")->get("d")->n == -2 && std::isnan(j.get("e")->n) && !j.get("nada"));
}

TEST(fallback_uses_the_key_that_fits_the_music) {
    // key set to C but the music is in B: the safety tonic is B (detected), never C; too little heard: silence
    TempoTracker tr((double)P::HOP / SR);
    ProgressionMemory mem(std::nullopt);
    Engine b(tr, mem, 0, false, 0.0);
    EngineProbe::hear(b, {"B", "F#", "G#m", "E"});
    CHECK(EngineProbe::fallback(b, 1.0)->chord == note_pc("B"));
    Engine few(tr, mem, 0, false, 0.0);
    EngineProbe::hear(few, {"B", "F#"});     // not C, and not enough to tell
    auto out = EngineProbe::fallback(few, 1.0);
    CHECK((!out || out->mode == Mode::Silence) && few.cur.first == -1);
    Engine c(tr, mem, 0, false, 0.0);
    EngineProbe::hear(c, {"C", "G", "Am", "F"});
    CHECK(EngineProbe::fallback(c, 1.0)->mode == Mode::Tonic && c.cur.first == 0);
}

TEST(count_in) {
    // Iniciar: 4 clicks, one bar; the first is accented; nothing is played or learned before the first downbeat
    Rng rng(0);
    LabConfig cfg;
    cfg.modo = "metronomo", cfg.bpm = 120, cfg.contagem = true, cfg.aprender_na_tonica = true;
    Processor proc(cfg);
    std::set<int> left;
    std::vector<double> x(P::HOP);
    for (int k = 0; k < (int)(2.55 * SR) / P::HOP; ++k) {
        for (double& v : x) v = rng.normal() * 0.05;   // playing over the count-in
        process(proc, x.data());
        left.insert(proc.count_left());
    }
    auto found = clicks(proc);
    CHECK(found.size() == 4);                          // no click after: the Clique button is off
    for (size_t i = 0; i < 4; ++i) NEAR(found[i].t, 0.5 + 0.5 * (double)i, 1e-3);
    CHECK(left == std::set<int>({0, 1, 2, 3, 4}));
    CHECK(proc.engine().state == State::Ocioso && proc.acc().changes.empty() && !proc.memory().has_slot());
}

TEST(detect_key_from_chords) {
    auto keys = [](const std::vector<std::string>& cs) {
        auto ix = indices(cs);
        Heard heard;
        for (int i : ix) heard.push_back(i);
        return detect_key(heard);
    };
    CHECK(keys({"B", "F#", "G#m", "E"}) == note_pc("B") && keys({"C", "G", "Am", "F"}) == 0 && !keys({"C", "G"}));
}

TEST(logo_assets) {
    auto bytes = [](const char* name) {
        std::ifstream f(fs::path("assets") / name, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    std::string ico = bytes("kordia.ico"), png = bytes("kordia-1024.png");
    std::set<int> sizes;
    for (int i = 0; i < (unsigned char)ico[4]; ++i) {
        int w = (unsigned char)ico[6 + 16 * (size_t)i];
        sizes.insert(w ? w : 256);
    }
    CHECK(sizes.count(16) && sizes.count(32) && sizes.count(48) && sizes.count(256));
    CHECK(png.substr(1, 3) == "PNG" && (unsigned char)png[18] == 4 && (unsigned char)png[22] == 4);   // 1024 x 1024
}

// ------------------------------------------------------------------ scenarios
static const std::map<std::string, Metrics>& rows() {
    static std::once_flag once;
    static std::map<std::string, Metrics> all;
    std::call_once(once, [] {
        std::vector<std::string> names;
        for (const auto& s : scenarios()) names.push_back(s.name);
        for (auto& [name, m] : evaluate_all(names, "test", false)) all[name] = std::move(m);
    });
    return all;
}
static double m(const char* scenario, const char* key) { return metric(rows().at(scenario), key); }

TEST(never_plays_when_it_should_not) {
    for (const auto& [name, r] : rows()) {
        CHECK(metric(r, "falso_disparo_s") <= 2.0);
        CHECK(metric(r, "parada_s") <= 3.0);          // ~1 bar + 1 s at the slowest scenario tempo
        CHECK(metric(r, "acc_total") >= 0.5);
        // known problems (docs/resultados.md): #1 feedback (the long pad tail + reverb comes back in the input),
        // #3 tempo lost in a strong ritardando
        CHECK(metric(r, "errado_confiante_n") <= (name == "desacelerando_final" || name == "realimentacao" ? 4 : 3));
    }
}

TEST(stable) {
    CHECK(m("estavel_120", "acc_total") >= 0.85 && m("estavel_120", "falso_disparo_s") <= 1.0 && m("estavel_120", "bpm_erro_medio") <= 5);
    CHECK(m("estavel_120", "parou_no_compasso") != 0);
}

TEST(prediction_beats_reaction) { CHECK(m("estavel_120", "lat_travado_ms") < m("estavel_120", "lat_reativo_ms") / 2); }
TEST(metronome_aligns_to_grid) { CHECK(std::abs(m("modo_metronomo", "lat_media_pulso")) <= 0.5); }
TEST(forgets_after_long_silence) { CHECK(m("pausa_e_fim", "esqueceu") != 0 && m("troca_de_tom", "esqueceu") != 0); }
TEST(follows_dynamics) { CHECK(m("dinamica", "corr_dinamica") >= 0.7); }
TEST(accelerating) { CHECK(m("acelerando", "bpm_erro_medio") <= 5 && m("acelerando", "acc_total") >= 0.85); }

TEST(wrong_key_setting_still_follows_the_audio) {
    // key set to C, music in B: the audio wins (prior only) and the fallback never plays C over it
    CHECK(m("tom_errado", "acc_total") >= 0.75 && m("tom_errado", "errado_confiante_n") <= 3);
}

TEST(held_pattern_survives_mistakes) {
    // hold mode: wrong chord, late change and late entry after the lock do not break the pattern
    CHECK(m("erros_com_padrao", "acc_total") >= 0.85 && m("erros_com_padrao", "falso_disparo_s") <= 1.0);
}

TEST(hold_is_better_than_following_when_the_musician_slips) {
    Song s = build_scenario("erros_com_padrao");
    LabConfig held = cfg_c(), follow = cfg_c();
    held.manter_padrao = true;
    double a = metric(evaluate(run(to_double(s.audio), held), s.truth), "acc_total");
    double b = metric(evaluate(run(to_double(s.audio), follow), s.truth), "acc_total");
    CHECK(a > b);
}

TEST(learning_holds_the_tonic_until_the_pattern_locks) {
    // before the lock only the key's tonic (quiet pad), never a chase of chords; the key is the configured one if it
    // fits, else detected (tom_errado: set to C, music in B)
    for (const char* name : {"estavel_120", "tom_errado", "erros_com_padrao"}) {
        Song s = build_scenario(name);
        LabConfig cfg = s.truth.run;
        cfg.aprender_na_tonica = cfg.manter_padrao = true;
        Result res = run(to_double(s.audio), cfg);
        double lock = 0;
        for (const auto& f : res.log)
            if (f.state == State::Travado) { lock = f.t; break; }
        std::set<int> tonic;
        int before = 0;
        for (const auto& c : res.changes) {
            if (c.t >= lock) continue;
            ++before;
            CHECK(c.mode == Mode::Tonic || c.mode == Mode::Silence);
            if (c.mode == Mode::Tonic) tonic.insert(c.chord);
        }
        CHECK(before <= 2 && tonic == std::set<int>({std::string(name) == "tom_errado" ? note_pc("B") : 0}));
    }
}

TEST(core_is_rate_independent) {
    // live on a 48 kHz device the core analyses at 24 kHz: same musical result as at 22.05 kHz
    Song s = build_scenario("estavel_120");
    Metrics r = evaluate(run(resample_poly(to_double(s.audio), 160, 147), s.truth.run, 0.0, 24000.0), s.truth);
    CHECK(metric(r, "acc_total") >= 0.85 && metric(r, "falso_disparo_s") == 0 && metric(r, "lat_travado_ms") < 80);
}

// ------------------------------------------------------------------ exam: what must not get worse
TEST(song_sounds_a_half_step_below_the_shapes) {
    auto bars = exam::song({"pop", note_pc("B"), "clique"});
    CHECK(*bars[0].chord == "C" && *bars[1].chord == "G" && *bars[2].chord == "Am" && *bars[3].chord == "F");
    CHECK(label(bars[0].label()) == "B" && label(bars[1].label()) == "F#" && label(bars[2].label()) == "Abm" && label(bars[3].label()) == "E");
}

TEST(forms_keep_their_shapes_and_use_a_capo) {
    auto bars = exam::song({"formas_worship", note_pc("A"), "clique"});
    CHECK(*bars[0].chord == "G" && *bars[1].chord == "D/F#" && *bars[2].chord == "Em7" && *bars[3].chord == "Cadd9");
    CHECK(label(bars[0].label()) == "A" && label(bars[1].label()) == "E" && label(bars[2].label()) == "F#m" && label(bars[3].label()) == "D");
}

TEST(two_chords_in_a_bar_are_in_the_truth) {
    auto b = exam::build({"duas_por_compasso", 0, "clique"});
    std::vector<int> played;
    for (const auto& s : b.truth.chords)
        if (s.lab != N_IDX) played.push_back(s.lab);
    CHECK(std::vector<int>(played.begin(), played.begin() + 4) == indices({"C", "G", "Am", "F"}));
    NEAR(b.truth.chords[2].s - b.truth.chords[1].s, 2.0 * 60 / exam::BPMS[0], 0.01);
}

TEST(cents_zero_keeps_the_old_scenarios) {
    Guitar plain(1.0, 7), detuned(1.0, 7, 5.0);
    CHECK(std::all_of(plain.tuning.begin(), plain.tuning.end(), [](double t) { return t == 0; }));
    CHECK(std_dev({detuned.tuning.begin(), detuned.tuning.end()}) > 0);
}

TEST(score_catches_a_wrong_pattern) {
    // the score must see what the app played, not trust the lock: a confident wrong chord counts
    Truth tr;
    tr.duration = 8.0, tr.tempo_t = {0, 8}, tr.tempo_bpm = {120, 120};
    tr.chords = {{0.0, 4.0, chord_index("C")}, {4.0, 8.0, chord_index("G")}};
    tr.changes = {{0.0, -1, chord_index("C"), false}, {4.0, chord_index("C"), chord_index("G"), false}};
    Result res;
    res.changes = {{0.0, chord_index("C"), Mode::Full}, {4.0, chord_index("Am"), Mode::Full}};
    for (int i = 0; i < 80; ++i) {
        Frame f{};
        f.t = i * 0.1, f.state = State::Travado;
        res.log.push_back(f);
    }
    auto s = exam::score(res, tr, 0);
    NEAR(s.acc, 0.5, 0.01);
    NEAR(s.wrong_s, 3.5, 0.02);
    CHECK(exam::verdict(s) == "errou");
}

TEST(the_loop_the_musician_plays_most_in_every_key) {
    auto r = exam::run_exam(exam::cases({"pop"}, {}, {"clique", "tom_da_forma"}));
    double acc = 0;
    int ok = 0;
    for (const auto& x : r) acc += x.s.acc, ok += x.veredito == "ok";
    CHECK(acc / (double)r.size() >= 0.80 && ok >= 15);
}

TEST(cycle_that_closes_on_its_first_chord) {
    // C | Bb | F | C with the click: was 0/24, the pattern learned a two-bar tonic as one bar
    for (const auto& x : exam::run_exam(exam::cases({"emprestado"}, {0, 3, 6, 9}, {"clique"})))
        CHECK(x.s.acc >= 0.8);   // (the bug scored 0.42-0.57; 0.82 in F# has no wrong chord)
}

TEST(recorded_takes_rebuild_the_real_session) {
    // control of the splice: the good G-shape session keeps its score when cut and put back in order
    if (!exam::has_real_takes()) { std::cout << "  (sem sessoes/: pulado)\n"; return; }
    CHECK(exam::play({"pop", note_pc("F#"), "real_clique"}).s.acc >= 0.9);
}

TEST(click_sessions_learn_the_cycle_on_the_downbeat) {
    // the sessions of 2026-10-30: the pattern lost B and changed 2 beats late (acc 0.65 and 0.47)
    if (!exam::has_session(5)) { std::cout << "  (sem sessoes/: pulado)\n"; return; }
    auto r = exam::run_controls();
    double acc = 0;
    for (const auto& x : r) acc += x.s.acc;
    CHECK(acc / (double)r.size() >= 0.85 && r[5].veredito == "ok");
}

// ------------------------------------------------------------------ audio check
static const std::string BUILTIN = "Microphone Array (Intel Smart Sound Technology for Digital Microphones)";
static const std::string STOMP_IN = "Digital Audio Interface (Line 6 HX Stomp)";

// the machine where the bug was found: the pedal unplugged, the app silently on the laptop microphone
static AudioSystem stomp_unplugged() {
    AudioSystem s;
    s.inputs = {{STOMP_IN, AudioInput::NOT_PRESENT},
                {"Microphone (Line 6 HX Stomp)", AudioInput::NOT_PRESENT},
                {"Headset Microphone (AirPods Pro2 Hands-Free)", AudioInput::NOT_PRESENT},
                {"Microphone (G733 Gaming Headset)", AudioInput::NOT_PRESENT},
                {"Microphone (Realtek(R) Audio)", AudioInput::UNPLUGGED},
                {"Stereo Mix (Realtek(R) Audio)", AudioInput::DISABLED},
                {BUILTIN, AudioInput::ACTIVE}};
    return s;
}

TEST(session_report_reduces_the_frames_to_columns) {
    const std::string csv =
        "t,db,onset,bpm,tempo_conf,ouvido,conf,estado,nivel,tocando,proc_ms,falha\n"
        "0.1,-60,0,,0,N,0.3,OCIOSO,PERDIDO,-,0.1,0\n"                    // nothing yet
        "1.1,-20,1,98,1,F#,0.7,OUVINDO,CONFIANTE,tônica,0.1,0\n"         // learning: no chord played to disagree with
        "2.1,-20,0,98,1,Ebm,0.8,TRAVADO,CONFIANTE,Ebm,0.1,0\n"
        "2.2,-20,0,98,1,Ebm,0.6,TRAVADO,CONFIANTE,Ebm,0.1,0\n"
        "2.3,-20,0,98,1,C#,0.9,TRAVADO,CONFIANTE,Ebm,0.1,0\n"            // outvoted inside the column
        "3.1,-20,0,98,1,E,0.8,TRAVADO,CONFIANTE,Eb(sem terça),0.1,0\n"   // E is not Eb
        "4.1,-20,0,98,1,Ebm,0.8,TRAVADO,CONFIANTE,Eb(sem terça),0.1,0\n" // same root, the third is what differs
        "5.1,-20,0";                                                     // cut short
    auto c = report_columns(csv, 1.0);
    CHECK(c.size() == 5);
    CHECK(c[0].heard.empty() && c[0].played.empty() && !c[0].miss);
    CHECK(c[1].heard == "F#" && c[1].played == "tônica" && !c[1].miss);
    CHECK(c[2].heard == "Ebm" && c[2].conf == 70 && c[2].played == "Ebm" && !c[2].miss && c[2].t == 2.0);
    CHECK(c[3].miss);
    CHECK(!c[4].miss);
}

TEST(audio_check_names_the_interface_that_is_missing) {
    auto d = diagnose(stomp_unplugged(), BUILTIN);
    CHECK(d.level == "aviso");
    CHECK(d.message.find("Line 6 HX Stomp não está conectado") != std::string::npos);
    CHECK(d.message.find("Line 6 HX Stomp, Line 6 HX Stomp") == std::string::npos);   // two inputs, one device
    CHECK(d.message.find("G733") == std::string::npos && d.message.find("AirPods") == std::string::npos);   // headsets are not the guitar
    CHECK(d.message.find("Realtek") == std::string::npos);   // an empty jack is the computer's own
}

TEST(audio_check_stays_quiet_when_the_interface_is_in_use) {
    auto s = stomp_unplugged();
    s.inputs[0].state = AudioInput::ACTIVE;
    CHECK(diagnose(s, STOMP_IN).level == "ok");
}

TEST(audio_check_points_to_the_interface_left_unselected) {
    auto s = stomp_unplugged();
    s.inputs[0].state = AudioInput::ACTIVE;
    auto d = diagnose(s, BUILTIN);
    CHECK(d.level == "aviso" && d.message.find("está conectado") != std::string::npos);
}

TEST(audio_check_blocking_problems_come_first) {
    auto s = stomp_unplugged();
    s.bad_drivers = {"HX Stomp"};
    CHECK(diagnose(s, BUILTIN).message.find("Driver sem funcionar: HX Stomp") != std::string::npos);

    s.inputs.back().muted = true;
    auto muted = diagnose(s, BUILTIN);
    CHECK(muted.level == "erro" && muted.settings == "som" && muted.message.find("muda") != std::string::npos);

    s.mic_blocked = true;
    CHECK(diagnose(s, BUILTIN).settings == "privacidade");
}

TEST(audio_check_without_any_active_input) {
    auto s = stomp_unplugged();
    s.inputs.back().state = AudioInput::DISABLED;
    auto d = diagnose(s, "");
    CHECK(d.level == "erro" && d.message.find("Line 6 HX Stomp") != std::string::npos);
    CHECK(diagnose(AudioSystem{}, "").message.find("nenhuma entrada") != std::string::npos);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    fs::current_path(KORDIA_REPO_DIR);
    std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0;
    std::vector<std::string> failed;
    auto t0 = std::chrono::steady_clock::now();
    for (const Test& t : registry()) {
        if (std::string(t.name).find(filter) == std::string::npos) continue;
        try {
            t.fn();
            ++passed;
        } catch (const std::exception& e) {
            failed.push_back(t.name);
            std::cout << "FALHOU " << t.name << ": " << e.what() << "\n";
        }
    }
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%d passaram, %d falharam em %.1f s\n", passed, (int)failed.size(), took);
    return failed.empty() ? 0 : 1;
}
