// Kordia core: the whole accompaniment system, one block at a time.
// Line-by-line port of the Python reference in acompanhante/ (same names, same order of operations).
// The Python package stays the lab: scenarios, metrics and reports run this core through the
// kordia_core bindings and must give the same numbers (tests/test_cpp.py).
#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kordia {

// ------------------------------------------------------------------ parameters
// Mirrors acompanhante/params.py (checked by tests/test_cpp.py::test_params_match).
namespace P {
constexpr int HOP = 512, ONSET_WIN = 1024, CHROMA_WIN = 4096;
constexpr double ONSET_FMIN = 150.0, ONSET_FMAX = 5000.0;
constexpr int ONSET_MEDIAN_FRAMES = 32;
constexpr double ONSET_K = 1.5, ONSET_DELTA = 0.6, ONSET_REFRACTORY = 0.10, SILENCE_DB = -55.0;
constexpr double TEMPO_ENV_SECONDS = 8.0;
constexpr int TEMPO_ACF_EVERY = 10;
constexpr double TEMPO_BPM_MIN = 50.0, TEMPO_BPM_MAX = 200.0, TEMPO_PRIOR_BPM = 105.0, TEMPO_PRIOR_OCTAVES = 0.7;
constexpr int TEMPO_MIN_ONSETS = 6;
constexpr double PLL_WINDOW = 0.2, PLL_ALPHA = 0.35, PLL_BETA = 0.10, TEMPO_RESET_DEV = 0.12;
constexpr int TEMPO_CONF_BEATS = 8, BEATS_PER_BAR = 4;
constexpr double CHROMA_FMIN = 60.0, CHROMA_FMAX = 2500.0, CHROMA_GAMMA = 100.0;
constexpr int TEMPLATE_HARMONICS = 6;
constexpr double TEMPLATE_DECAY = 0.6;
constexpr double HMM_BETA = 18.0, HMM_N_SCORE = 0.55, HMM_P_CHANGE = 0.002, HMM_P_CHANGE_ONSET = 0.02,
                 HMM_P_CHANGE_STRONG = 0.08, HMM_ONSET_BOOST_TIME = 0.25, KEY_PRIOR_OUT = 0.3;
constexpr double CONF_BETA = 25.0, CONF_SMOOTH = 0.3;
constexpr double CONF_HIGH = 0.70, CONF_LOW = 0.45, CHANGE_AFTER_ONSET = 0.35, DOUBT_TO_FALLBACK_BARS = 1.0,
                 STOP_BEATS = 1.5, STOP_IOI_FACTOR = 1.25, STOP_DEFAULT_SECONDS = 1.0, FORGET_BARS = 2.0;
constexpr double LOCK_CONFIRM_BEATS = 0.5;
constexpr int LOCK_MAX_MISSES = 3;
constexpr double LOCK_CONTRA_BEATS = 0.5, LOCK_LATE_BEATS = 1.0;
constexpr int CYCLE_MIN = 2, CYCLE_MAX = 8;
constexpr double METRO_LATE_BEATS = 0.5;
constexpr double DYN_DB_LOW = -45.0, DYN_DB_HIGH = -12.0;
constexpr int DYN_VEL_LOW = 60, DYN_VEL_HIGH = 120;
constexpr double DYN_SMOOTH = 0.25, DYN_SILENCE_DECAY = 1.5;
constexpr int BASS_LOW = 36, PAD_ROOT_LOW = 48;
constexpr double PAD_SHIMMER = 0.35;
constexpr double PAD_ATTACK = 0.9, PAD_RELEASE = 2.2, PAD_CUTOFF = 1800.0, PAD_LEVEL = 0.16, PAD_DRY = 0.75,
                 PAD_WET = 0.6, REVERB_ROOM = 0.86, REVERB_DAMP = 0.35, BASS_LEVEL = 0.45, BASS_RELEASE = 0.15,
                 CLICK_LEVEL = 0.5, STOP_FADE = 0.6;
}  // namespace P

constexpr double INF = std::numeric_limits<double>::infinity();

