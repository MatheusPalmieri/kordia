// Headless check of the live path: open real devices (volume 0), run, read the UI snapshot, stop.
// Usage: KordiaLiveTest [driver type] [sample rate] [input] [output]
// Env: KORDIA_SECONDS (default 2), KORDIA_BUFFER, KORDIA_FIXED_BPM (fixed BPM + click), KORDIA_BPM_LIVE (BPM set mid-run)
#include <juce_events/juce_events.h>

#include "LiveEngine.h"

static juce::String env(const char* name) { return juce::SystemStats::getEnvironmentVariable(name, ""); }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    LiveEngine engine;
    auto dev = engine.devices();
    std::cout << "verificacao: " << juce::JSON::toString(engine.audioCheck(dev["default_input"].toString()), true) << std::endl;
    auto* q = new juce::DynamicObject();
    juce::var request(q);   // owns q (reference counted): keep it alive for the whole test
    q->setProperty("type", argc > 1 ? juce::var(juce::String(argv[1])) : dev["type"]);
    q->setProperty("sample_rate", argc > 2 ? juce::String(argv[2]).getDoubleValue() : 0.0);
    q->setProperty("input", argc > 3 ? juce::var(juce::String(argv[3])) : dev["default_input"]);
    q->setProperty("output", argc > 4 ? juce::var(juce::String(argv[4])) : dev["default_output"]);
    q->setProperty("tom", "C");
    q->setProperty("volume", 0.0);
    if (env("KORDIA_BUFFER").isNotEmpty()) q->setProperty("buffer", env("KORDIA_BUFFER").getIntValue());
    if (env("KORDIA_FIXED_BPM").isNotEmpty()) {
        q->setProperty("bpm", env("KORDIA_FIXED_BPM").getDoubleValue());
        q->setProperty("clique", true);
    }
    q->setProperty("contagem", env("KORDIA_FIXED_BPM").isNotEmpty());
    auto err = engine.start(request, true);   // as the app opens: only listens
    if (err.isNotEmpty()) return std::cout << "listen: " << err << std::endl, 1;
    juce::Thread::sleep(500);
    auto heard = engine.snapshot();
    std::cout << "ouvindo: running " << heard["running"].toString() << " listening " << heard["listening"].toString() << " db "
              << heard["db"].toString() << " nota " << heard["note"].toString() << " sessao " << engine.sessionDir().getFullPathName() << std::endl;

    err = engine.start(request);
    std::cout << "driver " << request["type"].toString() << " | start: " << (err.isEmpty() ? "ok" : err) << std::endl;
    if (err.isNotEmpty()) return 1;

    const int seconds = juce::jmax(1, env("KORDIA_SECONDS").getIntValue());
    juce::Thread::sleep(800);
    std::cout << "  contagem: " << engine.snapshot()["count"].toString() << std::endl;
    juce::Thread::sleep(500 * seconds);
    auto mid = engine.snapshot();
    std::cout << "  meio: bpm " << mid["bpm"].toString() << " beat " << mid["beat"].toString() << " contagem " << mid["count"].toString() << std::endl;
    if (env("KORDIA_BPM_LIVE").isNotEmpty()) {
        auto* live = new juce::DynamicObject();
        live->setProperty("bpm", env("KORDIA_BPM_LIVE").getDoubleValue());
        engine.setLive(juce::var(live));
    }
    juce::Thread::sleep(500 * seconds);

    auto s = engine.snapshot();
    std::cout << "  db " << (double)s["db"] << " | raw_db " << (double)s["raw_db"] << " ch " << (int)s["raw_channels"]
              << " | latency_ms " << (int)s["latency_ms"] << " | xruns " << (int)s["xruns"] << " | bpm "
              << s["bpm"].toString() << " | error " << s["error"].toString() << std::endl;
    std::cout << "  out " << juce::JSON::toString(s["out"], true) << std::endl;
    engine.stop();
    std::cout << "  session " << engine.sessionDir().getFullPathName() << std::endl;
    return 0;
}
