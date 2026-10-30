// Prova: songs the engine was never tuned on, in all 12 keys, with the settings of the live app and the imperfections
// measured in the real sessions. Answers one question: does the app end up playing what was played?
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

#include "lab.h"
#include "pocketfft_hdronly.h"

namespace lab::exam {

// measured in sessoes/ (2026-10-29 and 30)
static constexpr double LATE = 0.08;           // s: strums reach the input this late against the click (median 54-92 ms)
static constexpr double JITTER = 0.025;        // s: std of each strum against the grid
static constexpr double CENTS = 5.0;           // std of each string's tuning error (the sessions sit within +/-7 cents)
static constexpr double NOISE_DB = -32.0;      // room noise relative to the playing level
static constexpr double LEVEL = 0.3;           // 0.5 peak -> 0.15 peak: the sessions peak at 0.05-0.5
static constexpr double ROOM_SECONDS = 0.25;   // decay of a small room; the input is a USB mic, not a pickup
static constexpr int TUNED_DOWN = -1;          // the guitar is a half step down: shapes sound one semitone below their name
static constexpr int MIN_BARS = 24, MIN_LAPS = 4;
static constexpr double SESSION_BPM = 90.0;
static constexpr double FADE = 0.005;          // s: splice between two recorded bars
static const char* PATTERNS[3] = {"DUDUDUDU", "D-DU-UDU", "DUxUDUxU"};
static const std::set<std::string> FORMS = {"formas_worship", "formas_sus"};   // shapes only: transposed with a capo
static const std::vector<std::string> DEGREES = {"C", "G", "Am", "F"};
static const fs::path SESSIONS_DIR = "sessoes";

// the real guitar: bars of the live sessions, played on the click over I V vi IV (sessoes/ is not versioned).
// (folder, sounding key, first bar on chord I, end bar). The truth of a bar is its position in the cycle, not what
// the app heard, so a bar it misheard live (the Bbm barre) stays in the exam as a hard bar.
struct Take { const char* folder; int key, first, end; };
static const Take SESSIONS[4] = {{"2026-10-29_22-50-35", 11, 1, 25}, {"2026-10-29_22-51-45", 1, 1, 11},
                                 {"2026-10-29_22-52-18", 6, 3, 23}, {"2026-10-30_10-57-27", 6, 1, 37}};
// control: real sessions played straight, whole, with the interface's settings. (folder, cycle, first and end bar)
static const std::vector<Control> CONTROLS = {
    {"2026-10-29_22-50-35", "B F# Abm E", 1, 25}, {"2026-10-29_22-51-45", "C# Ab Bbm F#", 1, 11},
    {"2026-10-29_22-52-18", "F# C# Ebm B", 3, 23}, {"2026-10-30_10-57-27", "F# C# Ebm B", 1, 37},
    {"2026-10-30_15-52-02", "F# C# Ebm B", 1, 20}, {"2026-10-30_15-53-00", "F# C# Ebm B", 1, 21}};

// bars split by '|'; two chords in a bar change on beat 3. Written in C (or G: FORMS), transposed to each key.
const std::vector<std::pair<std::string, std::string>>& progressions() {
    static const std::vector<std::pair<std::string, std::string>> all = {
        {"pop", "C | G | Am | F"},
        {"sensivel", "Am | F | C | G"},
        {"worship", "C | F | Am | G"},
        {"tres_acordes", "C | F | G"},
        {"dois_acordes", "C | F"},
        {"duas_por_compasso", "C G | Am F"},
        {"dois_compassos", "C | C | G | G | Am | Am | F | F"},
        {"volta_na_tonica", "C | G | C | F"},
        {"seis_acordes", "C | G | Am | Em | F | G"},
        {"canone", "C | G | Am | Em | F | C | F | G"},
        {"falsa_volta", "C | G | C | G | Am | F"},
        {"menor", "Am | G | F | G"},
        {"ii_V_I", "Dm | G | C | Am"},
        {"emprestado", "C | Bb | F | C"},
        {"dominante_secundaria", "C | E | Am | F"},
        {"formas_worship", "G | D/F# | Em7 | Cadd9"},
        {"formas_sus", "G | G/B | Cadd9 | Dsus4"}};
    return all;
}

static std::vector<std::string> words(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

static std::vector<std::vector<std::string>> lap_of(const std::string& prog) {
    std::vector<std::vector<std::string>> lap;
    for (const auto& [name, text] : progressions()) {
        if (name != prog) continue;
        std::istringstream in(text);
        for (std::string bar; std::getline(in, bar, '|');) lap.push_back(words(bar));
    }
    return lap;
}

static bool is_real_progression(const std::string& prog) {
    for (const auto& bar : lap_of(prog))
        for (const auto& c : bar)
            if (std::find(DEGREES.begin(), DEGREES.end(), c) == DEGREES.end()) return false;
    return true;
}

static std::string note_name(int pc) { return label(pc); }

std::string Case::name() const { return prog + "/" + note_name(key) + "/" + variant; }

static uint32_t seed_of(const std::string& text) {
    uint32_t s = 0;
    for (unsigned char c : text) s += c;
    return s;
}

// Bars of the song and its tempo: pattern and BPM rotate with the key, so the matrix stays one song per cell.
std::vector<Bar> song(const Case& c, int* bpm_out) {
    const char* pattern = PATTERNS[c.key % 3];
    int bpm = BPMS[c.key % 4];
    bool forms = FORMS.count(c.prog) > 0;
    int home = note_pc(forms ? "G" : "C"), up = (int)floor_mod(c.key - TUNED_DOWN - home, 12);
    int capo = forms ? up + TUNED_DOWN : TUNED_DOWN;
    auto shape = [&](const std::string& s) { return forms ? s : label(chord_transpose(chord_index(s), up)); };
    std::vector<Bar> lap;
    for (const auto& tokens : lap_of(c.prog)) {
        Bar b;
        b.chord = shape(tokens[0]), b.pattern = pattern, b.bpm = bpm, b.capo = capo;
        if (tokens.size() > 1) b.half = shape(tokens[1]);
        lap.push_back(b);
    }
    int laps = std::max(MIN_LAPS, (int)std::ceil((double)MIN_BARS / (double)lap.size()));
    std::vector<Bar> bars;
    for (int i = 0; i < laps; ++i) bars.insert(bars.end(), lap.begin(), lap.end());
    auto last = final_bar(*lap[0].chord, bpm, capo);
    bars.insert(bars.end(), last.begin(), last.end());
    if (bpm_out) *bpm_out = bpm;
    return bars;
}

// x is single precision and goes through a single precision FFT, as the measured chain did.
static std::vector<double> room(const std::vector<float>& x, Rng& rng) {
    size_t tail = (size_t)(ROOM_SECONDS * SR), n = x.size() + tail + 1;
    std::vector<double> ir(n, 0.0);
    ir[0] = 1.0;
    for (size_t i = 0; i < tail; ++i)
        ir[i + 1] = 0.04 * (rng.normal() * std::exp(-(double)i / (ROOM_SECONDS / 6.9 * SR)));
    std::vector<float> padded(x);
    padded.resize(n, 0.0f);
    size_t bins = n / 2 + 1, out_len = 2 * (bins - 1);
    std::vector<std::complex<float>> X(bins);
    std::vector<std::complex<double>> H(bins);
    pocketfft::r2c(pocketfft::shape_t{n}, pocketfft::stride_t{sizeof(float)}, pocketfft::stride_t{sizeof(std::complex<float>)},
                   pocketfft::shape_t{0}, pocketfft::FORWARD, padded.data(), X.data(), 1.0f);
    pocketfft::r2c(pocketfft::shape_t{n}, pocketfft::stride_t{sizeof(double)}, pocketfft::stride_t{sizeof(std::complex<double>)},
                   pocketfft::shape_t{0}, pocketfft::FORWARD, ir.data(), H.data(), 1.0);
    for (size_t i = 0; i < bins; ++i) H[i] *= std::complex<double>(X[i]);
    std::vector<double> y(out_len);
    pocketfft::c2r(pocketfft::shape_t{out_len}, pocketfft::stride_t{sizeof(std::complex<double>)}, pocketfft::stride_t{sizeof(double)},
                   pocketfft::shape_t{0}, pocketfft::BACKWARD, H.data(), y.data(), 1.0 / (double)out_len);
    y.resize(x.size());
    return y;
}

using Takes = std::map<std::string, std::vector<std::vector<double>>>;
struct KeyedTakes { int key; Takes bars; };

// One entry per session: a song never mixes two sessions, whose levels differ by 12 dB.
static const std::vector<KeyedTakes>& takes() {
    static std::once_flag once;
    static std::vector<KeyedTakes> all;
    std::call_once(once, [] {
        for (const Take& t : SESSIONS) {
            Session s = load_session(SESSIONS_DIR / t.folder);
            if ((int)s.sr != SR || !s.cfg.bpm || *s.cfg.bpm != SESSION_BPM) throw std::runtime_error(std::string("sessão inesperada: ") + t.folder);
            double bar = 4 * 60.0 / SESSION_BPM;
            KeyedTakes k{t.key, {}};
            for (int i = t.first; i < t.end; ++i) {
                size_t a = (size_t)((0.5 + s.lookahead + LATE - 0.03 + i * bar) * SR), e = std::min(a + (size_t)(bar * SR), s.audio.size());
                k.bars[DEGREES[(size_t)((i - t.first) % 4)]].emplace_back(s.audio.begin() + (std::ptrdiff_t)a,
                                                                          s.audio.begin() + (std::ptrdiff_t)e);
            }
            all.push_back(std::move(k));
        }
    });
    return all;
}

// A new song cut from real bars, moved to the key by the shortest shift (which also moves the tempo).
static Built build_real(const Case& c) {
    Rng rng(seed_of(c.name()));
    auto shift = [&](int src) { return (int)floor_mod(c.key - src + 6, 12) - 6; };
    int nearest = 99;
    for (const auto& t : takes()) nearest = std::min(nearest, std::abs(shift(t.key)));
    std::vector<const KeyedTakes*> options;
    for (const auto& t : takes())
        if (std::abs(shift(t.key)) == nearest) options.push_back(&t);
    const KeyedTakes& pick = *options[(size_t)rng.integers((long long)options.size())];
    double ratio = std::pow(2.0, shift(pick.key) / 12.0), bpm = SESSION_BPM * ratio;

    auto lap = lap_of(c.prog);
    int laps = std::max(MIN_LAPS, (int)std::ceil((double)MIN_BARS / (double)lap.size()));
    std::vector<std::vector<std::string>> bars;
    for (int i = 0; i < laps; ++i) bars.insert(bars.end(), lap.begin(), lap.end());
    bars.push_back({lap[0][0]});

    size_t n = pick.bars.at(DEGREES[0])[0].size();
    auto fade = linspace(0, 1, (int)(FADE * SR));
    std::vector<double> body;
    std::vector<std::pair<std::string, size_t>> chords;
    for (const auto& tokens : bars)
        for (const auto& chord : tokens) {
            const auto& options_of = pick.bars.at(chord);
            const auto& take = options_of[(size_t)rng.integers((long long)options_of.size())];
            std::vector<double> x(take.begin(), take.begin() + (std::ptrdiff_t)std::min(n / tokens.size(), take.size()));
            for (size_t i = 0; i < fade.size(); ++i) x[i] *= fade[i];
            for (size_t i = 0; i < fade.size(); ++i) x[x.size() - fade.size() + i] *= fade[fade.size() - 1 - i];
            body.insert(body.end(), x.begin(), x.end());
            chords.push_back({chord, x.size()});
        }
    body = resample_poly(body, 1000, (int)round_even(1000 * ratio));

    bool click = c.variant == "real_clique";
    double lead = click ? 0.5 + LOOKAHEAD + 8 * 60.0 / bpm + LATE - 0.03 : 1.0;
    std::vector<double> audio((size_t)(long long)(lead * SR), 0.0);
    audio.insert(audio.end(), body.begin(), body.end());
    audio.resize(audio.size() + 3 * SR, 0.0);
    double duration = (double)audio.size() / SR, t = lead;
    Built out;
    Truth& tr = out.truth;
    tr.chords.push_back({0.0, lead, N_IDX});
    int last = -1;
    for (const auto& [chord, samples] : chords) {
        int lab = chord_transpose(chord_index(chord), c.key);
        double end = t + (double)samples / SR / ratio;
        if (lab != last) tr.changes.push_back({t, last, lab, false});
        tr.chords.push_back({t, end, lab});
        t = end, last = lab;
    }
    tr.chords.push_back({t, duration, N_IDX});
    tr.duration = duration, tr.tempo_t = {0.0, duration}, tr.tempo_bpm = {bpm, bpm};
    out.audio.assign(audio.begin(), audio.end());
    out.cfg.modo = click ? "metronomo" : "livre";
    if (click) out.cfg.bpm = bpm;
    out.cfg.clique = out.cfg.contagem = click;
    out.cfg.manter_padrao = out.cfg.aprender_na_tonica = true;
    return out;
}

Built build(const Case& c) {
    if (c.variant == "real_clique" || c.variant == "real_livre") return build_real(c);
    int bpm = 0;
    auto bars = song(c, &bpm);
    uint32_t seed = seed_of(c.name());
    bool click = c.variant != "livre";
    RenderOpts o;
    // with the click the musician enters on a downbeat two bars after Iniciar, late by the measured offset
    o.lead = click ? 0.5 + LOOKAHEAD + 8 * 60.0 / bpm + LATE : 1.0;
    o.jitter = JITTER, o.noise_db = NOISE_DB, o.cents = CENTS;
    Song s = render(bars, seed, o);
    Rng rng(seed);
    auto wet = room(s.audio, rng);
    Built out;
    out.truth = std::move(s.truth);
    out.audio.resize(wet.size());
    for (size_t i = 0; i < wet.size(); ++i) out.audio[i] = (float)(LEVEL * wet[i]);
    out.cfg.modo = click ? "metronomo" : "livre";
    if (click) out.cfg.bpm = bpm;
    out.cfg.clique = out.cfg.contagem = click;
    if (c.variant == "tom_da_forma") out.cfg.tom = note_name((int)floor_mod(c.key - TUNED_DOWN, 12));
    out.cfg.manter_padrao = out.cfg.aprender_na_tonica = true;
    return out;
}

// Chords the app played over the last lap, for reading a failure at a glance.
static std::string last_lap(const Result& res, const Truth& truth) {
    std::set<int> distinct;
    for (const auto& s : truth.chords)
        if (s.lab != N_IDX && s.lab != PAUSE) distinct.insert(s.lab);
    std::string all;
    for (const auto& c : res.changes)
        if (c.chord >= 0 && c.mode == Mode::Full) all += (all.empty() ? "" : " ") + label(c.chord);
    size_t keep = 6 * std::max<size_t>(distinct.size(), 4);
    std::string tail = all.size() > keep ? all.substr(all.size() - keep) : all;
    size_t space = tail.find(' ');
    return space == std::string::npos ? tail : tail.substr(space + 1);
}

// lock_s: from the first strum to the pattern locking. acc: share of the time after the lock with the exact chord.
// wrong_s: seconds of harmony that clashes (after the first beat of a change, runs of 1/2 beat or more).
Score score(const Result& res, const Truth& truth, int key) {
    auto g = time_grid(truth.duration);
    auto tl = truth_labels(truth, g);
    auto [lab, mode] = timeline(res.changes, g);
    double beat = 60.0 / median(truth.tempo_bpm);
    double first = 0;
    for (const auto& s : truth.chords)
        if (s.lab != N_IDX && s.lab != PAUSE) { first = s.s; break; }
    std::optional<double> lock;
    for (const auto& f : res.log)
        if (f.state == State::Travado) { lock = f.t; break; }

    size_t n = g.size(), after = 0, right_after = 0;
    auto root = [](int l) { return l >= 0 && l < 24 ? l % 12 : -1; };
    std::vector<char> wrong(n);
    for (size_t i = 0; i < n; ++i) {
        bool is_chord = tl[i] != N_IDX && tl[i] != PAUSE;
        if (is_chord && lock && g[i] >= *lock) {
            ++after;
            right_after += mode[i] == Mode::Full && lab[i] == tl[i];
        }
        bool clash = (mode[i] == Mode::Full && lab[i] != tl[i]) || (mode[i] == Mode::Root5 && root(lab[i]) != root(tl[i]));
        if (mode[i] == Mode::Tonic && lab[i] >= 0 && !is_diatonic(lab[i], key)) clash = true;
        wrong[i] = is_chord && clash;
    }
    for (const auto& c : truth.changes)
        for (size_t i = 0; i < n; ++i)
            if (g[i] >= c.t && g[i] < c.t + beat) wrong[i] = 0;
    long long clash_frames = 0;
    for (long long r : runs(wrong))
        if ((double)r * DT >= beat / 2) clash_frames += r;
    Score s;
    if (lock) {
        s.lock_s = round_digits(*lock - first, 2);
        s.lock_compassos = round_digits((*lock - first) / (4 * beat), 1);
    }
    s.acc = after ? round_digits((double)right_after / (double)after, 3) : 0.0;
    s.wrong_s = round_digits((double)clash_frames * DT, 2);
    s.played = last_lap(res, truth);
    return s;
}

std::string verdict(const Score& s) {
    if (!s.lock_s) return "nao_travou";
    return s.acc >= 0.90 && s.wrong_s <= 2.0 ? "ok" : "errou";
}

Row play(const Case& c) {
    Built b = build(c);
    std::vector<double> audio(b.audio.begin(), b.audio.end());
    Score s = score(run(std::move(audio), b.cfg, b.cfg.clique ? LOOKAHEAD : 0.0), b.truth, c.key);
    return {c.name(), c.prog, note_name(c.key), c.variant, verdict(s), s};
}

// A real click session as it was played, truth = position in the cycle, automatic key as in the interface.
static ControlRow play_control(const Control& control) {
    Session se = load_session(SESSIONS_DIR / control.folder);
    LabConfig cfg = se.cfg;
    cfg.tom.reset();
    cfg.manter_padrao = cfg.aprender_na_tonica = true;
    auto cycle = words(control.cycle);
    double bar = 4 * 60.0 / *cfg.bpm, t0 = 0.5 + se.lookahead + LATE;
    Truth tr;
    tr.chords.push_back({0.0, t0 + control.first * bar, N_IDX});
    for (int b = control.first; b < control.end; ++b) {
        int lab = chord_index(cycle[(size_t)floor_mod(b - control.first, (long long)cycle.size())]);
        tr.chords.push_back({t0 + b * bar, t0 + (b + 1) * bar, lab});
        tr.changes.push_back({t0 + b * bar, -1, lab, false});
    }
    tr.duration = tr.chords.back().e;
    tr.tempo_t = {0.0, tr.duration}, tr.tempo_bpm = {*cfg.bpm, *cfg.bpm};
    se.audio.resize(std::min(se.audio.size(), (size_t)(tr.duration * se.sr)));   // a take may end before its last bar
    Score s = score(run(std::move(se.audio), cfg, se.lookahead, se.sr), tr, note_pc(cycle[0]));
    return {control.folder, verdict(s), s};
}

bool has_session(int control) { return fs::is_directory(SESSIONS_DIR / CONTROLS[(size_t)control].folder); }

bool has_real_takes() {
    return std::all_of(std::begin(SESSIONS), std::end(SESSIONS), [](const Take& t) { return fs::is_directory(SESSIONS_DIR / t.folder); });
}

std::vector<Case> cases(const std::vector<std::string>& progs_in, const std::vector<int>& keys_in, const std::vector<std::string>& variants_in) {
    std::vector<std::string> progs = progs_in, variants = variants_in;
    if (progs.empty())
        for (const auto& [name, text] : progressions()) progs.push_back(name);
    if (variants.empty()) {
        variants = VARIANTS;
        if (has_real_takes()) variants.insert(variants.end(), REAL_VARIANTS.begin(), REAL_VARIANTS.end());
    }
    std::vector<int> keys = keys_in;
    if (keys.empty())
        for (int k = 0; k < 12; ++k) keys.push_back(k);
    std::vector<Case> out;
    for (const auto& p : progs)
        for (const auto& v : variants) {
            bool real = std::find(REAL_VARIANTS.begin(), REAL_VARIANTS.end(), v) != REAL_VARIANTS.end();
            if (real && !is_real_progression(p)) continue;
            for (int k : keys) out.push_back({p, k, v});
        }
    return out;
}

std::vector<Row> run_exam(const std::vector<Case>& selection) {
    std::vector<Row> rows(selection.size());
    parallel_for(selection.size(), [&](size_t i) { rows[i] = play(selection[i]); });
    return rows;
}

std::vector<ControlRow> run_controls() {
    std::vector<Control> present;
    for (size_t i = 0; i < CONTROLS.size(); ++i)
        if (has_session((int)i)) present.push_back(CONTROLS[i]);
    std::vector<ControlRow> rows(present.size());
    parallel_for(present.size(), [&](size_t i) { rows[i] = play_control(present[i]); });
    return rows;
}

static std::string opt_text(const std::optional<double>& v) { return v ? shortest_repr(*v) : "None"; }

void print_controls(const std::vector<ControlRow>& rows) {
    double acc = 0, wrong = 0;
    for (const auto& r : rows) {
        std::cout << std::format("  {}  {:10s} acc {:.2f} errado {:5.1f} s trava {}\n", r.sessao, r.veredito, r.s.acc, r.s.wrong_s, opt_text(r.s.lock_compassos));
        acc += r.s.acc, wrong += r.s.wrong_s;
    }
    if (!rows.empty()) std::cout << std::format("  média: acc {:.2f}, errado {:.1f} s\n", acc / (double)rows.size(), wrong / (double)rows.size());
}

void print_exam(const std::vector<Row>& rows) {
    std::vector<std::string> columns;
    for (const auto& list : {VARIANTS, REAL_VARIANTS})
        for (const auto& v : list)
            if (std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.variante == v; })) columns.push_back(v);
    std::cout << std::format("{:24s}", "progressão");
    for (const auto& v : columns) std::cout << std::format("{:>14s}", v);
    std::cout << std::format("{:>7s}{:>13s}{:>10s}\n", "acc", "trava(comp)", "errado_s");
    for (const auto& [prog, text] : progressions()) {
        std::vector<const Row*> rs;
        for (const auto& r : rows)
            if (r.progressao == prog) rs.push_back(&r);
        if (rs.empty()) continue;
        std::cout << std::format("{:24s}", prog);
        for (const auto& v : columns) {
            int ok = 0, total = 0;
            for (const Row* r : rs)
                if (r->variante == v) ++total, ok += r->veredito == "ok";
            std::cout << std::format("{:>14s}", total ? std::format("{}/{}", ok, total) : "-");
        }
        std::vector<double> lock;
        double acc = 0, wrong = 0;
        for (const Row* r : rs) {
            if (r->s.lock_compassos) lock.push_back(*r->s.lock_compassos);
            acc += r->s.acc, wrong += r->s.wrong_s;
        }
        std::cout << std::format("{:7.2f}{:13.1f}{:10.1f}\n", acc / (double)rs.size(), lock.empty() ? std::nan("") : median(lock), wrong / (double)rs.size());
    }
    long long ok = std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.veredito == "ok"; });
    std::cout << std::format("\n{}/{} músicas certas ({:.0f}%)\n", ok, rows.size(), 100.0 * (double)ok / (double)rows.size());
    for (const auto& r : rows)
        if (r.veredito != "ok")
            std::cout << std::format("  {:10s} {:42s} acc {:.2f} errado {:5.1f} s trava {}  [{}]\n", r.veredito, r.caso, r.s.acc, r.s.wrong_s, opt_text(r.s.lock_compassos), r.s.played);
}

fs::path save_exam(const std::vector<Row>& rows, const std::string& versao) {
    fs::path out = fs::path("resultados") / versao;
    fs::create_directories(out);
    std::string text = "caso,progressao,tom,variante,veredito,lock_s,lock_compassos,acc,wrong_s,played\r\n";
    auto opt = [](const std::optional<double>& v) { return v ? shortest_repr(*v) : std::string(); };
    for (const auto& r : rows)
        text += r.caso + "," + r.progressao + "," + r.tom + "," + r.variante + "," + r.veredito + "," + opt(r.s.lock_s) + "," +
                opt(r.s.lock_compassos) + "," + shortest_repr(r.s.acc) + "," + shortest_repr(r.s.wrong_s) + "," + r.s.played + "\r\n";
    write_text(out / "prova.csv", text);
    return out / "prova.csv";
}

}  // namespace lab::exam
