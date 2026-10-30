#include "LiveEngine.h"

#include <cstring>

using namespace kordia;

static void copyText(char* dst, size_t cap, const std::string& s) {
    size_t n = std::min(cap - 1, s.size());
    std::memcpy(dst, s.data(), n);
    dst[n] = 0;
}

// ------------------------------------------------------------------ session recording
// Same folder layout as acompanhante/session.py, so `python -m acompanhante.cli rodar --sessao` replays it.
// The audio thread only writes into lock-free FIFOs; this thread does all file I/O.
struct Row {
    double t, db, bpm, tempoConf, conf, procMs;
    int onset, heard, state, level, chord, mode, fail;
};

class SessionWriter : private juce::Thread {
public:
    SessionWriter(const juce::File& dir, const juce::var& config, double asr)
        : juce::Thread("kordia-session"), dir_(dir), audioFifo_((int)(asr * 20)), audio_((size_t)(asr * 20)),
          rowFifo_(4096), rows_(4096), decFifo_(256), decs_(256) {
        dir_.createDirectory();
        dir_.getChildFile("config.json").replaceWithText(juce::JSON::toString(config));
        juce::WavAudioFormat wav;
        writer_.reset(wav.createWriterFor(new juce::FileOutputStream(dir_.getChildFile("entrada.wav")), asr, 1, 16, {}, 0));
        csv_ = std::make_unique<juce::FileOutputStream>(dir_.getChildFile("quadros.csv"));
        jsonl_ = std::make_unique<juce::FileOutputStream>(dir_.getChildFile("decisoes.jsonl"));
        csv_->writeText("t,db,onset,bpm,tempo_conf,ouvido,conf,estado,nivel,tocando,proc_ms,falha\n", false, false, nullptr);
        startThread();
    }

    // audio thread
    void push(const double* x, int n, const Row& row) {
        int s1, n1, s2, n2;
        audioFifo_.prepareToWrite(n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) audio_[(size_t)(s1 + i)] = (float)x[i];
        for (int i = 0; i < n2; ++i) audio_[(size_t)(s2 + i)] = (float)x[n1 + i];
        audioFifo_.finishedWrite(n1 + n2);
        write(rowFifo_, rows_, row);
    }
    void pushDecision(const LiveEngine::Diary& d) { write(decFifo_, decs_, d); }

    void finish(const juce::var& summary) {
        stopThread(5000);
        drain();
        writer_.reset();
        csv_.reset();
        jsonl_.reset();
        dir_.getChildFile("resumo.json").replaceWithText(juce::JSON::toString(summary));
    }

private:
    template <typename T>
    static void write(juce::AbstractFifo& f, std::vector<T>& buf, const T& v) {
        int s1, n1, s2, n2;
        f.prepareToWrite(1, s1, n1, s2, n2);
        if (n1) buf[(size_t)s1] = v;   // full: dropped (the writer is far faster than 43 rows/s)
        f.finishedWrite(n1);
    }

    void run() override {
        while (!threadShouldExit()) {
            wait(50);
            drain();
        }
    }

