// Kordia live app: one window with the web UI (acompanhante/ui/index.html, embedded) in a WebView2.
// The page talks to LiveEngine through the WebView resource provider: GET /api/<name>?q=<json>.
#include <BinaryData.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "LiveEngine.h"

class MainComponent : public juce::Component {
public:
    MainComponent()
        : browser_(juce::WebBrowserComponent::Options{}
                       .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                           .withBackgroundColour(juce::Colours::black)   // no white flash before the entrance screen
                           .withUserDataFolder(
                               juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                   .getChildFile("kordia/webview2")))
                       .withResourceProvider([this](const auto& url) { return provide(url); })) {
        addAndMakeVisible(browser_);
        browser_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
        setSize(1240, 880);
    }

    void resized() override { browser_.setBounds(getLocalBounds()); }

private:
    static juce::WebBrowserComponent::Resource bytes(const void* data, size_t size, const char* mime) {
        auto* p = static_cast<const std::byte*>(data);
        return {std::vector<std::byte>(p, p + size), mime};
    }

    static juce::WebBrowserComponent::Resource json(const juce::var& v) {
        auto s = juce::JSON::toString(v, true).toStdString();
        return bytes(s.data(), s.size(), "application/json");
    }

    static juce::var ok(const juce::String& err = {}) {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", err.isEmpty());
        if (err.isNotEmpty()) o->setProperty("error", err);
        return juce::var(o);
    }

    std::optional<juce::WebBrowserComponent::Resource> provide(const juce::String& url) {
        auto path = url.upToFirstOccurrenceOf("?", false, false);
        if (path.startsWith("http")) path = "/" + path.fromFirstOccurrenceOf("://", false, false).fromFirstOccurrenceOf("/", false, false);
        auto q = juce::JSON::parse(juce::URL::removeEscapeChars(url.fromFirstOccurrenceOf("?q=", false, false)));
        if (path == "/" || path == "/index.html") return bytes(BinaryData::index_html, BinaryData::index_htmlSize, "text/html");
        if (path == "/api/devices") return json(engine_.devices());
        if (path == "/api/state") return json(engine_.snapshot());
        if (path == "/api/start") return json(ok(engine_.start(q)));
        if (path == "/api/stop") return engine_.stop(), json(ok());
        if (path == "/api/live") return engine_.setLive(q), json(ok());
        if (path == "/api/open") {
            if (engine_.sessionDir().isDirectory()) engine_.sessionDir().startAsProcess();
            return json(ok());
        }
        return std::nullopt;
    }

    LiveEngine engine_;   // declared before the browser: destroyed after it
    juce::WebBrowserComponent browser_;
};

class KordiaApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    void initialise(const juce::String&) override { window_ = std::make_unique<Window>(); }
    void shutdown() override { window_.reset(); }

private:
    struct Window : juce::DocumentWindow {
        Window() : DocumentWindow("Kordia", juce::Colours::black, allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(720, 560, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window_;
};

START_JUCE_APPLICATION(KordiaApplication)
