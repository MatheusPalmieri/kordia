// What a run leaves on disk: mixed WAV, MIDI, events and one SVG with waveform + onsets, tempo, chords (truth / heard /
// played), state and dynamics.
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <sstream>
#include <stdexcept>

#include "lab.h"

namespace lab {

static void vlq(std::string& out, uint32_t value) {
    std::string bytes(1, (char)(value & 0x7F));
    while (value >>= 7) bytes.insert(bytes.begin(), (char)((value & 0x7F) | 0x80));
    out += bytes;
}

static void be32(std::string& s, uint32_t v) { for (int k = 24; k >= 0; k -= 8) s += (char)(v >> k); }
static void be16(std::string& s, uint32_t v) { s += (char)(v >> 8), s += (char)v; }

// Type-0 MIDI: bass ch 1 (finger bass), pad ch 2 (warm pad), click ch 10 (wood block), CC11 = expression.
void write_midi(const std::vector<Event>& events, const fs::path& path) {
    constexpr uint32_t TPQ = 480, US_PER_Q = 500000;   // 120 BPM: 960 ticks per second
    using Msg = std::pair<long long, std::string>;
    std::vector<Msg> msgs = {{0, {(char)0xC0, 33}}, {0, {(char)0xC1, 89}}};
    auto channel = [](Inst i) { return i == Inst::Bass ? 0 : i == Inst::Pad ? 1 : 9; };
    for (const auto& e : events) {
        long long tick = round_even(e.t * TPQ * 1e6 / US_PER_Q);
        if (e.type == EvType::On) {
            msgs.push_back({tick, {(char)(0x90 | channel(e.inst)), (char)e.pitch, (char)std::max(1, e.vel)}});
            if (e.inst == Inst::Click) msgs.push_back({tick + 24, {(char)0x89, (char)e.pitch, 0}});
        } else if (e.type == EvType::Off) {
            msgs.push_back({tick, {(char)(0x80 | channel(e.inst)), (char)e.pitch, 0}});
        } else {
            char v = (char)round_even(127 * e.value);
            msgs.push_back({tick, {(char)0xB0, 11, v}});
            msgs.push_back({tick, {(char)0xB1, 11, v}});
        }
    }
    std::stable_sort(msgs.begin(), msgs.end(), [](const Msg& a, const Msg& b) { return a.first < b.first; });   // offs stay first
    std::string data = {0, (char)0xFF, 0x51, 3};
    data += (char)(US_PER_Q >> 16), data += (char)(US_PER_Q >> 8), data += (char)US_PER_Q;
    long long last = 0;
    for (const auto& [tick, m] : msgs) {
        vlq(data, (uint32_t)(tick - last));
        data += m;
        last = tick;
    }
    data += std::string{0, (char)0xFF, 0x2F, 0};
    std::string file = "MThd";
    be32(file, 6), be16(file, 0), be16(file, 1), be16(file, TPQ);
    file += "MTrk";
    be32(file, (uint32_t)data.size());
    write_text(path, file + data);
}

// ------------------------------------------------------------------ SVG report
namespace {
constexpr int W = 1400, LEFT = 80, RIGHT = 20, TOP = 34, GAP = 16;
constexpr int PLOT_W = W - LEFT - RIGHT;

struct Panel { int y, h; };

class Svg {
public:
    Svg(double end, int height) : end_(end) {
        o_ << std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\" font-family=\"sans-serif\" font-size=\"11\">\n"
                          "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n", W, height);
    }
    double x(double t) const { return LEFT + t / end_ * PLOT_W; }
    void frame(const Panel& p, const std::string& name) {
        o_ << std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"none\" stroke=\"#bbb\"/>\n", LEFT, p.y, PLOT_W, p.h);
        text(6, p.y + p.h / 2.0, name, "#333");
    }
    void text(double px, double py, const std::string& s, const std::string& color, const char* anchor = "start") {
        std::string safe;
        for (char c : s) {
            if (c == '<') safe += "&lt;";
            else if (c == '&') safe += "&amp;";
            else safe += c;
        }
        o_ << std::format("<text x=\"{:.1f}\" y=\"{:.1f}\" fill=\"{}\" text-anchor=\"{}\">{}</text>\n", px, py, color, anchor, safe);
    }
    void vline(double t, const Panel& p, const std::string& color, double width, double opacity = 1.0) {
        o_ << std::format("<path d=\"M{:.1f} {}V{}\" stroke=\"{}\" stroke-width=\"{}\" opacity=\"{}\"/>\n", x(t), p.y, p.y + p.h, color, width, opacity);
    }
    void line(const std::vector<std::pair<double, double>>& pts, const std::string& color, double width = 1.0, bool dashed = false) {
        if (pts.empty()) return;
        o_ << "<path fill=\"none\" d=\"M";
        for (const auto& [px, py] : pts) o_ << std::format("{:.1f} {:.1f} ", px, py);
        o_ << std::format("\" stroke=\"{}\" stroke-width=\"{}\"{}/>\n", color, width, dashed ? " stroke-dasharray=\"5 3\"" : "");
    }
    void area(const std::vector<std::pair<double, double>>& pts, const std::string& color) {
        o_ << "<path d=\"M";
        for (const auto& [px, py] : pts) o_ << std::format("{:.1f} {:.1f} ", px, py);
        o_ << std::format("Z\" fill=\"{}\"/>\n", color);
    }
    void box(double t0, double t1, double y, double h, const std::string& color) {
        o_ << std::format("<rect x=\"{:.1f}\" y=\"{:.1f}\" width=\"{:.1f}\" height=\"{:.1f}\" fill=\"{}\" stroke=\"#fff\" stroke-width=\"0.5\"/>\n",
                          x(t0), y, x(t1) - x(t0), h, color);
    }
    std::string finish() { o_ << "</svg>\n"; return o_.str(); }
private:
    double end_;
    std::ostringstream o_;
};

