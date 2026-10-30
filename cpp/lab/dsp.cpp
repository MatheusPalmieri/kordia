// Offline helpers with the exact semantics the measurements were taken with (numpy, scipy.signal).
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <exception>
#include <mutex>
#include <numbers>
#include <numeric>
#include <thread>

#include "lab.h"

namespace lab {

static double pairwise(const double* a, size_t n) {
    if (n < 8) {
        double r = 0.0;
        for (size_t i = 0; i < n; ++i) r += a[i];
        return r;
    }
    if (n <= 128) {
        double r[8];
        std::copy(a, a + 8, r);
        size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (int k = 0; k < 8; ++k) r[k] += a[i + k];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) res += a[i];
        return res;
    }
    size_t half = n / 2;
    half -= half % 8;
    return pairwise(a, half) + pairwise(a + half, n - half);
}

double np_sum(const double* x, size_t n) { return pairwise(x, n); }
double np_mean(const std::vector<double>& x) { return np_sum(x.data(), x.size()) / (double)x.size(); }

double median(std::vector<double> x) {
    std::sort(x.begin(), x.end());
    size_t n = x.size();
    return n % 2 ? x[n / 2] : 0.5 * (x[n / 2 - 1] + x[n / 2]);
}

double percentile(std::vector<double> x, double q) {
    std::sort(x.begin(), x.end());
    double pos = q / 100.0 * (double)(x.size() - 1);
    size_t i = (size_t)pos;
    double frac = pos - (double)i;
    return i + 1 < x.size() ? x[i] + (x[i + 1] - x[i]) * frac : x[i];
}

double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp) {
    if (x < xp.front()) return fp.front();
    if (x > xp.back()) return fp.back();
    size_t j = (size_t)(std::upper_bound(xp.begin(), xp.end(), x) - xp.begin()) - 1;
    if (j == xp.size() - 1 || xp[j] == x) return fp[j];
    return (fp[j + 1] - fp[j]) / (xp[j + 1] - xp[j]) * (x - xp[j]) + fp[j];
}

double std_dev(const std::vector<double>& x) {
    double m = np_mean(x), s = 0;
    for (double v : x) s += (v - m) * (v - m);
    return std::sqrt(s / (double)x.size());
}

double corrcoef(const std::vector<double>& x, const std::vector<double>& y) {
    double mx = np_mean(x), my = np_mean(y), xx = 0, yy = 0, xy = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        double a = x[i] - mx, b = y[i] - my;
        xx += a * a, yy += b * b, xy += a * b;
    }
    return std::clamp(xy / std::sqrt(xx) / std::sqrt(yy), -1.0, 1.0);
}

std::vector<double> linspace(double a, double b, int n) {
    std::vector<double> y((size_t)n);
    if (n == 0) return y;
    double step = n > 1 ? (b - a) / (n - 1) : 0.0;
    for (int i = 0; i < n; ++i) y[(size_t)i] = i * step + a;
    if (n > 1) y.back() = b;
    return y;
}

// Direct form II transposed, the same operations in the same order as scipy.signal.lfilter.
std::vector<double> lfilter(const std::vector<double>& b_in, const std::vector<double>& a_in, const std::vector<double>& x) {
    size_t order = std::max(b_in.size(), a_in.size());
    std::vector<double> b = b_in, a = a_in, z(order, 0.0), y(x.size());
    b.resize(order, 0.0);
    a.resize(order, 0.0);
    for (size_t n = 0; n < x.size(); ++n) {
        double xn = x[n], yn = z[0] + b[0] * xn;
        y[n] = yn;
        for (size_t i = 0; i + 2 < order; ++i) z[i] = z[i + 1] + xn * b[i + 1] - yn * a[i + 1];
        if (order >= 2) z[order - 2] = xn * b[order - 1] - yn * a[order - 1];
    }
    return y;
}

static double bessel_i0(double x) {   // power series: 1e-16 from scipy.special.i0 at the arguments of a Kaiser window
    double term = 1.0, sum = 1.0, q = x * x / 4.0;
    for (int k = 1; k < 200 && term > sum * 1e-18; ++k) {
        term *= q / ((double)k * k);
        sum += term;
    }
    return sum;
}

// scipy.signal.resample_poly(x, up, down): Kaiser (beta 5) low-pass, zero padding outside the signal.
std::vector<double> resample_poly(const std::vector<double>& x, int up, int down) {
    int g = std::gcd(up, down);
    up /= g, down /= g;
    if (up == 1 && down == 1) return x;
    long long n_in = (long long)x.size();
    long long n_out = n_in * up / down + (n_in * up % down != 0);
    int max_rate = std::max(up, down), half_len = 10 * max_rate, numtaps = 2 * half_len + 1;
    double cutoff = 1.0 / max_rate, alpha = 0.5 * (numtaps - 1), norm = bessel_i0(5.0);
    std::vector<double> h((size_t)numtaps);
    for (int i = 0; i < numtaps; ++i) {
        double m = i - alpha, c = cutoff * m;
        double sinc = c == 0.0 ? 1.0 : std::sin(std::numbers::pi * c) / (std::numbers::pi * c);
        double ratio = m / alpha;
        h[(size_t)i] = cutoff * sinc * (bessel_i0(5.0 * std::sqrt(1.0 - ratio * ratio)) / norm);
    }
    double sum = np_sum(h.data(), h.size());
    for (double& v : h) v = v / sum * up;

    long long pre = down - half_len % down, post = 0, remove = (half_len + pre) / down;
    auto out_len = [&](long long len_h) { return ((n_in - 1) * up + len_h - 1) / down + 1; };
    while (out_len(numtaps + pre + post) < n_out + remove) ++post;
    h.insert(h.begin(), (size_t)pre, 0.0);
    long long len_h = (long long)h.size();

    std::vector<double> y((size_t)n_out);
    for (long long k = 0; k < n_out; ++k) {
        long long p = (k + remove) * down;
        long long first = std::max(0LL, (p - len_h + up) / up), last = std::min(n_in - 1, p / up);
        double acc = 0.0;
        for (long long j = first; j <= last; ++j) acc += x[(size_t)j] * h[(size_t)(p - j * up)];
        y[(size_t)k] = acc;
    }
    return y;
}

double round_digits(double x, int digits) {
    if (!std::isfinite(x)) return x;
    char buf[400];
    auto end = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, digits).ptr;
    double r = 0.0;
    std::from_chars(buf, end, r);
    return r;
}

std::string shortest_repr(double x) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
    char buf[400];
    auto end = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed).ptr;
    std::string s(buf, end);
    return s.find('.') == std::string::npos ? s + ".0" : s;
}

void parallel_for(size_t n, const std::function<void(size_t)>& f) {
    std::atomic<size_t> next{0};
    std::exception_ptr failure;
    std::mutex lock;
    auto worker = [&] {
        for (size_t i = next++; i < n; i = next++) {
            try {
                f(i);
            } catch (...) {
                std::lock_guard<std::mutex> guard(lock);
                if (!failure) failure = std::current_exception();
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < std::max(1u, std::thread::hardware_concurrency()); ++k) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (failure) std::rethrow_exception(failure);
}

}  // namespace lab
