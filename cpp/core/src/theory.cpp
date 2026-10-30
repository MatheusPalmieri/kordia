#include <stdexcept>

#include "kordia/core.h"

namespace kordia {

static const char* NOTE_NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

std::string label(int idx) {
    if (idx == N_IDX) return "N";
    return std::string(NOTE_NAMES[idx % 12]) + (idx >= 12 ? "m" : "");
}

int note_pc(const std::string& name) {
    for (int i = 0; i < 12; ++i)
        if (name == NOTE_NAMES[i]) return i;
    static const std::pair<const char*, int> aliases[] = {{"Db", 1}, {"D#", 3}, {"Gb", 6}, {"G#", 8}, {"A#", 10},
                                                          {"Cb", 11}, {"Fb", 4}, {"E#", 5}, {"B#", 0}};
    for (auto& [a, pc] : aliases)
        if (name == a) return pc;
    throw std::invalid_argument("nota desconhecida: " + name);
}

int chord_index(const std::string& chord) {
    std::string c = chord.substr(0, chord.find('/'));
    if (c.empty()) throw std::invalid_argument("acorde vazio");
    size_t rl = (c.size() > 1 && (c[1] == '#' || c[1] == 'b')) ? 2 : 1;
    std::string rest = c.substr(rl);
    bool minor = rest.rfind("m", 0) == 0 && rest.rfind("maj", 0) != 0;
    return note_pc(c.substr(0, rl)) + 12 * minor;
}

std::array<int, 3> chord_pcs(int idx) {
    int root = idx % 12;
    bool minor = idx >= 12;
    return {root, (root + (minor ? 3 : 4)) % 12, (root + 7) % 12};
}

std::array<std::array<double, 12>, 24> templates() {
    std::array<std::array<double, 12>, 24> T{};
    for (int i = 0; i < 24; ++i) {
        for (int tone : chord_pcs(i))
            for (int h = 1; h <= P::TEMPLATE_HARMONICS; ++h)
                T[i][pymod(tone + pyround(12 * std::log2((double)h)), 12)] += std::pow(P::TEMPLATE_DECAY, h - 1);
        double n = 0;
        for (double v : T[i]) n += v * v;
        n = std::sqrt(n);
        for (double& v : T[i]) v /= n;
    }
    return T;
}

std::array<double, 25> key_prior(std::optional<int> key_pc) {
    std::array<double, 25> w;
    w.fill(1.0);
    if (key_pc) {
        int k = *key_pc;
        int diatonic[6] = {k, (k + 5) % 12, (k + 7) % 12, 12 + (k + 2) % 12, 12 + (k + 4) % 12, 12 + (k + 9) % 12};
        for (int i = 0; i < 24; ++i) {
            bool d = false;
            for (int x : diatonic) d |= (x == i);
            w[i] = d ? 1.0 : P::KEY_PRIOR_OUT;
        }
    }
    double s = 0;
    for (double v : w) s += v;
    for (double& v : w) v /= s;
    return w;
}

const char* state_name(State s) {
    static const char* n[] = {"OCIOSO", "OUVINDO", "TRAVADO", "FALLBACK", "ENCERRANDO"};
    return n[(int)s];
}
const char* level_name(Level l) {
    static const char* n[] = {"CONFIANTE", "DUVIDA", "PERDIDO"};
    return n[(int)l];
}
const char* mode_name(Mode m) {
    static const char* n[] = {"", "full", "root5", "tonic"};
    return n[(int)m];
}

}  // namespace kordia
