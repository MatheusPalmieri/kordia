// WAV, JSON and session folders: what the lab reads and writes, with no dependency outside the standard library.
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "lab.h"

namespace lab {

// ------------------------------------------------------------------ WAV
template <typename T>
static T read_le(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

Wav read_wav(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("não abri " + path.string());
    std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || b.compare(0, 4, "RIFF") || b.compare(8, 4, "WAVE")) throw std::runtime_error("não é WAV: " + path.string());
    Wav w;
    int bits = 0, tag = 0;
    bool seen_fmt = false;
    for (size_t pos = 12; pos + 8 <= b.size();) {
        std::string id = b.substr(pos, 4);
        size_t size = read_le<uint32_t>(&b[pos + 4]), body = pos + 8;
        if (id == "fmt ") {
            tag = read_le<uint16_t>(&b[body]);
            w.channels = read_le<uint16_t>(&b[body + 2]);
            w.sr = read_le<uint32_t>(&b[body + 4]);
            bits = read_le<uint16_t>(&b[body + 14]);
            if (tag == 0xFFFE) tag = read_le<uint16_t>(&b[body + 24]);   // WAVE_FORMAT_EXTENSIBLE: first word of the GUID
            seen_fmt = true;
        } else if (id == "data") {
            if (!seen_fmt) throw std::runtime_error("WAV sem fmt: " + path.string());
            size = std::min(size, b.size() - body);   // a session cut short may not have the final size
            size_t step = (size_t)bits / 8, n = size / step;
            w.samples.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const char* p = &b[body + i * step];
                double v;
                if (tag == 3 && bits == 32) v = read_le<float>(p);
                else if (tag == 3 && bits == 64) v = read_le<double>(p);
                else if (tag == 1 && bits == 16) v = read_le<int16_t>(p) / 32768.0;
                else if (tag == 1 && bits == 24) v = (read_le<uint16_t>(p) | ((int)(signed char)p[2] << 16)) / 8388608.0;
                else if (tag == 1 && bits == 32) v = read_le<int32_t>(p) / 2147483648.0;
                else throw std::runtime_error("formato de WAV não suportado: " + path.string());
                w.samples[i] = v;
            }
            return w;
        }
        pos = body + size + (size & 1);
    }
    throw std::runtime_error("WAV sem dados: " + path.string());
}

void write_wav(const fs::path& path, const std::vector<double>& x, int channels, double sr) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("não criei " + path.string());
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    uint32_t bytes = (uint32_t)(x.size() * 2);
    f.write("RIFF", 4), u32(36 + bytes), f.write("WAVEfmt ", 8), u32(16), u16(1), u16((uint16_t)channels);
    u32((uint32_t)sr), u32((uint32_t)sr * channels * 2), u16((uint16_t)(channels * 2)), u16(16);
    f.write("data", 4), u32(bytes);
    std::vector<int16_t> pcm(x.size());
    for (size_t i = 0; i < x.size(); ++i) pcm[i] = (int16_t)std::lrint(std::clamp(x[i], -1.0, 1.0) * 32767.0);
    f.write((const char*)pcm.data(), (std::streamsize)bytes);
}

std::vector<double> load_wav(const fs::path& path) {
    Wav w = read_wav(path);
    std::vector<double> mono(w.samples.size() / w.channels);
    for (size_t i = 0; i < mono.size(); ++i) {
        double s = 0;
        for (int c = 0; c < w.channels; ++c) s += w.samples[i * w.channels + c];
        mono[i] = s / w.channels;
    }
    return (int)w.sr == SR ? mono : resample_poly(mono, SR, (int)w.sr);   // input preparation: a driver would do it live
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("não criei " + path.string());
    f << text;
}

