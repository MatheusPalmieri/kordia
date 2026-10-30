// What the report screen draws from a recorded session: quadros.csv reduced to columns of heard × played.
// Pure (no JUCE, no files) so kordia_tests can cover it.
#pragma once

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct ReportColumn {
    double t = 0;           // start of the column, s
    std::string heard;      // most frequent chord heard; empty: no chord
    int conf = 0;           // mean confidence of those frames, %
    std::string played;     // most frequent thing played: a chord, "tônica", or empty
    bool miss = false;      // a real chord played against a different root heard
};

inline std::string chord_root(const std::string& chord) {
    size_t n = chord.size() > 1 && (chord[1] == '#' || chord[1] == 'b') ? 2 : 1;
    return chord.substr(0, n);
}

inline std::string most_frequent(const std::map<std::string, int>& counts) {
    std::string best;
    int top = 0;
    for (const auto& [name, n] : counts)
        if (n > top) best = name, top = n;
    return best;
}

// csv: quadros.csv as the app writes it. seconds: width of each column.
inline std::vector<ReportColumn> report_columns(const std::string& csv, double seconds) {
    struct Bucket { std::map<std::string, int> heard, played; std::map<std::string, double> conf; };
    std::vector<Bucket> buckets;
    std::istringstream lines(csv);
    std::string line, field;
    std::getline(lines, line);   // header
    while (std::getline(lines, line)) {
        std::vector<std::string> f;
        std::istringstream cells(line);
        while (std::getline(cells, field, ',')) f.push_back(field);
        if (f.size() < 10) continue;   // the last line of a session cut short
        const auto k = (size_t)(std::max(0.0, std::atof(f[0].c_str())) / seconds);
        if (k >= buckets.size()) buckets.resize(k + 1);
        if (f[5] != "N") buckets[k].heard[f[5]]++, buckets[k].conf[f[5]] += std::atof(f[6].c_str());
        if (f[9] != "-") buckets[k].played[f[9]]++;
    }
    std::vector<ReportColumn> columns;
    for (size_t k = 0; k < buckets.size(); ++k) {
        const Bucket& b = buckets[k];
        ReportColumn c;
        c.t = (double)k * seconds;
        c.heard = most_frequent(b.heard);
        c.played = most_frequent(b.played);
        if (!c.heard.empty()) c.conf = (int)(100 * b.conf.at(c.heard) / b.heard.at(c.heard) + 0.5);
        const bool real_chord = !c.played.empty() && c.played[0] >= 'A' && c.played[0] <= 'G';
        c.miss = real_chord && !c.heard.empty() && chord_root(c.heard) != chord_root(c.played);
        columns.push_back(c);
    }
    return columns;
}