// Python semantics helpers: round() is half-to-even, % is always non-negative.
inline long long pyround(double x) { return (long long)std::nearbyint(x); }
inline long long pymod(long long a, long long m) { long long r = a % m; return r < 0 ? r + m : r; }

// ------------------------------------------------------------------ theory
constexpr int N_IDX = 24;  // vocabulary: 0..11 major, 12..23 minor, 24 = no chord
std::string label(int idx);
int note_pc(const std::string& name);            // throws std::invalid_argument
int chord_index(const std::string& chord);       // 'Am', 'F#m', 'G7', 'Am/G' -> canonical triad index
std::array<int, 3> chord_pcs(int idx);           // root, third, fifth
std::array<std::array<double, 12>, 24> templates();
std::array<double, 25> key_prior(std::optional<int> key_pc);

// ------------------------------------------------------------------ config
struct RunConfig {
    std::string modo = "livre";        // livre | metronomo | decorado
    std::optional<std::string> tom;    // main key, e.g. "C"
    std::optional<double> bpm;         // metronome mode
    std::optional<std::string> progressao;
    bool clique = false;
};

// ------------------------------------------------------------------ analysis
class Spectrum {  // windowed real FFT, magnitudes normalized so a sinusoid of amplitude A reads A
public:
    explicit Spectrum(int n);
    const std::vector<double>& operator()(const double* x);
private:
    int n_;
    std::vector<double> win_, buf_, mag_;
    std::vector<std::complex<double>> spec_;
    double norm_;
};

struct Onset { double t, db, flux; };

class OnsetDetector {
public:
    explicit OnsetDetector(double sr);
    std::optional<Onset> process(const std::vector<double>& mag, double t, double rms_db);
    double flux = 0.0;
private:
    std::vector<int> band_;
    std::vector<double> prev_, lm_;
    bool has_prev_ = false;
    std::deque<double> hist_;
    double last_t_ = -INF, prev_flux_ = 0.0;
};

class ChordTracker {
public:
    ChordTracker(double sr, std::optional<int> key_pc);
    struct Result { int idx; double conf; };
    Result process(const std::vector<double>& mag, bool silent, double p_change);
    std::array<double, 25> post{}, evidence{};
private:
    std::vector<int> band_, semi_;    // FFT bin -> semitone slot
    std::vector<int> semi_pc_;        // semitone slot -> pitch class
    std::vector<double> semis_;
    std::array<std::array<double, 12>, 24> T_;
    std::array<double, 25> prior_;
};

// ------------------------------------------------------------------ tempo
class Grid {  // shared beat-grid bookkeeping (Python: tempo._Grid)
public:
    virtual ~Grid() = default;
    virtual void reset();
    virtual void update(double t, double flux, const std::optional<Onset>& onset) = 0;
    virtual double confidence() const;
    virtual bool frozen() const { return false; }
    bool has_period() const { return period > 0; }
    std::optional<double> bpm() const { return period > 0 ? std::optional<double>(60.0 / period) : std::nullopt; }
    double beat_phase(double t) const { return next_index - (next_beat - t) / period; }
    double time_of_beat(long long idx) const { return next_beat + (idx - next_index) * period; }
    long long bar_pos(long long idx) const { return pymod(idx - bar_offset, P::BEATS_PER_BAR); }
    double next_downbeat(double t) const;
    bool is_strong(double t, double tol = 0.25) const;
    void observe_change(double t);

    double period = 0.0;  // 0 = unknown (Python: None)
    double next_beat = 0.0;
    long long next_index = 0, bar_offset = 0;
    std::optional<double> first_onset;
    double now = 0.0;
protected:
    void advance(double t);
    void start(double t0, double period);
    std::deque<double> onsets_, pending_;
    std::deque<bool> hits_;
    std::deque<long long> votes_;
};