// one color per chord (matplotlib's tab20), grey for no chord
std::string chord_color(int lab) {
    static const char* tab20[20] = {"#1f77b4", "#aec7e8", "#ff7f0e", "#ffbb78", "#2ca02c", "#98df8a", "#d62728", "#ff9896", "#9467bd", "#c5b0d5",
                                    "#8c564b", "#c49c94", "#e377c2", "#f7b6d2", "#7f7f7f", "#c7c7c7", "#bcbd22", "#dbdb8d", "#17becf", "#9edae5"};
    return lab < 0 || lab >= N_IDX ? "#dddddd" : tab20[lab % 20];
}

struct Span { double s, e; int lab; std::string text; };

void lane(Svg& svg, const Panel& p, int row, const std::vector<Span>& spans) {
    double h = p.h / 3.0, y = p.y + row * h;
    for (const auto& s : spans) {
        if (s.e <= s.s) continue;
        svg.box(s.s, s.e, y + 3, h - 6, chord_color(s.lab));
        if (!s.text.empty() && s.e - s.s > 0.8) svg.text((svg.x(s.s) + svg.x(s.e)) / 2, y + h / 2 + 4, s.text, "#000", "middle");
    }
}
}  // namespace

void plot(const Result& res, const fs::path& path, const Truth* truth, const std::string& title, double sr) {
    const auto& log = res.log;
    if (log.empty()) throw std::runtime_error("sessão curta demais para o relatório");
    double end = log.back().t;
    const Panel audio{TOP, 170}, tempo{audio.y + audio.h + GAP, 130}, chords{tempo.y + tempo.h + GAP, 180};
    const Panel state{chords.y + chords.h + GAP, 110}, dyn{state.y + state.h + GAP, 110};
    int height = dyn.y + dyn.h + 40;
    Svg svg(end, height);
    svg.text(W / 2.0, 20, title, "#000", "middle");

    // 1 waveform + onsets
    svg.frame(audio, "áudio");
    size_t hop = std::max<size_t>(1, res.audio.size() / (size_t)PLOT_W);
    std::vector<std::pair<double, double>> top, bottom;
    double mid = audio.y + audio.h / 2.0, peak = 1e-9;
    std::vector<double> env;
    for (size_t i = 0; i + hop <= res.audio.size(); i += hop) {
        double v = 0;
        for (size_t k = i; k < i + hop; ++k) v = std::max(v, std::abs(res.audio[k]));
        env.push_back(v), peak = std::max(peak, v);
    }
    for (size_t i = 0; i < env.size(); ++i) {
        double px = svg.x((double)(i * hop) / sr), dy = env[i] / peak * (audio.h / 2.0 - 2);
        top.push_back({px, mid - dy}), bottom.push_back({px, mid + dy});
    }
    top.insert(top.end(), bottom.rbegin(), bottom.rend());
    svg.area(top, "#888888");
    for (const auto& [t, db] : res.onsets) svg.vline(t, audio, "#d62728", 0.6, 0.6);
    if (truth)
        for (double d : truth->downbeats) svg.vline(d, audio, "#1f77b4", 0.7, 0.7);

    // 2 tempo
    svg.frame(tempo, "BPM");
    double lo = INF, hi = -INF;
    for (const auto& f : log)
        if (!std::isnan(f.bpm)) lo = std::min(lo, f.bpm), hi = std::max(hi, f.bpm);
    if (truth)
        for (double b : truth->tempo_bpm) lo = std::min(lo, b), hi = std::max(hi, b);
    if (lo <= hi) {
        lo -= 5, hi += 5;
        auto y = [&](double bpm) { return tempo.y + tempo.h - (bpm - lo) / (hi - lo) * tempo.h; };
        std::vector<std::pair<double, double>> pts;
        for (const auto& f : log)
            if (!std::isnan(f.bpm)) pts.push_back({svg.x(f.t), y(f.bpm)});
        svg.line(pts, "#ff7f0e", 1.2);
        svg.text(LEFT - 4, tempo.y + 12, std::format("{:.0f}", hi), "#555", "end");
        svg.text(LEFT - 4, tempo.y + tempo.h, std::format("{:.0f}", lo), "#555", "end");
        if (truth) {
            pts.clear();
            for (size_t i = 0; i < truth->tempo_t.size(); ++i) pts.push_back({svg.x(truth->tempo_t[i]), y(truth->tempo_bpm[i])});
            svg.line(pts, "#000", 1.0, true);
        }
    }

    // 3 chords: truth / heard / played
    svg.frame(chords, "acordes");
    std::vector<Span> heard;
    for (const auto& f : log)
        if (heard.empty() || heard.back().lab != f.heard) {
            if (!heard.empty()) heard.back().e = f.t;
            heard.push_back({f.t, end, f.heard, f.heard == N_IDX ? "" : label(f.heard)});
        }
    std::vector<Span> played;
    for (size_t i = 0; i < res.changes.size(); ++i) {
        const Change& c = res.changes[i];
        double next = i + 1 < res.changes.size() ? res.changes[i + 1].t : end;
        bool named = c.mode == Mode::Full || c.mode == Mode::Root5;   // 'tonic' fallback and silence are drawn grey
        played.push_back({c.t, next, named ? c.chord : -1, named ? label(c.chord) + (c.mode == Mode::Root5 ? "5" : "") : ""});
    }
    if (truth) {
        std::vector<Span> real;
        for (const auto& s : truth->chords) real.push_back({s.s, s.e, s.lab, s.lab == PAUSE ? "P" : s.lab == N_IDX ? "" : label(s.lab)});
        lane(svg, chords, 0, real);
    }
    lane(svg, chords, 1, heard);
    lane(svg, chords, 2, played);
    svg.text(LEFT + 3, chords.y + 11, "verdade", "#444");
    svg.text(LEFT + 3, chords.y + chords.h / 3.0 + 11, "ouvido", "#444");
    svg.text(LEFT + 3, chords.y + 2 * chords.h / 3.0 + 11, "tocado", "#444");

    // 4 state machine + confidence
    svg.frame(state, "estado");
    std::vector<std::pair<double, double>> steps, confidence;
    for (const auto& f : log) {
        double y = state.y + state.h - 6 - (double)f.state / 4.0 * (state.h - 12);
        if (!steps.empty()) steps.push_back({svg.x(f.t), steps.back().second});
        steps.push_back({svg.x(f.t), y});
        confidence.push_back({svg.x(f.t), state.y + state.h - f.conf * state.h});
    }
    svg.line(confidence, "#2ca02c", 0.5);
    svg.line(steps, "#9467bd", 1.2);
    for (int s = 0; s < 5; ++s) svg.text(LEFT + 3, state.y + state.h - 2 - s / 4.0 * (state.h - 12), state_name((State)s), "#777");

    // 5 dynamics
    svg.frame(dyn, "dinâmica");
    auto dy = [&](double v) { return dyn.y + dyn.h - v / 1.1 * dyn.h; };
    std::vector<std::pair<double, double>> vel, expr;
    for (const auto& f : log) vel.push_back({svg.x(f.t), dy(f.vel / 127.0)}), expr.push_back({svg.x(f.t), dy(f.expr)});
    svg.line(expr, "#7f7f7f", 0.8);
    svg.line(vel, "#8c564b", 1.2);
    if (truth && !truth->intensity.empty()) {
        std::vector<std::pair<double, double>> pts;
        for (const auto& [t, gain] : truth->intensity) {
            if (!pts.empty()) pts.push_back({svg.x(t), pts.back().second});
            pts.push_back({svg.x(t), dy(gain)});
        }
        svg.line(pts, "#000", 0.8, true);
    }
    svg.text(LEFT + 3, dyn.y + 12, "velocity (marrom), expressão (cinza)", "#555");

    for (int t = 0; t <= (int)end; t += std::max(1, (int)end / 20)) svg.text(svg.x(t), dyn.y + dyn.h + 14, std::to_string(t), "#555", "middle");
    svg.text(W / 2.0, dyn.y + dyn.h + 30, "tempo (s)", "#333", "middle");
    write_text(path, svg.finish());
}

