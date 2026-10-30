// Kordia lab: synthetic scenarios, metrics, the exam, reports and session replay, on top of the core.
// Nothing here runs live; the app only calls the kordia_lab executable to draw the report of a finished session.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kordia/core.h"

namespace lab {

using namespace kordia;
namespace fs = std::filesystem;

constexpr int SR = 22050;    // Hz: rate of the synthetic audio and of the 44.1 kHz sessions
constexpr int PAUSE = 25;    // truth label "P": a short pause inside a song (N_IDX = no chord, silence)
std::string truth_label(int lab);   // "N", "P" or the chord; "None" for -1

// ------------------------------------------------------------------ random numbers
// NumPy's default_rng (SeedSequence + PCG64 + ziggurat), bit by bit: the scenarios and the exam stay the same songs
// that every version in docs/resultados.md was measured on.
class Rng {
public:
    explicit Rng(uint32_t seed);
    uint64_t next64();
    uint32_t next32();
    double next_double();
    double normal();                         // standard_normal()
    double normal(double loc, double scale) { return loc + scale * normal(); }
    double uniform(double lo, double hi) { return lo + (hi - lo) * next_double(); }
    long long integers(long long lo, long long hi);   // [lo, hi)
    long long integers(long long hi) { return integers(0, hi); }
private:
    uint64_t hi_ = 0, lo_ = 0, inc_hi_ = 0, inc_lo_ = 0;
    uint32_t saved_ = 0;
    bool has_saved_ = false;
    void step();
};

// ------------------------------------------------------------------ dsp (offline helpers)
double np_sum(const double* x, size_t n);    // pairwise, like numpy: the rounding of a sum is part of the audio
double np_mean(const std::vector<double>& x);
double median(std::vector<double> x);
double percentile(std::vector<double> x, double q);
double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp);
double std_dev(const std::vector<double>& x);
double corrcoef(const std::vector<double>& x, const std::vector<double>& y);
std::vector<double> linspace(double a, double b, int n);
std::vector<double> lfilter(const std::vector<double>& b, const std::vector<double>& a, const std::vector<double>& x);
std::vector<double> resample_poly(const std::vector<double>& x, int up, int down);
double round_digits(double x, int digits);       // correctly rounded decimal
std::string shortest_repr(double x);               // shortest form that reads back the same, with ".0" on whole numbers

// Runs f(0..n-1) on all cores; rethrows the first failure.
void parallel_for(size_t n, const std::function<void(size_t)>& f);

// ------------------------------------------------------------------ files
struct Wav { std::vector<double> samples; int channels = 1; double sr = SR; };   // interleaved
Wav read_wav(const fs::path& path);          // PCM 16/24/32, float 32/64
void write_wav(const fs::path& path, const std::vector<double>& samples, int channels, double sr);   // PCM 16
std::vector<double> load_wav(const fs::path& path);   // any WAV -> mono at SR

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    static Json parse(const std::string& text);    // throws std::runtime_error
    static Json load(const fs::path& path);
    const Json* get(const std::string& key) const;  // nullptr: missing or null
};

// ------------------------------------------------------------------ one offline run
struct LabConfig : RunConfig {
    double fb_gain = 0.0, fb_delay = 0.0;   // test only: closed loop output -> input (gain, s)
};

struct Result {
    std::vector<double> audio, accomp;      // input as heard (including feedback); stereo interleaved
    std::vector<Frame> log;
    std::vector<Event> events;
    std::vector<Change> changes;
    std::vector<std::pair<double, double>> onsets;
    std::vector<Decision> decisions;
};

using Layers = std::array<double, P::N_LAYERS>;
// lookahead: only to replay a live session exactly. sr: sessions from 48 kHz devices are analysed at 24 kHz.
Result run(std::vector<double> audio, const LabConfig& cfg, double lookahead = 0.0, double sr = SR,
           const Layers& layers = {1.0, 0.0, 0.0, 0.0, 0.0}, double bass_gain = 1.0);

