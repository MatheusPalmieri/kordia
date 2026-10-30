// Live engine: audio device + kordia::Processor + session recording. Everything the UI needs goes through
// devices() / start() / stop() / setLive() / snapshot(), as the JSON ui/index.html reads.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

#include "kordia/core.h"

class SessionWriter;

class LiveEngine : public juce::AudioIODeviceCallback {
public:
    LiveEngine();
    ~LiveEngine() override;

    juce::var devices();
    // empty = ok, else an error message for the UI. preview: only listens (shows what it hears, plays and records nothing)
    juce::String start(const juce::var& request, bool preview = false);
    void stop();
    void setLive(const juce::var& request);
    juce::var snapshot();
    juce::File sessionDir() const { return session_; }

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceError(const juce::String& message) override { error_ = message; }

    // What the UI shows, copied out of the audio thread after each analysis block (fixed size, no allocation).
    struct Diary { double t; int state; int chord; int mode; char reason[160]; };
    struct Snap {
        int state = 0, level = 2, chord = -1, mode = 0, heard = 24, beat = 0, next = -1, pos = -1, nPattern = 0;
        int pattern[kordia::P::GRID_MAX_SLOTS] = {};
        int count = 0, note = -1;   // count-in clicks left; strongest pitch class of the frame
        double conf = 0, db = -120, bpm = 0;
        bool frozen = false, fixedGrid = false, click = false;
        double bassRms = 0, padRms = 0;
        int key = -1;          // current tonic (configured if it fits, else detected)
        bool learning = false;
        int bassNote = -1, nPad = 0, padNotes[8] = {};
        Diary diary[12] = {};
        int nDiary = 0;
    };

private:
    void analyzeHop();
    void publish();

    juce::AudioDeviceManager dm_;
    std::unique_ptr<kordia::Processor> proc_;
    std::unique_ptr<SessionWriter> rec_;
    juce::File session_;
    juce::String error_, report_;

    // audio-thread state
    int factor_ = 2;                  // device rate / analysis rate (44.1k -> 22.05k, 48k -> 24k)
    double sr_ = 44100.0, asr_ = 22050.0, lookahead_ = 0.0;
    std::vector<double> fir_, hist_;  // anti-alias low-pass for the decimation
    int histPos_ = 0, phase_ = 0;
    std::vector<double> hop_;
    int hopFill_ = 0;
    long long outPos_ = 0;
    std::vector<double> L_, R_;
    long long seenDecisions_ = 0;
    Diary diary_[12] = {};
    int diaryHead_ = 0, diaryCount_ = 0;
    double costSum_ = 0, costMax_ = 0;
    int costN_ = 0;

    // set by the UI thread, read by the audio thread
    std::atomic<float> gain_{1.0f}, volume_{1.0f}, clickVolume_{0.7f};
    std::atomic<bool> monitor_{false}, click_{false}, dynFixed_{false}, freeze_{false}, running_{false}, hold_{true};
    std::atomic<bool> preview_{false};

    std::string lastReason_;
    int lastXruns_ = 0;
    juce::ChildProcess reportProc_;   // kordia_lab replaying the session -> report

    juce::SpinLock snapLock_;
    Snap snap_;
    std::atomic<double> costMean_{0}, costPeak_{0}, rawDb_{-120};
    std::atomic<int> rawChannels_{0};
    // shared-mode WASAPI on some machines hands float samples read as int32: every value lands near
    // +0.42 / -0.58 and almost none near zero. Detected so the UI can say "change driver" instead of playing on noise.
    int corruptCount_ = 0, corruptTotal_ = 0;
    std::atomic<bool> inputCorrupt_{false};
    // the capture stopped delivering (seen live: exact zeros forever after a dropout storm)
    long long zeroRun_ = 0;
    std::atomic<bool> inputDead_{false};
    std::atomic<double> pendingBpm_{0};   // live BPM change, applied by the audio thread
    std::atomic<float> layerGain_[kordia::P::N_LAYERS] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::atomic<float> bassGain_{1.0f};
    std::atomic<bool> learn_{true};
    int xrunWindowStart_ = 0;             // UI thread: dropout rate over the last ~5 s
    juce::uint32 xrunWindowT_ = 0;
    bool xrunStorm_ = false;
    std::atomic<int> xrunBase_{0};   // dropouts while the driver starts are not counted
    int xruns() const;
    juce::Time started_;
};