    void drain() {
        int s1, n1, s2, n2;
        audioFifo_.prepareToRead(audioFifo_.getNumReady(), s1, n1, s2, n2);
        for (auto [s, n] : {std::pair{s1, n1}, std::pair{s2, n2}})
            if (n > 0 && writer_) {
                const float* ch[] = {audio_.data() + s};
                writer_->writeFromFloatArrays(ch, 1, n);
            }
        audioFifo_.finishedRead(n1 + n2);

        static const char* states[] = {"OCIOSO", "OUVINDO", "TRAVADO", "FALLBACK", "ENCERRANDO"};
        static const char* levels[] = {"CONFIANTE", "DUVIDA", "PERDIDO"};
        rowFifo_.prepareToRead(rowFifo_.getNumReady(), s1, n1, s2, n2);
        for (auto [s, n] : {std::pair{s1, n1}, std::pair{s2, n2}})
            for (int i = 0; i < n; ++i) {
                const Row& r = rows_[(size_t)(s + i)];
                juce::String playing = r.mode == 0 ? "-" : juce::String(label(r.chord < 0 ? 0 : r.chord));
                if (r.mode == 2) playing << juce::String::fromUTF8("(sem terça)");
                if (r.mode == 3) playing = juce::String::fromUTF8("tônica");
                juce::String line;
                line << juce::String(r.t, 3) << "," << juce::String(r.db, 1) << "," << r.onset << ","
                     << (std::isnan(r.bpm) ? juce::String() : juce::String(r.bpm, 1)) << "," << juce::String(r.tempoConf, 2)
                     << "," << label(r.heard) << "," << juce::String(r.conf, 2) << "," << states[r.state] << ","
                     << levels[r.level] << "," << playing << "," << juce::String(r.procMs, 3) << "," << r.fail << "\n";
                csv_->writeText(line, false, false, nullptr);
            }
        rowFifo_.finishedRead(n1 + n2);

        decFifo_.prepareToRead(decFifo_.getNumReady(), s1, n1, s2, n2);
        for (auto [s, n] : {std::pair{s1, n1}, std::pair{s2, n2}})
            for (int i = 0; i < n; ++i) {
                const auto& d = decs_[(size_t)(s + i)];
                auto* o = new juce::DynamicObject();
                o->setProperty("t", d.t);
                o->setProperty("estado", states[d.state]);
                if (d.chord != -2) {   // -2: reason change without an accompaniment change
                    o->setProperty("acorde", d.chord < 0 ? juce::var() : juce::var(juce::String(label(d.chord))));
                    o->setProperty("modo", d.mode == 0 ? juce::var() : juce::var(mode_name((Mode)d.mode)));
                }
                o->setProperty("motivo", juce::String::fromUTF8(d.reason));
                jsonl_->writeText(juce::JSON::toString(juce::var(o), true) + "\n", false, false, nullptr);
            }
        decFifo_.finishedRead(n1 + n2);
        csv_->flush();
        jsonl_->flush();
    }

    juce::File dir_;
    juce::AbstractFifo audioFifo_;
    std::vector<float> audio_;
    juce::AbstractFifo rowFifo_;
    std::vector<Row> rows_;
    juce::AbstractFifo decFifo_;
    std::vector<LiveEngine::Diary> decs_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;
    std::unique_ptr<juce::FileOutputStream> csv_, jsonl_;
};

// ------------------------------------------------------------------ engine
static const char* STATE_TEXT[] = {"Esperando você tocar", "Ouvindo e seguindo", "Padrão aprendido",
                                   "Em dúvida — segurando a tônica", "Você parou — terminando o compasso"};

LiveEngine::LiveEngine() {}

LiveEngine::~LiveEngine() { stop(); }

juce::var LiveEngine::devices() {
    juce::Array<juce::var> typeList, ins, outs;
    juce::String current = dm_.getCurrentAudioDeviceType();
    for (auto* t : dm_.getAvailableDeviceTypes()) typeList.add(t->getTypeName());
    juce::AudioIODeviceType* type = nullptr;
    for (auto* t : dm_.getAvailableDeviceTypes())
        if ((current.isEmpty() && t->getTypeName() == "Windows Audio") || t->getTypeName() == current) type = t;
    if (!type && !dm_.getAvailableDeviceTypes().isEmpty()) type = dm_.getAvailableDeviceTypes()[0];
    auto* o = new juce::DynamicObject();
    if (type) {
        type->scanForDevices();
        for (auto& n : type->getDeviceNames(true)) {
            auto* d = new juce::DynamicObject();
            d->setProperty("id", n);
            d->setProperty("name", n);
            ins.add(juce::var(d));
        }
        for (auto& n : type->getDeviceNames(false)) {
            auto* d = new juce::DynamicObject();
            d->setProperty("id", n);
            d->setProperty("name", n);
            outs.add(juce::var(d));
        }
        auto inNames = type->getDeviceNames(true), outNames = type->getDeviceNames(false);
        int di = type->getDefaultDeviceIndex(true), dout = type->getDefaultDeviceIndex(false);
        o->setProperty("default_input", di >= 0 && di < inNames.size() ? inNames[di] : juce::String());
        o->setProperty("default_output", dout >= 0 && dout < outNames.size() ? outNames[dout] : juce::String());
        o->setProperty("type", type->getTypeName());
    }
    o->setProperty("types", typeList);
    o->setProperty("inputs", ins);
    o->setProperty("outputs", outs);
    return juce::var(o);
}

