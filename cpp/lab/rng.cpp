// NumPy's default_rng: SeedSequence -> PCG64 (XSL-RR 128/64) -> ziggurat normals, Lemire bounded integers.
#include <cmath>

#include "lab.h"
#include "ziggurat_tables.h"

namespace lab {

static constexpr uint32_t MULT_A = 0x931e8875, MULT_B = 0x58f38ded, INIT_A = 0x43b0d7e5, INIT_B = 0x8b51f9dd;
static constexpr uint32_t MIX_L = 0xca01f9dd, MIX_R = 0x4973f715;
static constexpr uint64_t PCG_HI = 0x2360ED051FC65DA4ULL, PCG_LO = 0x4385DF649FCCF645ULL;
static constexpr double ZIG_R = 3.6541528853610087963519472518, ZIG_INV_R = 0.27366123732975827203338247596;

static uint32_t hashmix(uint32_t value, uint32_t& hash) {
    value ^= hash;
    hash *= MULT_A;
    value *= hash;
    return value ^ (value >> 16);
}

static uint32_t mix(uint32_t x, uint32_t y) {
    uint32_t r = MIX_L * x - MIX_R * y;
    return r ^ (r >> 16);
}

static void mul64(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    lo = (mid << 32) | (uint32_t)p00;
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

void Rng::step() {   // state = state * MULT + inc
    uint64_t hi, lo;
    mul64(lo_, PCG_LO, hi, lo);
    hi += lo_ * PCG_HI + hi_ * PCG_LO;
    lo_ = lo + inc_lo_;
    hi_ = hi + inc_hi_ + (lo_ < lo);
}

Rng::Rng(uint32_t seed) {
    // SeedSequence with one word of entropy, pool of 4
    uint32_t pool[4], hash = INIT_A;
    pool[0] = hashmix(seed, hash);
    for (int i = 1; i < 4; ++i) pool[i] = hashmix(0, hash);
    for (int src = 0; src < 4; ++src)
        for (int dst = 0; dst < 4; ++dst)
            if (src != dst) pool[dst] = mix(pool[dst], hashmix(pool[src], hash));
    uint32_t state[8];
    hash = INIT_B;
    for (int i = 0; i < 8; ++i) {
        uint32_t v = pool[i % 4] ^ hash;
        hash *= MULT_B;
        v *= hash;
        state[i] = v ^ (v >> 16);
    }
    auto word = [&](int i) { return (uint64_t)state[2 * i] | ((uint64_t)state[2 * i + 1] << 32); };
    uint64_t seed_hi = word(0), seed_lo = word(1), seq_hi = word(2), seq_lo = word(3);
    inc_hi_ = (seq_hi << 1) | (seq_lo >> 63);
    inc_lo_ = (seq_lo << 1) | 1;
    step();
    lo_ += seed_lo;
    hi_ += seed_hi + (lo_ < seed_lo);
    step();
}

uint64_t Rng::next64() {
    step();
    uint64_t x = hi_ ^ lo_;
    unsigned rot = (unsigned)(hi_ >> 58);
    return rot ? (x >> rot) | (x << (64 - rot)) : x;
}

uint32_t Rng::next32() {
    if (has_saved_) {
        has_saved_ = false;
        return saved_;
    }
    uint64_t x = next64();
    has_saved_ = true;
    saved_ = (uint32_t)(x >> 32);
    return (uint32_t)x;
}

double Rng::next_double() { return (double)(next64() >> 11) * (1.0 / 9007199254740992.0); }

double Rng::normal() {
    for (;;) {
        uint64_t r = next64();
        int idx = (int)(r & 0xff);
        r >>= 8;
        int sign = (int)(r & 1);
        uint64_t rabs = (r >> 1) & 0x000fffffffffffffULL;
        double x = (double)rabs * WI[idx];
        if (sign) x = -x;
        if (rabs < KI[idx]) return x;   // 99.3% of the time
        if (idx == 0) {
            for (;;) {
                double xx = -ZIG_INV_R * std::log1p(-next_double());
                double yy = -std::log1p(-next_double());
                if (yy + yy > xx * xx) return ((rabs >> 8) & 1) ? -(ZIG_R + xx) : ZIG_R + xx;
            }
        }
        if ((FI[idx - 1] - FI[idx]) * next_double() + FI[idx] < std::exp(-0.5 * x * x)) return x;
    }
}

long long Rng::integers(long long lo, long long hi) {
    uint32_t rng = (uint32_t)(hi - 1 - lo);
    if (rng == 0) return lo;   // no draw
    uint32_t excl = rng + 1;
    uint64_t m = (uint64_t)next32() * excl;
    uint32_t left = (uint32_t)m;
    if (left < excl) {
        uint32_t threshold = (UINT32_MAX - rng) % excl;
        while (left < threshold) {
            m = (uint64_t)next32() * excl;
            left = (uint32_t)m;
        }
    }
    return lo + (long long)(m >> 32);
}

}  // namespace lab