static std::string events_json(const Result& res) {
    static const char* types[] = {"on", "off", "expr"};
    static const char* insts[] = {"bass", "pad", "click", "all"};
    std::ostringstream o;
    o << "{\"changes\": [";
    for (size_t i = 0; i < res.changes.size(); ++i) {
        const Change& c = res.changes[i];
        o << (i ? ", " : "") << "[" << shortest_repr(c.t) << ", " << (c.chord < 0 ? "null" : "\"" + label(c.chord) + "\"") << ", "
          << (c.mode == Mode::Silence ? "null" : "\"" + std::string(mode_name(c.mode)) + "\"") << "]";
    }
    o << "], \"events\": [";
    for (size_t i = 0; i < res.events.size(); ++i) {
        const Event& e = res.events[i];
        o << (i ? ", " : "") << "{\"t\": " << shortest_repr(e.t) << ", \"type\": \"" << types[(int)e.type] << "\", \"inst\": \"" << insts[(int)e.inst]
          << "\", \"pitch\": " << e.pitch << ", \"vel\": " << e.vel;
        if (e.type == EvType::Expr) o << ", \"value\": " << shortest_repr(e.value);
        if (e.rel >= 0) o << ", \"rel\": " << shortest_repr(e.rel);
        o << "}";
    }
    o << "]}";
    return o.str();
}