static juce::String str(const juce::var& v) { return v.isVoid() || v.isUndefined() ? juce::String() : v.toString(); }

juce::String LiveEngine::start(const juce::var& q) {
    if (running_) return {};
    RunConfig cfg;
    try {
        if (auto k = str(q["tom"]); k.isNotEmpty()) note_pc(k.toStdString()), cfg.tom = k.toStdString();
        if (q["bpm"].isDouble() || q["bpm"].isInt()) {
            double bpm = (double)q["bpm"];
            if (bpm < 30 || bpm > 250) return "BPM deve estar entre 30 e 250.";   // ASCII only
            cfg.bpm = bpm;
        }
        if (auto p = str(q["progressao"]).trim(); p.isNotEmpty()) {
            for (auto& c : juce::StringArray::fromTokens(p, " ", "")) {
                if (c.isEmpty()) continue;
                try { chord_index(c.toStdString()); }
                catch (...) { return juce::String::fromUTF8("Acorde não reconhecido: '") + c + "' (use C, Am, F#m, Bb...)"; }
            }
            cfg.progressao = p.toStdString();
        }
    } catch (const std::exception& e) {
        return juce::String::fromUTF8("Configuração inválida: ") + e.what();
    }
    cfg.clique = (bool)q["clique"];
    cfg.modo = cfg.bpm ? "metronomo" : (cfg.progressao ? "decorado" : "livre");

    dm_.getAvailableDeviceTypes();
    if (auto type = str(q["type"]); type.isNotEmpty() && type != dm_.getCurrentAudioDeviceType())
        dm_.setCurrentAudioDeviceType(type, true);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm_.getAudioDeviceSetup(setup);
    setup.inputDeviceName = str(q["input"]);
    setup.outputDeviceName = str(q["output"]);
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setBit(0);
    setup.useDefaultOutputChannels = true;
    setup.sampleRate = (double)q["sample_rate"];   // 0 / absent: device default (44.1 or 48 kHz)
    // 480 samples (~10 ms): measured stable on WASAPI exclusive here (256 gave dropouts); nearest the driver allows
    setup.bufferSize = q.hasProperty("buffer") && (int)q["buffer"] > 0 ? (int)q["buffer"] : 480;
    // initialise() also tells the manager we need 1 input and 2 outputs; without it the capture is never opened
    // and the callback receives an input buffer with no data in it (measured: "-7 dB of noise" on every mic)
    if (auto err = dm_.initialise(1, 2, nullptr, false, {}, &setup); err.isNotEmpty())
        return juce::String::fromUTF8("Não consegui abrir o áudio: ") + err;
    auto* dev = dm_.getCurrentAudioDevice();
    if (!dev) return juce::String::fromUTF8("Nenhum dispositivo de áudio aberto.");

    sr_ = dev->getCurrentSampleRate();
    factor_ = juce::jmax(1, juce::roundToInt(sr_ / 22050.0));
    asr_ = sr_ / factor_;
    lookahead_ = (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples()) / sr_;
    // anti-alias low-pass (Blackman-windowed sinc) at 0.45 of the analysis Nyquist... of the device rate
    const int taps = factor_ > 1 ? 63 : 1;
    fir_.assign((size_t)taps, 0.0);
    double fc = 0.45 / factor_, sum = 0;
    for (int k = 0; k < taps; ++k) {
        double m = k - (taps - 1) / 2.0;
        double sinc = m == 0 ? 2 * fc : std::sin(2 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
        double w = taps == 1 ? 1 : 0.42 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * k / (taps - 1)) +
                                       0.08 * std::cos(4 * juce::MathConstants<double>::pi * k / (taps - 1));
        fir_[(size_t)k] = sinc * w;
        sum += fir_[(size_t)k];
    }
    for (auto& h : fir_) h /= sum;
    hist_.assign((size_t)taps, 0.0);
    histPos_ = phase_ = hopFill_ = 0;
    hop_.assign(P::HOP, 0.0);
    L_.assign(8192, 0.0);
    R_.assign(8192, 0.0);
    outPos_ = 0;
    seenDecisions_ = 0;
    diaryHead_ = diaryCount_ = 0;
    costSum_ = costMax_ = 0;
    costN_ = 0;
    lastReason_.clear();
    lastReason_.reserve(512);   // no allocation in the audio thread when the reason changes
    lastXruns_ = 0;
    error_.clear();
    report_.clear();
    xrunBase_ = 0;
    inputCorrupt_ = false;
    inputDead_ = false;
    zeroRun_ = 0;
    pendingBpm_ = 0;
    corruptCount_ = corruptTotal_ = 0;
    try {
        proc_ = std::make_unique<Processor>(cfg, lookahead_, asr_, sr_);
    } catch (const std::exception& e) {
        dm_.closeAudioDevice();
        return e.what();
    }
    proc_->decisions.reserve(512);
    proc_->onset_list.reserve(512);
    proc_->acc().events.reserve(1024);
    proc_->acc().changes.reserve(512);
    setLive(q);

    auto run = new juce::DynamicObject();
    run->setProperty("modo", juce::String(cfg.modo));
    run->setProperty("tom", cfg.tom ? juce::var(juce::String(*cfg.tom)) : juce::var());
    run->setProperty("bpm", cfg.bpm ? juce::var(*cfg.bpm) : juce::var());
    run->setProperty("progressao", cfg.progressao ? juce::var(juce::String(*cfg.progressao)) : juce::var());
    run->setProperty("clique", cfg.clique);
    auto info = new juce::DynamicObject();
    info->setProperty("run", juce::var(run));
    info->setProperty("lookahead", lookahead_);
    info->setProperty("sr", asr_);
    info->setProperty("motor", "C++/JUCE");
    info->setProperty("driver", dm_.getCurrentAudioDeviceType());
    info->setProperty("entrada", setup.inputDeviceName);
    info->setProperty("saida", setup.outputDeviceName);
    info->setProperty("taxa_dispositivo", sr_);
    info->setProperty("buffer", dev->getCurrentBufferSizeSamples());
    session_ = juce::File(KORDIA_REPO_DIR).getChildFile("sessoes").getChildFile(
        juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"));
    rec_ = std::make_unique<SessionWriter>(session_, juce::var(info), asr_);
    started_ = juce::Time::getCurrentTime();
    running_ = true;
    dm_.addAudioCallback(this);
    return {};
}

void LiveEngine::stop() {
    if (!running_) return;
    dm_.removeAudioCallback(this);
    running_ = false;
    auto* dev = dm_.getCurrentAudioDevice();
    auto* s = new juce::DynamicObject();
    s->setProperty("duracao_s", (juce::Time::getCurrentTime() - started_).inSeconds());
    s->setProperty("falhas_de_audio", xruns());
    s->setProperty("proc_ms_media", costMean_.load());
    s->setProperty("proc_ms_max", costPeak_.load());
    s->setProperty("orcamento_ms", dev ? 1000.0 * dev->getCurrentBufferSizeSamples() / sr_ : 0.0);
    s->setProperty("latencia_ms", juce::roundToInt(lookahead_ * 1000));
    s->setProperty("decisoes", (juce::int64)(proc_ ? proc_->n_decisions : 0));
    dm_.closeAudioDevice();
    if (rec_) rec_->finish(juce::var(s));
    rec_.reset();
    // the report comes from the Python lab replaying the session (same algorithm, offline)
    auto python = juce::File(KORDIA_REPO_DIR).getChildFile(".venv/Scripts/python.exe");
    if (python.existsAsFile()) {
        if (reportProc_.start(juce::StringArray{python.getFullPathName(), "-m", "acompanhante.cli", "rodar", "--sessao",
                                          session_.getFullPathName(), "--saida", session_.getFullPathName()},
                        0))
            report_ = "gerando";
    }
}

void LiveEngine::setLive(const juce::var& q) {
    if (q.hasProperty("gain_db")) gain_ = (float)juce::Decibels::decibelsToGain((double)q["gain_db"]);
    if (q.hasProperty("volume")) volume_ = (float)juce::jlimit(0.0, 2.0, (double)q["volume"]);
    if (q.hasProperty("monitor")) monitor_ = (bool)q["monitor"];
    if (q.hasProperty("clique")) click_ = (bool)q["clique"];
    if (q.hasProperty("click_volume")) clickVolume_ = (float)juce::jlimit(0.0, 1.0, (double)q["click_volume"]);
    if (q.hasProperty("dinamica_fixa")) dynFixed_ = (bool)q["dinamica_fixa"];
    if (q.hasProperty("fixar_bpm")) freeze_ = (bool)q["fixar_bpm"];
    if (q.hasProperty("bpm") && (q["bpm"].isDouble() || q["bpm"].isInt()))
        pendingBpm_ = juce::jlimit(30.0, 250.0, (double)q["bpm"]);
}

void LiveEngine::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut,
                                                  int n, const juce::AudioIODeviceCallbackContext&) {
    auto t0 = juce::Time::getHighResolutionTicks();
    if (!running_ || !proc_ || n > (int)L_.size()) {
        for (int c = 0; c < numOut; ++c) juce::FloatVectorOperations::clear(out[c], n);
        return;
    }
    proc_->click = click_;
    proc_->click_vel = (int)(clickVolume_ * 127);
    proc_->dyn().fixed = dynFixed_;
    if (freeze_ != proc_->tracker().frozen()) proc_->freeze_tempo(freeze_);
    if (double b = pendingBpm_.exchange(0.0); b > 0) proc_->set_bpm(b);

    const float g = gain_;
    const float* x = numIn > 0 ? in[0] : nullptr;
    if (x) {   // raw device level (before gain and decimation): diagnostics in the snapshot and session summary
        double e = 0;
        for (int i = 0; i < n; ++i) e += (double)x[i] * x[i];
        rawDb_ = 10 * std::log10(e / n + 1e-12);
    }
    rawChannels_ = numIn;
    for (int i = 0; x && i < n; ++i) {
        zeroRun_ = x[i] == 0.0f ? zeroRun_ + 1 : 0;
        inputDead_ = zeroRun_ > (long long)(2 * sr_);
        float a = std::abs(x[i]);
        corruptCount_ += a > 0.3f && a < 0.75f;
        if (++corruptTotal_ == 22050) {   // ~0.5 s
            inputCorrupt_ = corruptCount_ > 0.97 * corruptTotal_;
            corruptCount_ = corruptTotal_ = 0;
        }
    }
    const int taps = (int)fir_.size();
    for (int i = 0; i < n; ++i) {
        hist_[(size_t)histPos_] = x ? x[i] * g : 0.0;
        histPos_ = (histPos_ + 1) % taps;
        if (++phase_ < factor_) continue;
        phase_ = 0;
        double y = 0;
        for (int k = 0; k < taps; ++k) y += fir_[(size_t)k] * hist_[(size_t)((histPos_ + k) % taps)];
        hop_[(size_t)hopFill_++] = y;
        if (hopFill_ == P::HOP) {
            analyzeHop();
            hopFill_ = 0;
        }
    }

    proc_->render(outPos_ / sr_ + lookahead_, n, L_.data(), R_.data());
    if (outPos_ < (long long)(0.5 * sr_) && outPos_ + n >= (long long)(0.5 * sr_))
        if (auto* d = dm_.getCurrentAudioDevice()) xrunBase_ = juce::jmax(0, d->getXRunCount());
    outPos_ += n;
    const float vol = volume_;
    const bool mon = monitor_;
    for (int c = 0; c < numOut; ++c) {
        const double* src = (c % 2) ? R_.data() : L_.data();
        for (int i = 0; i < n; ++i)
            out[c][i] = (float)juce::jlimit(-1.0, 1.0, src[i] * vol + (mon && x ? x[i] * g : 0.0));
    }
    double ms = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1000;
    costSum_ += ms;
    costMax_ = juce::jmax(costMax_ * 0.999, ms);   // slowly decaying peak
    if (++costN_ == 200) {
        costMean_ = costSum_ / costN_;
        costSum_ = 0;
        costN_ = 0;
    }
    costPeak_ = costMax_;
}