// ------------------------------------------------------------------ JSON
namespace {
struct Parser {
    const std::string& t;
    size_t i = 0;
    [[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("JSON: ") + what + " na posição " + std::to_string(i)); }
    void ws() { while (i < t.size() && std::isspace((unsigned char)t[i])) ++i; }
    bool word(const char* w) {
        size_t n = std::strlen(w);
        if (t.compare(i, n, w)) return false;
        i += n;
        return true;
    }
    std::string string_value() {
        std::string s;
        for (++i; i < t.size() && t[i] != '"'; ++i) {
            if (t[i] != '\\') { s += t[i]; continue; }
            char c = t[++i];
            switch (c) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'u': {
                    unsigned cp = (unsigned)std::stoul(t.substr(i + 1, 4), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) s += (char)cp;
                    else if (cp < 0x800) s += (char)(0xC0 | cp >> 6), s += (char)(0x80 | (cp & 63));
                    else s += (char)(0xE0 | cp >> 12), s += (char)(0x80 | ((cp >> 6) & 63)), s += (char)(0x80 | (cp & 63));
                    break;
                }
                default: s += c;
            }
        }
        if (i >= t.size()) fail("texto sem fim");
        ++i;
        return s;
    }
    Json value() {
        ws();
        if (i >= t.size()) fail("fim inesperado");
        Json j;
        char c = t[i];
        if (c == '{') {
            j.kind = Json::Obj;
            ++i;
            for (ws(); i < t.size() && t[i] != '}'; ws()) {
                if (t[i] == ',') { ++i; ws(); }
                if (t[i] != '"') fail("chave esperada");
                std::string key = string_value();
                ws();
                if (t[i++] != ':') fail("':' esperado");
                j.o.emplace_back(std::move(key), value());
            }
            ++i;
        } else if (c == '[') {
            j.kind = Json::Arr;
            ++i;
            for (ws(); i < t.size() && t[i] != ']'; ws()) {
                if (t[i] == ',') { ++i; continue; }
                j.a.push_back(value());
            }
            ++i;
        } else if (c == '"') {
            j.kind = Json::Str;
            j.s = string_value();
        } else if (word("true")) { j.kind = Json::Bool; j.b = true; }
        else if (word("false")) { j.kind = Json::Bool; }
        else if (word("null")) {}
        else if (word("NaN")) { j.kind = Json::Num; j.n = std::nan(""); }
        else {
            size_t used = 0;
            try { j.n = std::stod(t.substr(i, 40), &used); } catch (...) { fail("valor inválido"); }
            j.kind = Json::Num;
            i += used;
        }
        return j;
    }
};
}  // namespace

Json Json::parse(const std::string& text) { Parser p{text}; return p.value(); }

Json Json::load(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("não abri " + path.string());
    std::stringstream s;
    s << f.rdbuf();
    return parse(s.str());
}

const Json* Json::get(const std::string& key) const {
    for (const auto& [k, v] : o)
        if (k == key) return v.kind == Null ? nullptr : &v;
    return nullptr;
}

// ------------------------------------------------------------------ sessions
LabConfig config_from_json(const Json& d) {
    LabConfig c;
    auto text = [&](const char* k) { const Json* v = d.get(k); return v ? std::optional<std::string>(v->s) : std::nullopt; };
    auto number = [&](const char* k) { const Json* v = d.get(k); return v ? std::optional<double>(v->n) : std::nullopt; };
    auto flag = [&](const char* k) { const Json* v = d.get(k); return v && v->b; };
    if (auto m = text("modo")) c.modo = *m;
    c.tom = text("tom"), c.progressao = text("progressao");
    c.bpm = number("bpm"), c.bpm_inicial = number("bpm_inicial");
    c.clique = flag("clique"), c.manter_padrao = flag("manter_padrao");
    c.aprender_na_tonica = flag("aprender_na_tonica"), c.contagem = flag("contagem");
    if (const Json* fb = d.get("feedback")) c.fb_gain = fb->get("gain")->n, c.fb_delay = fb->get("delay")->n;
    return c;
}

Session load_session(const fs::path& folder) {
    Json cfg = Json::load(folder / "config.json");
    Wav w = read_wav(folder / "entrada.wav");
    Session s;
    s.audio = std::move(w.samples);
    s.sr = w.sr;
    if (const Json* run = cfg.get("run")) s.cfg = config_from_json(*run);
    if (const Json* la = cfg.get("lookahead")) s.lookahead = la->n;
    return s;
}

}  // namespace lab
