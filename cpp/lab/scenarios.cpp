// The synthetic test scenarios: audio + ground truth.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

#include "lab.h"

namespace lab {

std::string truth_label(int lab) {
    if (lab < 0) return "None";
    if (lab == PAUSE) return "P";
    return label(lab);
}

static int parse_label(const std::string& s) {
    if (s == "N") return N_IDX;
    if (s == "P") return PAUSE;
    return chord_index(s);
}

int Bar::label() const { return chord ? chord_transpose(chord_index(*chord), capo) : N_IDX; }

std::vector<Bar> final_bar(const std::string& chord, double bpm, int capo, double gain) {
    Bar b;
    b.chord = chord, b.pattern = "D-------", b.bpm = bpm, b.capo = capo, b.gain = gain;
    return {b};
}

std::vector<double> pink_noise(size_t n, Rng& rng) {
    std::vector<double> x(n);
    for (double& v : x) v = rng.normal();
    return lfilter({0.049922035, -0.095993537, 0.050612699, -0.004408786}, {1, -2.494956002, 2.017265875, -0.522189400}, x);
}

// 'Singer': sine with vibrato on random C-major notes, one per beat.
static std::vector<double> voice(const std::vector<double>& beat_dur, Rng& rng) {
    static const int scale[8] = {60, 62, 64, 65, 67, 69, 71, 72};
    constexpr double TWO_PI = 2 * std::numbers::pi;
    std::vector<double> out;
    for (double d : beat_dur) {
        int n = (int)(d * SR);
        double f = 440 * std::pow(2.0, (scale[rng.integers(8)] - 69) / 12.0), cum = 0.0;
        for (int i = 0; i < n; ++i) {
            double t = (double)i / SR;
            cum += f * (1 + 0.017 * std::sin(TWO_PI * 5 * t));
            double ph = TWO_PI * cum / SR;
            double env = std::min(1.0, t / 0.05) * std::min(1.0, (d - t) / 0.05);
            out.push_back(env * (std::sin(ph) + 0.4 * std::sin(2 * ph) + 0.2 * std::sin(3 * ph)));
        }
    }
    return out;
}

static double rms_where(const std::vector<double>& x, const std::vector<char>& mask) {
    std::vector<double> sq;
    for (size_t i = 0; i < x.size(); ++i)
        if (mask[i]) sq.push_back(x[i] * x[i]);
    return std::sqrt(np_mean(sq));
}

Song render(const std::vector<Bar>& bars, uint32_t seed, const RenderOpts& o) {
    Rng rng(seed);
    // tempo map: one entry per beat
    std::vector<double> beats, beat_dur, downbeats;
    double t = o.lead;
    for (size_t i = 0; i < bars.size(); ++i) {
        double bpm0 = bars[i].bpm, nxt = i + 1 < bars.size() ? bars[i + 1].bpm : bpm0;
        downbeats.push_back(t);
        for (int b = 0; b < 4; ++b) {
            double bpm = bpm0 + (nxt - bpm0) * b / 4;
            beats.push_back(t);
            beat_dur.push_back(60.0 / bpm);
            t += 60.0 / bpm;
        }
    }
    double end_t = t, duration = end_t + o.tail;
    std::vector<double> eff;
    for (size_t i = 0; i < bars.size(); ++i) eff.push_back(downbeats[i] + bars[i].shift * beat_dur[4 * i]);

    Guitar g(duration, seed, o.cents);
    std::optional<std::string> cur_shape;
    std::vector<Seg> segments = {{0.0, o.lead, N_IDX}};
    for (size_t i = 0; i < bars.size(); ++i) {
        const Bar& bar = bars[i];
        double bar_end = i + 1 < bars.size() ? downbeats[i + 1] : end_t;
        if (!bar.chord) {
            if (cur_shape) g.mute(downbeats[i]);
            cur_shape.reset();
            segments.push_back({downbeats[i], bar_end, N_IDX});
            continue;
        }
        int lab = bar.label();
        double seg_start = downbeats[i];
        size_t next_bass = 0;
        for (int k = 0; k < (int)bar.pattern.size(); ++k) {
            char c = bar.pattern[(size_t)k];
            size_t bi = 4 * i + (size_t)(k / 2);
            double ts_grid = beats[bi] + (k % 2) * 0.5 * beat_dur[bi];
            double ts = ts_grid + rng.normal(0, o.jitter);
            size_t j = 0;
            for (size_t jj = 0; jj < bars.size(); ++jj)
                if (eff[jj] <= ts_grid + 1e-9) j = jj;
            bool own = j == i || !bars[j].chord;   // the guitarist may already be, or still be, on a neighbour bar
            const Bar& src = own ? bar : bars[j];
            std::string shape = own && src.shape2 && k >= 4 ? *src.shape2 : *src.chord;
            if (own && bar.wrong && k < 4) shape = *bar.wrong;
            if (own && bar.half && k >= 4) shape = *bar.half;
            if (bar.half && k == 4) {
                segments.push_back({seg_start, ts_grid, lab});
                lab = chord_transpose(chord_index(*bar.half), bar.capo), seg_start = ts_grid;
            }
            if (c == 'D' || c == 'U') {
                if (!cur_shape || shape != *cur_shape) {
                    g.mute(ts);
                    cur_shape = shape;
                }
                double accent = k % 2 == 0 && c == 'D' ? 1.0 : 0.7;
                double amp = bar.gain * accent * rng.uniform(0.85, 1.15);
                g.strum(ts, voicing(shape, src.capo), c, amp);
            } else if (c == 'x') {
                g.chk(ts, bar.gain * rng.uniform(0.8, 1.2));
            } else if (c == 'b') {
                g.single(ts, bar.bass_notes[next_bass++], bar.gain);
            } else if (c == 'm') {
                g.mute(ts);
                cur_shape.reset();
                if (!bar.late_entry) {
                    segments.push_back({seg_start, ts_grid, lab});
                    lab = PAUSE, seg_start = ts_grid;
                }
            }
        }
        segments.push_back({seg_start, bar_end, lab});
    }
    g.mute(end_t);
    segments.push_back({end_t, duration, N_IDX});

    std::vector<double> audio(g.out.begin(), g.out.begin() + (std::ptrdiff_t)(long long)(duration * SR));
    double peak = 0;
    for (double v : audio) peak = std::max(peak, std::abs(v));
    for (double& v : audio) v = v / (peak + 1e-9) * 0.5;
    std::vector<char> playing(audio.size(), 0);
    for (const auto& s : segments)
        if (s.lab != N_IDX && s.lab != PAUSE)
            std::fill(playing.begin() + std::min<std::ptrdiff_t>((std::ptrdiff_t)(s.s * SR), (std::ptrdiff_t)playing.size()),
                      playing.begin() + std::min<std::ptrdiff_t>((std::ptrdiff_t)(s.e * SR), (std::ptrdiff_t)playing.size()), 1);
    double ref = rms_where(audio, playing);
    if (o.noise_db) {
        auto n = pink_noise(audio.size(), rng);
        std::vector<double> sq(n.size());
        for (size_t i = 0; i < n.size(); ++i) sq[i] = n[i] * n[i];
        double rms = std::sqrt(np_mean(sq)), gain = std::pow(10.0, *o.noise_db / 20);
        for (size_t i = 0; i < n.size(); ++i) audio[i] += n[i] / rms * ref * gain;
    }
    if (o.voice_db) {
        auto sung = voice(beat_dur, rng);
        std::vector<double> v(audio.size(), 0.0);
        size_t lead = (size_t)(long long)(o.lead * SR);
        for (size_t i = 0; i < sung.size() && lead + i < v.size(); ++i) v[lead + i] = sung[i];
        for (size_t i = 0; i < v.size(); ++i) v[i] *= playing[i];
        double rms = rms_where(v, playing), gain = std::pow(10.0, *o.voice_db / 20);
        for (size_t i = 0; i < v.size(); ++i) audio[i] += v[i] / rms * ref * gain;
    }

    // merge segments and derive changes / ends
    std::vector<Seg> merged;
    for (const auto& s : segments) {
        if (s.e - s.s <= 1e-6) continue;
        if (!merged.empty() && merged.back().lab == s.lab) merged.back().e = s.e;
        else merged.push_back(s);
    }
    std::set<double> surprise_at;
    for (size_t i = 0; i < bars.size(); ++i)
        if (bars[i].surprise) surprise_at.insert(round_digits(downbeats[i], 6));
    Song song;
    Truth& tr = song.truth;
    int last = -1;
    for (size_t k = 0; k < merged.size(); ++k) {
        const Seg& s = merged[k];
        if (s.lab == N_IDX || s.lab == PAUSE) {
            if (s.lab == N_IDX && k > 0 && merged[k - 1].lab != N_IDX) {
                tr.ends.push_back(s.s);
                last = -1;
            }
            continue;
        }
        if (s.lab != last) tr.changes.push_back({s.s, last, s.lab, surprise_at.count(round_digits(s.s, 6)) > 0});
        last = s.lab;
    }
    tr.duration = duration, tr.beats = beats, tr.downbeats = downbeats, tr.chords = merged;
    tr.tempo_t = beats;
    for (double d : beat_dur) tr.tempo_bpm.push_back(60.0 / d);
    for (size_t i = 0; i < bars.size(); ++i)
        if (bars[i].chord) tr.intensity.push_back({downbeats[i], bars[i].gain});
    song.audio.assign(audio.begin(), audio.end());   // (double -> float: the rounding of the rest of the chain)
    return song;
}

// ------------------------------------------------------------------ the scenarios
static const std::vector<std::string> PROG = {"C", "G", "Am", "F"};

static Bar bar_of(const std::string& chord, double bpm, const std::string& pattern = "DUDUDUDU") {
    Bar b;
    b.chord = chord, b.bpm = bpm, b.pattern = pattern;
    return b;
}

static std::vector<Bar> loop(const std::vector<std::string>& prog, int cycles, double bpm, const std::string& pattern = "DUDUDUDU") {
    std::vector<Bar> bars;
    for (int i = 0; i < cycles; ++i)
        for (const auto& c : prog) bars.push_back(bar_of(c, bpm, pattern));
    return bars;
}

static void append(std::vector<Bar>& a, const std::vector<Bar>& b) { a.insert(a.end(), b.begin(), b.end()); }

static std::vector<Bar> silent(int n, double bpm) {
    Bar b;
    b.bpm = bpm;
    return std::vector<Bar>((size_t)n, b);
}

static std::vector<Bar> metro_bars(Rng& rng) {
    static const double shifts[4] = {-0.5, -0.25, 0.25, 0.4};
    auto bars = loop(PROG, 4, 110);
    for (size_t i = 1; i < bars.size(); ++i) bars[i].shift = shifts[rng.integers(4)];
    append(bars, final_bar("C", 110));
    return bars;
}

static std::vector<Bar> pausa_e_fim() {
    auto bars = loop(PROG, 2, 110);
    bars[7].pattern = "DUDUm---";
    append(bars, loop(PROG, 2, 110));
    append(bars, final_bar("C", 110));
    append(bars, silent(3, 110));
    append(bars, loop({"Am", "F", "C", "G"}, 2, 110));
    append(bars, final_bar("Am", 110));
    return bars;
}

static std::vector<Bar> nota_de_passagem() {
    auto bars = loop(PROG, 4, 100);
    bars[6].pattern = "DUDUbUDU", bars[6].bass_notes = {43};
    bars[10].shape2 = "Am/G";
    bars[13].pattern = "DUDUDUbb", bars[13].bass_notes = {45, 43};
    bars[15].pattern = "DUDUDUbb", bars[15].bass_notes = {43, 47};
    append(bars, final_bar("C", 100));
    return bars;
}

static std::vector<Bar> troca_de_tom() {
    auto bars = loop(PROG, 3, 110);
    append(bars, final_bar("C", 110));
    append(bars, silent(2, 110));
    auto d = loop(PROG, 3, 100);   // C-shapes with capo 2 = D A Bm G
    for (auto& b : d) b.capo = 2;
    auto e = loop({"E", "B", "C#m", "A"}, 3, 100);
    for (int i = 0; i < 4; ++i) d[(size_t)i].surprise = e[(size_t)i].surprise = true;
    append(bars, d);
    append(bars, e);
    append(bars, final_bar("E", 100));
    return bars;
}

static std::vector<Bar> acorde_fora_do_tom() {
    auto bars = loop(PROG, 2, 110);
    auto lap = [&](const char* second, const char* third, bool surprise2, bool surprise3) {
        std::vector<Bar> l = {bar_of("C", 110), bar_of(second, 110), bar_of(third, 110), bar_of("F", 110)};
        l[1].surprise = surprise2, l[2].surprise = surprise3;
        append(bars, l);
    };
    lap("G", "Em", false, true);
    lap("E", "Am", true, false);
    lap("Bb", "Am", true, false);
    append(bars, loop(PROG, 2, 110));
    append(bars, final_bar("C", 110));
    return bars;
}

static std::vector<Bar> dinamica() {
    auto gains = linspace(0.25, 1.0, 8), down = linspace(1.0, 0.25, 8);
    gains.insert(gains.end(), down.begin(), down.end());
    auto bars = loop(PROG, 4, 100);
    for (size_t i = 0; i < bars.size(); ++i) bars[i].gain = gains[i];
    append(bars, final_bar("C", 100, 0, 0.25));
    return bars;
}

// Third live session (2026-10-29): with the pattern learned, the musician's slips must not break it.
// Truth is the intended progression, not the slips.
static std::vector<Bar> erros_com_padrao() {
    auto bars = loop(PROG, 6, 100);
    bars[9].wrong = "Dm";                                   // half a bar of a wrong chord
    bars[13].shift = 1.25;                                  // changes to G 1.25 beats late
    bars[18].pattern = "m---DUDU", bars[18].late_entry = true;   // comes in 2 beats late on Am
    bars[21].wrong = "Em";                                  // wrong chord again on G
    append(bars, final_bar("C", 100));
    return bars;
}

static std::vector<Bar> with_final(std::vector<Bar> bars, const std::string& chord, double bpm) {
    append(bars, final_bar(chord, bpm));
    return bars;
}

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> all = [] {
        LabConfig livre_c;
        livre_c.tom = "C";
        LabConfig decorado = livre_c, feedback = livre_c, hold = livre_c, metronomo = livre_c;
        decorado.modo = "decorado", decorado.progressao = "C G Am F";
        // gain 0.1: with the accompaniment ~14 dB above the guitar, the return arrives ~6 dB below the guitar
        feedback.fb_gain = 0.1, feedback.fb_delay = 0.03;
        hold.manter_padrao = true;
        metronomo.modo = "metronomo", metronomo.bpm = 110;
        RenderOpts plain, noisy, loose;
        noisy.noise_db = -20, noisy.voice_db = -6;
        loose.jitter = 0.02;
        return std::vector<Scenario>{
            {"estavel_120", "C-G-Am-F em loop, 120 BPM constante", [](Rng&) { return with_final(loop(PROG, 4, 120), "C", 120); }, plain, livre_c},
            {"acelerando", "C-G-Am-F acelerando de 100 a 125 BPM", [](Rng&) {
                 std::vector<Bar> bars;
                 for (int i = 0; i < 20; ++i) bars.push_back(bar_of(PROG[(size_t)i % 4], 100 + 25.0 * i / 19));
                 return with_final(bars, "C", 125);
             }, plain, livre_c},
            {"desacelerando_final", "110 BPM com ritardando até ~65 no fim", [](Rng&) {
                 auto bars = loop(PROG, 3, 110);
                 double slow[4] = {100, 90, 80, 70};
                 for (int i = 0; i < 4; ++i) bars.push_back(bar_of(PROG[(size_t)i], slow[i]));
                 return with_final(bars, "C", 65);
             }, plain, livre_c},
            {"dinamica", "crescendo e diminuendo", [](Rng&) { return dinamica(); }, plain, livre_c},
            {"pausa_e_fim", "pausa de meio compasso, fim com silêncio longo, outra progressão depois", [](Rng&) { return pausa_e_fim(); }, plain, livre_c},
            {"nota_de_passagem", "Am com baixo em Sol e notas de passagem que não são troca", [](Rng&) { return nota_de_passagem(); }, plain, livre_c},
            {"troca_de_tom", "Dó -> silêncio -> Ré (capotraste 2) -> Mi sem silêncio", [](Rng&) { return troca_de_tom(); }, plain, livre_c},
            {"acorde_fora_do_tom", "Em, E e Bb no lugar do esperado, com progressão decorada", [](Rng&) { return acorde_fora_do_tom(); }, plain, decorado},
            {"batida_abafada", "palhetadas abafadas nos tempos 2 e 4", [](Rng&) { return with_final(loop(PROG, 4, 110, "DUxUDUxU"), "C", 110); }, plain, livre_c},
            {"ruido", "ruído rosa + 'voz' senoidal", [](Rng&) { return with_final(loop(PROG, 4, 100), "C", 100); }, noisy, livre_c},
            {"realimentacao", "a saída do sistema volta na entrada (30 ms, ~6 dB abaixo do violão)", [](Rng&) { return with_final(loop(PROG, 4, 110), "C", 110); }, plain, feedback},
            {"erros_com_padrao", "padrão travado; depois o músico erra um acorde, atrasa uma troca e entra atrasado", [](Rng&) { return erros_com_padrao(); }, plain, hold},
            // first live session (2026-10-29): I-V-vi-IV in B (guitar a half step down) with the key set to C
            {"tom_errado", "B-F#-G#m-E tocado com o tom configurado em Dó", [](Rng&) { return with_final(loop({"B", "F#", "G#m", "E"}, 4, 100), "B", 100); }, plain, livre_c},
            {"modo_metronomo", "trocas adiantadas/atrasadas contra um clique fixo de 110 BPM", metro_bars, loose, metronomo},
        };
    }();
    return all;
}