int LiveEngine::xruns() const {
    auto* dev = dm_.getCurrentAudioDevice();
    return dev && outPos_ >= (long long)(0.5 * sr_) ? juce::jmax(0, dev->getXRunCount() - xrunBase_) : 0;
}

void LiveEngine::analyzeHop() {
    auto t0 = juce::Time::getHighResolutionTicks();
    std::string& lastReason = lastReason_;
    proc_->analyze(hop_.data());
    const auto& eng = proc_->engine();
    // new decisions -> diary + session
    long long fresh = proc_->n_decisions - seenDecisions_;
    auto& dec = proc_->decisions;
    for (long long k = juce::jmin<long long>(fresh, (long long)dec.size()); k > 0; --k) {
        const auto& d = dec[dec.size() - (size_t)k];
        Diary e{d.t, (int)d.state, d.chord, (int)d.mode, {}};
        copyText(e.reason, sizeof(e.reason), d.reason);
        diary_[diaryHead_] = e;
        diaryHead_ = (diaryHead_ + 1) % 12;
        diaryCount_ = juce::jmin(12, diaryCount_ + 1);
        if (rec_) rec_->pushDecision(e);
        lastReason = eng.reason;
    }
    if (fresh == 0 && eng.reason != lastReason) {   // a reason without a sound change (lock, unlock, scheduled)
        lastReason = eng.reason;
        Diary e{proc_->time(), (int)eng.state, -2, 0, {}};
        copyText(e.reason, sizeof(e.reason), eng.reason);
        diary_[diaryHead_] = e;
        diaryHead_ = (diaryHead_ + 1) % 12;
        diaryCount_ = juce::jmin(12, diaryCount_ + 1);
        if (rec_) rec_->pushDecision(e);
    }
    seenDecisions_ = proc_->n_decisions;
    // live mode: bounded history (erase does not allocate)
    if (dec.size() > 256) dec.erase(dec.begin(), dec.end() - 64);
    if (proc_->onset_list.size() > 256) proc_->onset_list.erase(proc_->onset_list.begin(), proc_->onset_list.end() - 64);
    auto& evs = proc_->acc().events;
    if (evs.size() > 512) evs.erase(evs.begin(), evs.end() - 64);
    auto& ch = proc_->acc().changes;
    if (ch.size() > 256) ch.erase(ch.begin(), ch.end() - 64);

    const Frame& f = proc_->frame;
    int& lastXruns = lastXruns_;
    int xr = xruns();
    double ms = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1000;
    if (rec_)
        rec_->push(hop_.data(), P::HOP,
                   Row{f.t, f.db, f.bpm, f.tempo_conf, f.conf, ms, (int)f.onset, f.heard, (int)f.state, (int)f.level,
                       eng.cur.first, (int)eng.cur.second, xr > lastXruns ? 1 : 0});
    lastXruns = xr;
    publish();
}

