// CLI do laboratório: gerar (cenários), rodar (WAV ou sessão), avaliar (cenários -> métricas + relatórios), prova.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <map>
#include <set>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "lab.h"

using namespace lab;

namespace {

struct Args {
    std::map<std::string, std::vector<std::string>> values;   // --name a b c
    bool has(const std::string& k) const { return values.count(k) > 0; }
    std::string get(const std::string& k, const std::string& fallback = "") const {
        auto it = values.find(k);
        return it == values.end() || it->second.empty() ? fallback : it->second[0];
    }
    std::vector<std::string> list(const std::string& k) const {
        auto it = values.find(k);
        return it == values.end() ? std::vector<std::string>{} : it->second;
    }
};

Args parse(int argc, char** argv) {
    Args a;
    std::string key;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            key = s.substr(2);
            a.values[key];
        } else if (key.empty()) {
            throw std::invalid_argument("argumento solto: " + s);
        } else {
            a.values[key].push_back(s);
        }
    }
    return a;
}

std::vector<std::string> all_scenarios() {
    std::vector<std::string> names;
    for (const auto& s : scenarios()) names.push_back(s.name);
    return names;
}

int gerar(const Args& a) {
    if (!a.has("todos") && !a.has("cenario")) throw std::invalid_argument("gerar: use --cenario NOME ou --todos");
    fs::path out = a.get("saida", "dados");
    fs::create_directories(out);
    for (const auto& name : a.has("todos") ? all_scenarios() : a.list("cenario")) {
        Song s = build_scenario(name);
        write_wav(out / (name + ".wav"), std::vector<double>(s.audio.begin(), s.audio.end()), 1, SR);
        write_text(out / (name + ".json"), truth_to_json(s.truth));
        std::cout << std::format("{}: {:.1f} s -> {}.wav/.json\n", name, (double)s.audio.size() / SR, (out / name).string());
    }
    return 0;
}

int rodar(const Args& a) {
    if (!a.has("entrada") && !a.has("sessao")) throw std::invalid_argument("rodar: use --entrada ARQUIVO.wav ou --sessao PASTA");
    std::vector<double> audio;
    LabConfig cfg;
    double lookahead = 0.0, sr = SR;
    std::string name;
    if (a.has("sessao")) {
        fs::path folder = a.get("sessao");
        Session s = load_session(folder);
        audio = std::move(s.audio), cfg = s.cfg, lookahead = s.lookahead, sr = s.sr;
        name = "replay_" + fs::absolute(folder).lexically_normal().filename().string();
    } else {
        audio = load_wav(a.get("entrada"));
        cfg.modo = a.get("modo", "livre");
        if (a.has("tom")) cfg.tom = a.get("tom");
        if (a.has("bpm")) cfg.bpm = std::stod(a.get("bpm"));
        if (a.has("progressao")) cfg.progressao = a.get("progressao");
        name = fs::path(a.get("entrada")).stem().string();
    }
    auto t0 = std::chrono::steady_clock::now();
    double seconds = (double)audio.size() / sr;
    Result res = run(std::move(audio), cfg, lookahead, sr);
    std::optional<Truth> truth;
    if (a.has("verdade")) truth = truth_from_json(Json::load(a.get("verdade")));
    fs::path out = a.get("saida", "saida");
    save_outputs(res, out, name, truth ? &*truth : nullptr, name + " — modo " + cfg.modo, out, sr);
    double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << std::format("{}: {:.1f} s processados em {:.1f} s; {} trocas\n", name, seconds, took, res.changes.size());
    if (truth)
        for (const auto& [k, v] : evaluate(res, *truth)) std::cout << "  " << k << ": " << val_csv(v) << "\n";
    std::cout << "saídas em " << fs::absolute(out).string() << "\n";
    return 0;
}

