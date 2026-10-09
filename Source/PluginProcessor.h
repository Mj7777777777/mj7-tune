// MJ7 Tune - coeur du plugin : parametres, autotune, analyse de tonalite, styles.
#pragma once
#include "Params.h"

class MJ7TuneProcessor final : public juce::AudioProcessor,
                               private juce::Timer,
                               private juce::Thread
{
public:
    enum AnalysisState { idle = 0, waiting, listening, computing, done, failed };

    MJ7TuneProcessor();
    ~MJ7TuneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MJ7 Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    void startAnalysis();
    void undoAnalysis();
    AnalysisState analysisState() const noexcept { return (AnalysisState) state.load(); }
    float analysisProgress() const noexcept { return captureLen > 0 ? (float) capturePos.load() / (float) captureLen : 0.0f; }
    float analysisSeconds() const noexcept { return 20.0f; }
    bool hasAnalysis() const noexcept { return hasUndo; }
    juce::StringArray summary;

    int currentPreset() const noexcept { return presetIndex; }
    void loadFactoryPreset (int index);
    bool savePresetFile (const juce::File&);
    bool loadPresetFile (const juce::File&);
    static juce::File presetFolder();
    void toggleAB();
    int abSlot() const noexcept { return abIndex; }

    /** Notes autorisees (bit i = note i, Do = 0), d'apres la tonalite et la gamme ou les notes choisies. */
    uint16_t allowedNotes() const noexcept;
    /** Clic sur une touche du clavier : passe en gamme personnalisee et active / coupe cette note. */
    void toggleNote (int note);

    std::atomic<float> meter[1];
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    mj7::TuneEngine& engineForDisplay() noexcept { return engine; }
    int latencyMs() const noexcept { return juce::roundToInt (1000.0f * (float) latencyNow.load() / sr); }

private:
    void timerCallback() override;
    void run() override;
    void processChunk (float* L, float* R, int n);
    void applyAnalysis();
    void setParam (int pid, float realValue);
    void writeExtras (juce::ValueTree&) const;
    void readExtras (const juce::ValueTree&);
    float v (int pid) const noexcept { return raw[pid]->load (std::memory_order_relaxed); }
    bool on (int pid) const noexcept { return v (pid) > 0.5f; }

    std::atomic<float>* raw[mj7::kNumParams] = {};
    float sr = 48000.0f; int latency = 0, modeNow = -1; std::atomic<int> latencyNow { 0 };
    static constexpr int chunk = 512;

    mj7::TuneEngine engine;
    mj7::FixedDelay bypassL, bypassR; mj7::Smooth outGainSm;
    bool wasBypassed = false;
    float tmpR[chunk] = {};

    // --- analyse ---
    std::atomic<int> state { idle }, capturePos { 0 };
    std::atomic<bool> resultReady { false };
    std::vector<float> capture; int captureLen = 0, waited = 0;
    mj7::AnalysisResult result; mj7::TuningReport report;
    int undoKey = 0, undoScale = 0; bool hasUndo = false;

    int presetIndex = 0, abIndex = 0; juce::ValueTree abState[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MJ7TuneProcessor)
};