void LiveEngine::publish() {
    const juce::SpinLock::ScopedTryLockType lock(snapLock_);
    if (!lock.isLocked()) return;   // the UI is reading: skip this one, the next block refreshes it
    const auto& eng = proc_->engine();
    const auto& mem = proc_->memory();
    Snap& s = snap_;
    s.state = (int)eng.state;
    s.level = (int)eng.level;
    s.chord = eng.cur.first;
    s.mode = (int)eng.cur.second;
    s.heard = proc_->frame.heard;
    s.conf = proc_->frame.conf;
    s.db = proc_->frame.db;
    s.bpm = std::isnan(proc_->frame.bpm) ? 0 : proc_->frame.bpm;
    bool active = eng.state == State::Ouvindo || eng.state == State::Travado || eng.state == State::Fallback;
    auto b = proc_->beat_in_bar();
    s.beat = b && (active || proc_->click) ? *b : 0;
    auto nx = proc_->next_chord();
    s.next = nx ? *nx : -1;
    s.nPattern = mem.locked ? juce::jmin(8, (int)mem.known.size()) : 0;
    for (int i = 0; i < s.nPattern; ++i) s.pattern[i] = mem.known[(size_t)i];
    s.pos = mem.locked ? mem.pos : -1;
    s.frozen = proc_->tracker().frozen();
    s.fixedGrid = eng.metronome;
    s.click = proc_->click;
    s.bassRms = proc_->synth().bass_rms;
    s.padRms = proc_->synth().pad_rms;
    s.bassNote = proc_->acc().bass();
    s.nPad = juce::jmin(8, (int)proc_->acc().pad().size());
    for (int i = 0; i < s.nPad; ++i) s.padNotes[i] = proc_->acc().pad()[(size_t)i];
    s.nDiary = diaryCount_;
    for (int i = 0; i < diaryCount_; ++i) s.diary[i] = diary_[(diaryHead_ - 1 - i + 24) % 12];   // newest first
}