class TempoTracker : public Grid {
public:
    explicit TempoTracker(double frame);
    void reset() override;
    void update(double t, double flux, const std::optional<Onset>& onset) override;
    bool frozen() const override { return frozen_; }
    void set_frozen(bool f) { frozen_ = f && period > 0; }
private:
    void pll(double to);
    void acf();
    double best_phase(const std::vector<double>& recent, double period) const;
    double frame_;
    bool frozen_ = false;
    std::deque<double> env_;
    size_t env_max_;
    long long frames_ = 0;
    int disagree_ = 0;
};

class FixedGrid : public Grid {
public:
    FixedGrid(double bpm, std::optional<double> start);
    void reset() override;
    void update(double t, double flux, const std::optional<Onset>& onset) override;
    double confidence() const override { return period > 0 ? 1.0 : 0.0; }
private:
    double fixed_period_;
    std::optional<double> start_;
};

// ------------------------------------------------------------------ progression memory
class ProgressionMemory {
public:
    explicit ProgressionMemory(const std::optional<std::string>& progression);
    void reset();
    void push(int idx, long long beat, bool blip = false);
    void correct(int idx);
    std::optional<std::pair<int, long long>> predict() const;
    void advance(long long beat);
    void unlock() { locked = false; pos = -1; }

    bool locked = false;
    int pos = -1;
    std::vector<int> known, lengths;   // empty = none
private:
    void try_lock();
    std::vector<int> fixed_;
    std::vector<std::pair<int, long long>> seq_;   // (chord, start beat)
};

// ------------------------------------------------------------------ decision engine
enum class State { Ocioso, Ouvindo, Travado, Fallback, Encerrando };
enum class Level { Confiante, Duvida, Perdido };
enum class Mode { Silence, Full, Root5, Tonic };
const char* state_name(State s);
const char* level_name(Level l);
const char* mode_name(Mode m);   // "" for silence (Python: None)

struct Output { int chord; Mode mode; double t; };   // chord -1 = none

class Engine {
public:
    Engine(Grid& tracker, ProgressionMemory& memory, std::optional<int> key_pc, bool metronome, double lookahead);
    std::optional<Output> step(double t, const std::optional<Onset>& onset, int idx, double conf,
                               const std::array<double, 25>& evidence);
    State state = State::Ocioso;
    Level level = Level::Perdido;
    std::optional<double> last_onset;
    std::pair<int, Mode> cur{-1, Mode::Silence};
    std::string reason;
    std::optional<int> key_pc;
    bool metronome;
private:
    void forget();
    double bar() const { return tracker_.has_period() ? P::BEATS_PER_BAR * tracker_.period : 2.0; }
    std::optional<Output> set(int chord, Mode mode, double t, const char* why = nullptr);
    std::optional<Output> set(int chord, Mode mode, double t, const std::string& why) { return set(chord, mode, t, why.c_str()); }
    double stop_gap() const;
    double change_time(double t) const;
    std::optional<Output> reactive(double t, int idx, const std::array<double, 25>& evidence);
    std::optional<Output> reactive_change(double t, int idx);
    std::optional<Output> locked(double t, int idx);

    Grid& tracker_;
    ProgressionMemory& memory_;
    double lookahead_, frame_;
    std::deque<double> iois_;
    std::optional<std::pair<int, double>> pending_;
    std::optional<double> doubt_since_;
    double confirm_at_ = 0.0, changed_at_ = 0.0, contra_since_ = 0.0, last_change_t_ = -INF, stop_at_ = 0.0;
    std::optional<int> expected_, prev_chord_, contra_;
    int misses_ = 0;
public:
    void set_frame(double f) { frame_ = f; }
};

// ------------------------------------------------------------------ accompaniment
enum class Inst { Bass, Pad, Click, All };
enum class EvType { On, Off, Expr };
struct Event { double t; EvType type; Inst inst; int pitch; int vel; double value = 0.0; double rel = -1.0; };
struct Change { double t; int chord; Mode mode; };

class Dynamics {
public:
    void strum(double db) { level_ = level_ ? *level_ + P::DYN_SMOOTH * (db - *level_) : db; }
    int velocity() const;
    double expression(double t, std::optional<double> last_onset, double period) const;
    bool fixed = false;
private:
    std::optional<double> level_;
};