struct Session { std::vector<double> audio; LabConfig cfg; double lookahead = 0.0, sr = SR; };
// Folder sessoes/<date-time>/, written by SessionWriter (cpp/app/LiveEngine.cpp):
//   config.json     run config + devices + latency
//   entrada.wav     input exactly as the processor saw it (after input gain)
//   quadros.csv     one row per block: level, heard chord, confidence, state, BPM, cost, audio failures
//   decisoes.jsonl  every accompaniment change and every engine reason with its cause
//   resumo.json     totals written at stop; replay_<folder>.svg is added by the offline replay right after
Session load_session(const fs::path& folder);
LabConfig config_from_json(const Json& run);

// ------------------------------------------------------------------ chord shapes
int chord_transpose(int idx, int n);
std::vector<int> voicing(const std::string& shape, int capo = 0);   // MIDI notes, low to high

// ------------------------------------------------------------------ synthetic guitar (Karplus-Strong)
std::vector<double> pluck(double midi, double dur, double amp, double bright, Rng& rng, double t60 = 0.0);

class Guitar {   // renders strums into a buffer; each string is cut when re-plucked or muted, like a real guitar
public:
    Guitar(double duration, uint32_t seed = 0, double cents = 0.0);   // cents: std of each string's tuning error
    void mute(double t);
    void strum(double t, const std::vector<int>& notes, char direction = 'D', double amp = 1.0, double spread = 0.012);
    void single(double t, int midi, double amp = 1.0);
    void chk(double t, double amp = 1.0);
    std::vector<double> out;
    std::array<double, 6> tuning{};
private:
    struct Ringing { int string; long long start; std::vector<double> y; };
    void cut(size_t index, long long at);
    void add(int string, double t, std::vector<double> y);
    Rng rng_;
    std::vector<Ringing> ringing_;   // in the order they started: the subtraction order is part of the audio
};

// ------------------------------------------------------------------ scenarios
struct Bar {
    std::optional<std::string> chord;      // guitar shape ('C', 'Am/G'); none = silent bar (strings muted)
    std::string pattern = "DUDUDUDU";      // 8 eighths: D down, U up, x muted chk, - ring, m mute, b single bass note
    double bpm = 110.0;                    // tempo at bar start (interpolated to the next bar)
    int capo = 0;
    double gain = 1.0;
    double shift = 0.0;                    // beats; the guitarist changes early (<0) or late (>0) vs the grid
    std::optional<std::string> shape2;     // different shape for the second half (same harmony, e.g. 'Am/G')
    std::vector<int> bass_notes;           // MIDI notes consumed by 'b' slots
    bool surprise = false;
    std::optional<std::string> wrong;      // musician's mistake: this shape on the first half (truth stays `chord`)
    bool late_entry = false;               // musician's mistake: an 'm' here is a late entry, not a pause
    std::optional<std::string> half;       // harmony changes on beat 3 to this shape (truth changes too)
    int label() const;
};

struct Seg { double s, e; int lab; };
struct TruthChange { double t; int from, to; bool surprise; };
struct Truth {
    double duration = 0;
    std::vector<double> beats, downbeats;
    std::vector<double> tempo_t, tempo_bpm;
    std::vector<Seg> chords;
    std::vector<TruthChange> changes;
    std::vector<double> ends;
    std::vector<std::pair<double, double>> intensity;   // bar start, gain
    std::string name, description;
    int seed = 0;
    LabConfig run;
};
std::string truth_to_json(const Truth& t);
Truth truth_from_json(const Json& j);

struct RenderOpts {
    double lead = 1.0, tail = 3.0, jitter = 0.010, cents = 0.0;
    std::optional<double> noise_db, voice_db;
};
struct Song { std::vector<float> audio; Truth truth; };
Song render(const std::vector<Bar>& bars, uint32_t seed, const RenderOpts& opts = {});
std::vector<Bar> final_bar(const std::string& chord = "C", double bpm = 110.0, int capo = 0, double gain = 1.0);