juce::var LiveEngine::snapshot() {
    Snap s;
    {
        const juce::SpinLock::ScopedLockType lock(snapLock_);
        s = snap_;
    }
    auto* dev = dm_.getCurrentAudioDevice();
    auto* o = new juce::DynamicObject();
    bool running = running_ && proc_;
    o->setProperty("running", running);
    juce::String err = error_;
    if (inputCorrupt_ && err.isEmpty())
        err = juce::String::fromUTF8("A entrada chega corrompida neste driver (formato de amostra). "
                                     "Pare e escolha \"Windows Audio (Exclusive Mode)\".");
    o->setProperty("error", err.isEmpty() ? juce::var() : juce::var(err));
    // warnings: shown in the footer, no popup (they can be false alarms)
    auto now = juce::Time::getMillisecondCounter();
    if (now - xrunWindowT_ > 5000) {
        xrunStorm_ = xruns() - xrunWindowStart_ > 10;
        xrunWindowStart_ = xruns();
        xrunWindowT_ = now;
    }
    juce::String warn;
    if (running_ && inputDead_)
        warn = juce::String::fromUTF8("Sem sinal na entrada há mais de 2 s. Se você está tocando, o driver pode ter "
                                      "travado: pare e inicie de novo.");
    else if (running_ && xrunStorm_)
        warn = juce::String::fromUTF8("Muitas falhas de áudio: pare e escolha uma latência do driver maior.");
    o->setProperty("warning", warn.isEmpty() ? juce::var() : juce::var(warn));
    o->setProperty("session", session_ == juce::File() ? juce::var() : juce::var(session_.getFullPathName()));
    if (report_ == "gerando" && session_.getChildFile("replay_" + session_.getFileName() + ".png").existsAsFile())
        report_ = "pronto";
    o->setProperty("report", report_.isEmpty() ? juce::var() : juce::var(report_));
    o->setProperty("latency_ms", juce::roundToInt(lookahead_ * 1000));
    o->setProperty("xruns", xruns());
    o->setProperty("cost_ms", juce::String(costMean_.load(), 3).getDoubleValue());
    o->setProperty("cost_max_ms", juce::String(costPeak_.load(), 2).getDoubleValue());
    o->setProperty("budget_ms", dev ? juce::String(1000.0 * dev->getCurrentBufferSizeSamples() / sr_, 1).getDoubleValue() : 0.0);
    o->setProperty("raw_db", rawDb_.load());
    o->setProperty("raw_channels", rawChannels_.load());
    o->setProperty("engine", juce::String::fromUTF8("C++/JUCE · ") + dm_.getCurrentAudioDeviceType());
    juce::Array<juce::var> diary;
    for (int i = 0; i < s.nDiary; ++i) {
        auto* d = new juce::DynamicObject();
        d->setProperty("t", s.diary[i].t);
        d->setProperty("estado", state_name((State)s.diary[i].state));
        if (s.diary[i].chord != -2) {
            d->setProperty("acorde", s.diary[i].chord < 0 ? juce::var() : juce::var(juce::String(label(s.diary[i].chord))));
            d->setProperty("modo", s.diary[i].mode == 0 ? juce::var() : juce::var(mode_name((Mode)s.diary[i].mode)));
        }
        d->setProperty("motivo", juce::String::fromUTF8(s.diary[i].reason));
        diary.add(juce::var(d));
    }
    o->setProperty("diary", diary);
    if (!running) return juce::var(o);
    o->setProperty("state", state_name((State)s.state));
    o->setProperty("state_text", juce::String::fromUTF8(STATE_TEXT[s.state]));
    o->setProperty("level", level_name((Level)s.level));
    juce::var playing, extra;
    if (s.mode == (int)Mode::Tonic) {
        playing = proc_->engine().key_pc ? juce::String(label(*proc_->engine().key_pc)) : juce::String();
        extra = juce::String::fromUTF8("tônica");
    } else if (s.mode != (int)Mode::Silence) {
        playing = juce::String(label(s.chord));
        if (s.mode == (int)Mode::Root5) extra = juce::String::fromUTF8("sem terça");
    }
    o->setProperty("playing", playing);
    o->setProperty("playing_extra", extra);
    o->setProperty("heard", s.heard == N_IDX ? juce::var() : juce::var(juce::String(label(s.heard))));
    o->setProperty("conf", s.conf);
    o->setProperty("db", s.db);
    o->setProperty("bpm", s.bpm > 0 ? juce::var(s.bpm) : juce::var());
    o->setProperty("beat", s.beat ? juce::var(s.beat) : juce::var());
    o->setProperty("frozen", s.frozen);
    o->setProperty("fixed_grid", s.fixedGrid);
    o->setProperty("clique", s.click);
    o->setProperty("next", s.next >= 0 ? juce::var(juce::String(label(s.next))) : juce::var());
    auto noteName = [](int m) { return juce::String(label(m % 12)) + juce::String(m / 12 - 1); };
    auto* out = new juce::DynamicObject();
    out->setProperty("bass_rms", s.bassRms);
    out->setProperty("pad_rms", s.padRms);
    out->setProperty("bass", s.bassNote >= 0 ? juce::var(noteName(s.bassNote)) : juce::var());
    juce::Array<juce::var> pad;
    for (int i = 0; i < s.nPad; ++i) pad.add(noteName(s.padNotes[i]));
    out->setProperty("pad", pad);
    o->setProperty("out", juce::var(out));
    if (s.nPattern > 0) {
        juce::Array<juce::var> p;
        for (int i = 0; i < s.nPattern; ++i) p.add(juce::String(label(s.pattern[i])));
        o->setProperty("pattern", p);
        o->setProperty("pattern_pos", s.pos);
    }
    return juce::var(o);
}