Song build_scenario(const std::string& name) {
    for (const auto& sc : scenarios()) {
        if (sc.name != name) continue;
        int seed = 0;
        for (unsigned char c : name) seed += c;
        Rng rng((uint32_t)seed);
        Song song = render(sc.bars(rng), (uint32_t)seed, sc.opts);
        song.truth.name = name, song.truth.description = sc.description, song.truth.seed = seed, song.truth.run = sc.run;
        return song;
    }
    throw std::invalid_argument("cenário desconhecido: " + name);
}

// ------------------------------------------------------------------ truth files
static std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

static std::string number_list(const std::vector<double>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + shortest_repr(v[i]);
    return s + "]";
}

std::string truth_to_json(const Truth& t) {
    std::ostringstream o;
    auto opt = [](const std::optional<std::string>& v) { return v ? quoted(*v) : std::string("null"); };
    o << "{\n \"duration\": " << shortest_repr(t.duration) << ",\n \"beats\": " << number_list(t.beats)
      << ",\n \"downbeats\": " << number_list(t.downbeats) << ",\n \"tempo\": [";
    for (size_t i = 0; i < t.tempo_t.size(); ++i) o << (i ? ", " : "") << number_list({t.tempo_t[i], t.tempo_bpm[i]});
    o << "],\n \"chords\": [";
    for (size_t i = 0; i < t.chords.size(); ++i)
        o << (i ? ", " : "") << "[" << shortest_repr(t.chords[i].s) << ", " << shortest_repr(t.chords[i].e) << ", " << quoted(truth_label(t.chords[i].lab)) << "]";
    o << "],\n \"changes\": [";
    for (size_t i = 0; i < t.changes.size(); ++i) {
        const auto& c = t.changes[i];
        o << (i ? ", " : "") << "{\"t\": " << shortest_repr(c.t) << ", \"from\": " << (c.from < 0 ? "null" : quoted(label(c.from)))
          << ", \"to\": " << quoted(label(c.to)) << ", \"surprise\": " << (c.surprise ? "true" : "false") << "}";
    }
    o << "],\n \"ends\": " << number_list(t.ends) << ",\n \"intensity\": [";
    for (size_t i = 0; i < t.intensity.size(); ++i) o << (i ? ", " : "") << number_list({t.intensity[i].first, t.intensity[i].second});
    o << "],\n \"name\": " << quoted(t.name) << ",\n \"description\": " << quoted(t.description) << ",\n \"seed\": " << t.seed
      << ",\n \"run\": {\"modo\": " << quoted(t.run.modo) << ", \"tom\": " << opt(t.run.tom)
      << ", \"bpm\": " << (t.run.bpm ? shortest_repr(*t.run.bpm) : "null") << ", \"progressao\": " << opt(t.run.progressao)
      << ", \"manter_padrao\": " << (t.run.manter_padrao ? "true" : "false");
    if (t.run.fb_gain != 0.0) o << ", \"feedback\": {\"gain\": " << shortest_repr(t.run.fb_gain) << ", \"delay\": " << shortest_repr(t.run.fb_delay) << "}";
    o << "}\n}\n";
    return o.str();
}