struct Scenario {
    std::string name, description;
    std::function<std::vector<Bar>(Rng&)> bars;
    RenderOpts opts;
    LabConfig run;
};
const std::vector<Scenario>& scenarios();
Song build_scenario(const std::string& name);
std::vector<double> pink_noise(size_t n, Rng& rng);

// ------------------------------------------------------------------ metrics
struct Val {
    enum Kind { None, Bool, Int, Real } kind = None;
    double v = 0;
    bool isnan() const { return kind == Real && std::isnan(v); }
};
using Metrics = std::vector<std::pair<std::string, Val>>;
double metric(const Metrics& m, const std::string& key);    // throws if missing or empty
const Val& metric_val(const Metrics& m, const std::string& key);
Metrics evaluate(const Result& res, const Truth& truth);
std::string val_csv(const Val& v);
std::string val_json(const Val& v);
constexpr double DT = 0.01;   // s: metrics timeline

std::vector<double> time_grid(double duration);
std::vector<int> truth_labels(const Truth& truth, const std::vector<double>& g);
struct Timeline { std::vector<int> lab; std::vector<Mode> mode; };
Timeline timeline(const std::vector<Change>& changes, const std::vector<double>& g);
std::vector<long long> runs(const std::vector<char>& mask);

// ------------------------------------------------------------------ exam: new songs in all 12 keys
namespace exam {
inline const std::vector<std::string> VARIANTS = {"clique", "tom_da_forma", "livre"};
inline const std::vector<std::string> REAL_VARIANTS = {"real_clique", "real_livre"};
const std::vector<std::pair<std::string, std::string>>& progressions();
constexpr int BPMS[4] = {90, 72, 120, 104};
constexpr double LOOKAHEAD = 0.0218;

struct Case {
    std::string prog;
    int key;              // sounding key (pitch class)
    std::string variant;  // clique: metronome + automatic key; tom_da_forma: key set to the shape's name; livre: no click
    std::string name() const;
};
struct Built { std::vector<float> audio; Truth truth; LabConfig cfg; };
struct Score { std::optional<double> lock_s, lock_compassos; double acc = 0, wrong_s = 0; std::string played; };
struct Row { std::string caso, progressao, tom, variante, veredito; Score s; };
struct ControlRow { std::string sessao, veredito; Score s; };
struct Control { std::string folder, cycle; int first, end; };

std::vector<Bar> song(const Case& c, int* bpm = nullptr);
Built build(const Case& c);
Score score(const Result& res, const Truth& truth, int key);
std::string verdict(const Score& s);
Row play(const Case& c);
std::vector<Case> cases(const std::vector<std::string>& progs, const std::vector<int>& keys,
                        const std::vector<std::string>& variants);
std::vector<Row> run_exam(const std::vector<Case>& selection);
std::vector<ControlRow> run_controls();
void print_exam(const std::vector<Row>& rows);
void print_controls(const std::vector<ControlRow>& rows);
fs::path save_exam(const std::vector<Row>& rows, const std::string& versao);
bool has_session(int control);      // is CONTROLS[control] on this machine? (sessoes/ is not versioned)
bool has_real_takes();
}  // namespace exam

// ------------------------------------------------------------------ outputs
void write_midi(const std::vector<Event>& events, const fs::path& path);
void plot(const Result& res, const fs::path& path, const Truth* truth, const std::string& title, double sr = SR);
void save_outputs(const Result& res, const fs::path& out_dir, const std::string& name, const Truth* truth,
                  const std::string& title, const fs::path& audio_dir, double sr = SR);
void write_text(const fs::path& path, const std::string& text);

std::vector<std::pair<std::string, Metrics>> evaluate_all(const std::vector<std::string>& names, const std::string& versao,
                                                          bool save);

}  // namespace lab
