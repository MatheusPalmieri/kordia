// What Windows knows about the audio inputs, and what that means for the guitar reaching the app.
// read_audio_system() asks Windows; diagnose() is pure (no Windows, no JUCE) so kordia_tests can cover it.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct AudioInput {
    enum State { ACTIVE, DISABLED, NOT_PRESENT, UNPLUGGED };
    std::string name;   // as Windows shows it, UTF-8; the same name JUCE lists
    State state = ACTIVE;
    bool muted = false;
    float volume = 1;   // 0..1, the slider in the Windows sound settings
};

struct AudioSystem {
    std::vector<AudioInput> inputs;         // every input Windows remembers, connected or not
    std::vector<std::string> bad_drivers;   // audio or USB devices that are present but not working
    bool mic_blocked = false;               // Windows privacy settings deny the microphone to this app
};

struct AudioDiagnosis {
    std::string level = "ok";   // ok | aviso | erro
    std::string message;
    std::string settings;       // Windows page that solves it: privacidade | som | empty
};

AudioSystem read_audio_system();

inline bool audio_name_has(const std::string& name, std::initializer_list<const char*> words) {
    return std::any_of(words.begin(), words.end(), [&](const char* w) { return name.find(w) != std::string::npos; });
}
// shortcut: recognizes the computer's own microphone by name, add a word when a new maker shows up here
inline bool is_builtin_mic(const std::string& name) {
    return audio_name_has(name, {"Array", "Realtek", "Conexant", "Internal", "Integrated", "Webcam", "Interno"});
}
// a headset is never the guitar: a phone or AirPods out of range is no reason to warn
inline bool is_headset(const std::string& name) { return audio_name_has(name, {"Headset", "Hands-Free", "AirPods"}); }

// "Digital Audio Interface (Line 6 HX Stomp)" -> "Line 6 HX Stomp"
inline std::string audio_device_name(const std::string& name) {
    auto a = name.find('('), b = name.rfind(')');
    return a != std::string::npos && b != std::string::npos && b > a ? name.substr(a + 1, b - a - 1) : name;
}

inline std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

// One answer, the most blocking first: if Windows denies the microphone, nothing below matters.
inline AudioDiagnosis diagnose(const AudioSystem& sys, const std::string& selected) {
    if (sys.mic_blocked)
        return {"erro", "O Windows está bloqueando o microfone para este app.", "privacidade"};

    const AudioInput* current = nullptr;
    std::vector<std::string> missing, others;
    bool anyActive = false;
    for (const auto& in : sys.inputs) {
        const std::string device = audio_device_name(in.name);
        if (in.state == AudioInput::ACTIVE) {
            anyActive = true;
            if (in.name == selected) current = &in;
            else if (!is_builtin_mic(in.name) && !is_headset(in.name) && std::find(others.begin(), others.end(), device) == others.end())
                others.push_back(device);
        } else if (in.state == AudioInput::NOT_PRESENT && !is_headset(in.name) && !is_builtin_mic(in.name) &&
                   std::find(missing.begin(), missing.end(), device) == missing.end()) {
            missing.push_back(device);
        }
    }

    if (current && (current->muted || current->volume <= 0))
        return {"erro", "A entrada " + audio_device_name(current->name) + " está muda no Windows.", "som"};
    if (!sys.bad_drivers.empty())
        return {"erro", "Driver sem funcionar: " + join_names(sys.bad_drivers) + ". Se for sua interface, reinstale o driver do fabricante.", "som"};
    if (!anyActive) {
        if (!missing.empty())
            return {"erro", join_names(missing) + " não está conectado. Confira o cabo USB e se o aparelho está ligado.", "som"};
        return {"erro", "O Windows não tem nenhuma entrada de áudio ativa.", "som"};
    }
    if (current && is_builtin_mic(current->name)) {
        if (!others.empty())
            return {"aviso", join_names(others) + " está conectado, mas o app está ouvindo o microfone do computador. Troque em Ajustes > Entrada.", ""};
        if (!missing.empty())
            return {"aviso", join_names(missing) + " não está conectado, e o app está ouvindo o microfone do computador. Confira o cabo USB e se o aparelho está ligado.", ""};
    }
    return {};
}