Truth truth_from_json(const Json& j) {
    Truth t;
    auto numbers = [&](const char* key, std::vector<double>& v) {
        if (const Json* a = j.get(key))
            for (const Json& x : a->a) v.push_back(x.n);
    };
    t.duration = j.get("duration")->n;
    numbers("beats", t.beats);
    numbers("downbeats", t.downbeats);
    numbers("ends", t.ends);
    for (const Json& p : j.get("tempo")->a) t.tempo_t.push_back(p.a[0].n), t.tempo_bpm.push_back(p.a[1].n);
    for (const Json& c : j.get("chords")->a) t.chords.push_back({c.a[0].n, c.a[1].n, parse_label(c.a[2].s)});
    for (const Json& c : j.get("changes")->a) {
        const Json* from = c.get("from");
        const Json* surprise = c.get("surprise");
        t.changes.push_back({c.get("t")->n, from ? parse_label(from->s) : -1, parse_label(c.get("to")->s), surprise && surprise->b});
    }
    if (const Json* a = j.get("intensity"))
        for (const Json& p : a->a) t.intensity.push_back({p.a[0].n, p.a[1].n});
    if (const Json* s = j.get("name")) t.name = s->s;
    if (const Json* s = j.get("description")) t.description = s->s;
    if (const Json* r = j.get("run")) t.run = config_from_json(*r);
    return t;
}

}  // namespace lab
