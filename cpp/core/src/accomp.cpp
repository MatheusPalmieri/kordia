// Accompaniment generator and dynamics.
#include <algorithm>

#include "kordia/core.h"

namespace kordia {

int Dynamics::velocity() const {
    if (fixed || !level_) return P::DYN_VEL_HIGH;
    double x = std::clamp((*level_ - P::DYN_DB_LOW) / (P::DYN_DB_HIGH - P::DYN_DB_LOW), 0.0, 1.0);
    return (int)round_even(P::DYN_VEL_LOW + x * (P::DYN_VEL_HIGH - P::DYN_VEL_LOW));
}

double Dynamics::expression(double t, std::optional<double> last_onset, double period) const {
    double beat = period > 0 ? period : 0.5;
    if (!last_onset || t - *last_onset < 1.5 * beat) return 1.0;
    return std::exp(-(t - *last_onset - 1.5 * beat) / P::DYN_SILENCE_DECAY);
}

// Open worship voicing: root, fifth, octave, third an octave up (tenth), quiet shimmer two octaves up.
static void voice(int chord, Mode mode, std::optional<int> key_pc, int& bass,
                  std::vector<std::pair<int, double>>& pad) {
    bass = -1;
    pad.clear();
    if (mode == Mode::Silence) return;
    int root, third = -1;
    if (mode == Mode::Tonic) {   // chord carries the key's tonic (configured or detected)
        root = chord >= 0 ? chord % 12 : *key_pc;
    } else {
        auto pcs = chord_pcs(chord);
        root = pcs[0];
        if (mode != Mode::Root5) third = (int)floor_mod(pcs[1] - root, 12);
        bass = P::BASS_LOW + root;
    }
    int r = P::PAD_ROOT_LOW + root;
    pad = {{r, 1.0}, {r + 7, 0.9}, {r + 12, 0.8}};
    if (mode == Mode::Tonic)
        for (auto& p : pad) p.second *= P::LEARN_TONIC_SCALE;
    if (third >= 0) pad.push_back({r + 12 + third, 0.8});
    pad.push_back({r + 24, P::PAD_SHIMMER});
}

Event& Accompanist::ev(double t, EvType type, Inst inst, int pitch, int vel) {
    events.push_back({std::nearbyint(t * 1e5) / 1e5, type, inst, pitch, vel});
    return events.back();
}

void Accompanist::apply(const Output& out, int vel, std::vector<Event>& evs) {
    size_t first = evs.size();
    for (int p : pad_) evs.push_back(ev(out.t, EvType::Off, Inst::Pad, p));
    if (bass_ >= 0) evs.push_back(ev(out.t, EvType::Off, Inst::Bass, bass_));
    if (out.mode == Mode::Silence)   // the musician stopped: short fade, not the long crossfade release
        for (size_t i = first; i < evs.size(); ++i) evs[i].rel = P::STOP_FADE;
    std::vector<std::pair<int, double>> pad;
    voice(out.chord, out.mode, key_pc_, bass_, pad);
    pad_.clear();
    for (auto& [p, scale] : pad) {
        pad_.push_back(p);
        evs.push_back(ev(out.t, EvType::On, Inst::Pad, p, (int)std::max<long long>(1, round_even(vel * scale))));
    }
    if (bass_ >= 0) {
        evs.push_back(ev(out.t, EvType::On, Inst::Bass, bass_, vel));
        last_bass_t_ = out.t;
    }
    changes.push_back({out.t, out.chord, out.mode});
}

void Accompanist::beat(double t, bool strong, double period, int vel, std::vector<Event>& evs) {
    // re-attack the bass on beats 1 and 3 (only called when the tempo is trusted)
    if (bass_ < 0 || !strong || t - last_bass_t_ < 0.5 * period) return;
    last_bass_t_ = t;
    evs.push_back(ev(t, EvType::Off, Inst::Bass, bass_));
    evs.push_back(ev(t, EvType::On, Inst::Bass, bass_, vel));
}

void Accompanist::click(double t, bool accent, int vel, std::vector<Event>& evs) {
    evs.push_back(ev(t, EvType::On, Inst::Click, accent ? 77 : 76, vel));
}

void Accompanist::expression(double t, double value, std::vector<Event>& evs) {
    if (std::abs(value - expr_) < 0.03 && !(value == 1.0 && expr_ != 1.0)) return;
    expr_ = value;
    Event& e = ev(t, EvType::Expr, Inst::All);
    e.value = std::nearbyint(value * 1e3) / 1e3;
    evs.push_back(e);
}

}  // namespace kordia