const std::vector<std::pair<const char*, const char*>> COLS = {
    {"acc_total", "acc"}, {"acc_confiante", "acc_conf"}, {"cobertura", "cobert"}, {"lat_media_ms", "lat_ms"}, {"lat_p95_ms", "lat95"},
    {"lat_media_pulso", "lat_pul"}, {"lat_reativo_ms", "lat_reat"}, {"lat_travado_ms", "lat_trav"}, {"trocas_perdidas", "perd"},
    {"bpm_erro_medio", "bpm_err"}, {"t_trava_tempo_s", "trava_s"}, {"falso_disparo_s", "falso_s"}, {"errado_confiante_n", "err_conf"},
    {"parada_s", "parada"}, {"esqueceu", "esqueceu"}, {"recuperacao_pulsos", "recup"}, {"corr_dinamica", "dyn"}};

void print_table(const std::vector<std::pair<std::string, Metrics>>& rows) {
    std::cout << std::format("{:22s}", "cenario");
    for (const auto& [key, head] : COLS) std::cout << std::format("{:>9s}", head);
    std::cout << "\n";
    for (const auto& [name, m] : rows) {
        std::cout << std::format("{:22s}", name);
        for (const auto& [key, head] : COLS) {
            const Val& v = metric_val(m, key);
            std::string cell = v.kind == Val::None || v.isnan() ? "-" : v.kind == Val::Bool ? val_csv(v) : std::format("{:.2f}", v.v);
            std::cout << std::format("{:>9s}", cell);
        }
        std::cout << "\n";
    }
    std::cout << std::format("{:22s}", "MÉDIA");
    for (const auto& [key, head] : COLS) {
        std::vector<double> vals;
        for (const auto& [name, m] : rows) {
            const Val& v = metric_val(m, key);
            if ((v.kind == Val::Int || v.kind == Val::Real) && !v.isnan()) vals.push_back(v.v);
        }
        std::cout << std::format("{:>9s}", vals.empty() ? std::string("-") : std::format("{:.2f}", round_digits(np_mean(vals), 3)));
    }
    std::cout << "\n";
}

int avaliar(const Args& a) {
    auto names = a.has("cenario") && !a.has("todos") ? a.list("cenario") : all_scenarios();
    std::string versao = a.get("versao", "dev");
    print_table(evaluate_all(names, versao, true));
    std::cout << "\nresultados em " << fs::absolute(fs::path("resultados") / versao).string() << "\n";
    return 0;
}

int prova(const Args& a) {
    std::vector<int> keys;
    for (const auto& k : a.list("tom")) keys.push_back(note_pc(k));
    auto rows = exam::run_exam(exam::cases(a.list("progressao"), keys, a.list("variante")));
    exam::print_exam(rows);
    std::cout << "\nsessões reais, inteiras (controle):\n";
    exam::print_controls(exam::run_controls());
    std::cout << "\nresultados em " << fs::absolute(exam::save_exam(rows, a.get("versao", "dev"))).string() << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::string usage =
        "uso: kordia_lab gerar   --cenario NOME | --todos [--saida dados]\n"
        "     kordia_lab rodar   --entrada a.wav [--modo livre|metronomo|decorado] [--bpm 120] [--tom C] [--progressao \"C G Am F\"] [--verdade a.json] [--saida saida]\n"
        "     kordia_lab rodar   --sessao sessoes/<pasta> [--saida saida]\n"
        "     kordia_lab avaliar [--todos | --cenario NOME] [--versao v]\n"
        "     kordia_lab prova   [--versao v] [--progressao a b] [--tom C F#] [--variante clique livre]\n";
    if (argc < 2) {
        std::cerr << usage;
        return 2;
    }
    try {
        std::string cmd = argv[1];
        Args a = parse(argc, argv);
        if (cmd == "gerar") return gerar(a);
        if (cmd == "rodar") return rodar(a);
        if (cmd == "avaliar") return avaliar(a);
        if (cmd == "prova") return prova(a);
        std::cerr << usage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
