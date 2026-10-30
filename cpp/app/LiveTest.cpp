// Headless check of the live path: open devices (volume 0), run, read the UI snapshot, stop.
// Usage: KordiaLiveTest [driver type] [sample rate]
#include <juce_events/juce_events.h>

#include "LiveEngine.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    LiveEngine engine;
    auto dev = engine.devices();
    auto* q = new juce::DynamicObject();
    juce::var request(q);   // owns q (reference counted): keep it alive for the whole test
    q->setProperty("type", argc > 1 ? juce::var(juce::String(argv[1])) : dev["type"]);
    q->setProperty("input", argc > 3 ? juce::var(juce::String(argv[3])) : dev["default_input"]);
    q->setProperty("output", argc > 4 ? juce::var(juce::String(argv[4])) : dev["default_output"]);
    q->setProperty("sample_rate", argc > 2 ? juce::String(argv[2]).getDoubleValue() : 0.0);
    q->setProperty("tom", "C");
    q->setProperty("volume", 0.0);
    if (auto b = juce::SystemStats::getEnvironmentVariable("KORDIA_BUFFER", ""); b.isNotEmpty()) q->setProperty("buffer", b.getIntValue());
    auto err = engine.start(request);
    std::cout << "driver " << request["type"].toString() << " | start: " << (err.isEmpty() ? "ok" : err) << "\n";
    if (err.isNotEmpty()) return 1;
    juce::Thread::sleep(2000);
    auto s = engine.snapshot();
    std::cout << "  db " << (double)s["db"] << " | raw_db " << (double)s["raw_db"] << " ch " << (int)s["raw_channels"]
              << " | latency_ms " << (int)s["latency_ms"] << " | xruns " << (int)s["xruns"] << " | error " << s["error"].toString() << "\n";
    engine.stop();
    std::cout << "  session " << engine.sessionDir().getFullPathName() << "\n";
    return 0;
}