class Accompanist {
public:
    explicit Accompanist(std::optional<int> key_pc) : key_pc_(key_pc) {}
    void apply(const Output& out, int vel, std::vector<Event>& evs);
    void beat(double t, bool strong, double period, int vel, std::vector<Event>& evs);
    void click(double t, bool accent, int vel, std::vector<Event>& evs);
    void expression(double t, double value, std::vector<Event>& evs);
    std::vector<Event> events;    // history (bounded by the owner in live mode)
    std::vector<Change> changes;
private:
    Event& ev(double t, EvType type, Inst inst, int pitch = 0, int vel = 0);
    std::optional<int> key_pc_;
    int bass_ = -1;
    std::vector<int> pad_;
    double last_bass_t_ = -INF, expr_ = 1.0;
};

// ------------------------------------------------------------------ synth
class Synth {
public:
    explicit Synth(double sr);
    void handle(const Event& e);
    void render(double t0, int n, double* left, double* right);  // adds nothing: overwrites
private:
    struct Voice { Inst inst; int pitch, vel; double on, off, rel; };   // off < 0: held
    struct Comb { int D; double g, d; std::vector<double> buf; int i = 0; double process(double x); };
    struct Allpass { int D; double g; std::vector<double> xb, yb; int i = 0; double process(double x); };
    struct Channel { std::vector<Comb> combs; std::vector<Allpass> aps; };
    double release(const Voice& v) const;
    const std::vector<double>& saw(double f);
    double sr_;
    std::vector<Voice> voices_;
    std::map<int, std::vector<double>> tables_;
    double expr_from_ = 1.0, expr_to_ = 1.0, expr_t_ = 0.0;
    double b0_, b1_, b2_, a1_, a2_, z1_ = 0.0, z2_ = 0.0;
    Channel ch_[2];
    std::vector<double> bass_, pad_, click_;
};

// ------------------------------------------------------------------ processor
struct Frame {   // per-block log row (Python: Processor.log)
    double t, db, flux;
    bool onset;
    double bpm, tempo_conf;    // bpm NaN when unknown
    int heard;
    double conf;
    State state;
    Level level;
    double expr;
    int vel;
};

struct Decision { double t; State state; int chord; Mode mode; std::string reason; };

class Processor {
public:
    // sr: analysis rate (22050 in the lab; device rate / 2 live). out_sr: synth rate.
    Processor(const RunConfig& cfg, double lookahead = 0.0, double sr = 22050.0, double out_sr = 0.0);
    // Output block for synth time t0 (structural latency: decided by earlier analyze() calls).
    void render(double t0, int n, double* left, double* right) { synth_.render(t0, n, left, right); }
    // Consume one block of HOP input samples at the analysis rate.
    void analyze(const double* x);
    double time() const { return s_ / sr_; }   // analysis time (end of last block)
    void freeze_tempo(bool on);
    std::optional<int> next_chord() const;
    std::optional<int> beat_in_bar() const;

    Frame frame{};
    std::vector<Decision> decisions;
    long long n_decisions = 0;
    std::vector<std::pair<double, double>> onset_list;
    bool click;
    int click_vel = 100;
    const Engine& engine() const { return engine_; }
    const Grid& tracker() const { return *tracker_; }
    const ProgressionMemory& memory() const { return memory_; }
    Accompanist& acc() { return acc_; }
    Dynamics& dyn() { return dyn_; }
    const std::array<double, 25>& evidence() const { return chords_.evidence; }
private:
    double sr_, lookahead_;
    long long s_ = 0;
    std::vector<double> buf_;
    Spectrum spec_onset_, spec_chroma_;
    std::unique_ptr<Grid> tracker_;
    ProgressionMemory memory_;
    Engine engine_;
    OnsetDetector onsets_;
    ChordTracker chords_;
    Accompanist acc_;
    Dynamics dyn_;
    Synth synth_;
    double last_onset_t_ = -INF, last_click_t_ = -INF;
    bool last_onset_strong_ = false;
    std::vector<Event> evs_;
};

}  // namespace kordia