void save_outputs(const Result& res, const fs::path& out_dir, const std::string& name, const Truth* truth, const std::string& title,
                  const fs::path& audio_dir, double sr) {
    fs::create_directories(out_dir);
    fs::create_directories(audio_dir);
    std::vector<double> mix(res.accomp.size());
    double peak = 1.0;
    for (size_t i = 0; i < mix.size(); ++i) {
        mix[i] = res.audio[i / 2] + res.accomp[i];
        peak = std::max(peak, std::abs(mix[i]));
    }
    for (double& v : mix) v /= peak;
    write_wav(audio_dir / (name + "_render.wav"), mix, 2, sr);
    write_midi(res.events, audio_dir / (name + ".mid"));
    write_text(audio_dir / (name + "_eventos.json"), events_json(res));
    plot(res, out_dir / (name + ".svg"), truth, title, sr);
}

std::vector<std::pair<std::string, Metrics>> evaluate_all(const std::vector<std::string>& names, const std::string& versao, bool save) {
    std::vector<std::pair<std::string, Metrics>> rows(names.size());
    fs::path out = fs::path("resultados") / versao;
    parallel_for(names.size(), [&](size_t i) {
        Song s = build_scenario(names[i]);
        Result res = run(std::vector<double>(s.audio.begin(), s.audio.end()), s.truth.run);
        rows[i] = {names[i], evaluate(res, s.truth)};
        if (save) save_outputs(res, out / "relatorios", names[i], &s.truth, names[i] + " — " + s.truth.description, out / "audio");
    });
    if (save) {
        fs::create_directories(out);
        std::string json = "{\n", csv = "cenario";
        for (const auto& [key, v] : rows[0].second) csv += "," + key;
        csv += "\r\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            json += " \"" + rows[i].first + "\": {\n";
            csv += rows[i].first;
            const Metrics& m = rows[i].second;
            for (size_t k = 0; k < m.size(); ++k) {
                json += "  \"" + m[k].first + "\": " + val_json(m[k].second) + (k + 1 < m.size() ? ",\n" : "\n");
                csv += "," + val_csv(m[k].second);
            }
            json += i + 1 < rows.size() ? " },\n" : " }\n";
            csv += "\r\n";
        }
        write_text(out / "metricas.json", json + "}");
        write_text(out / "metricas.csv", csv);
    }
    return rows;
}

}  // namespace lab
