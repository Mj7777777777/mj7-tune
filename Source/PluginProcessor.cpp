#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mj7;

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int pid = 0; pid < kNumParams; ++pid)
    {
        const auto& d = paramDef (pid);
        const juce::ParameterID id { d.id, 1 };
        const auto name = U8 (d.name), unit = U8 (d.unit);
        switch (d.type)
        {
            case PType::tF:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.centre > 0.0f) range.setSkewForCentre (d.centre);
                const bool db = juce::String (d.unit).startsWith ("dB");
                const int decimals = (d.max - d.min) >= 100.0f ? 0 : ((db || (d.max - d.min) >= 20.0f) ? 1 : 2);
                layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, range, d.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([decimals, unit] (float value, int)
                        {
                            if (std::abs (value) < 0.005f) value = 0.0f;
                            const auto num = decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
                            return unit.isEmpty() ? num : num + " " + unit;
                        })
                        .withValueFromStringFunction ([] (const juce::String& s) { return s.replaceCharacter (',', '.').getFloatValue(); })));
                break;
            }
            case PType::tI: layout.add (std::make_unique<juce::AudioParameterInt> (id, name, (int) d.min, (int) d.max, (int) d.def)); break;
            case PType::tB: layout.add (std::make_unique<juce::AudioParameterBool> (id, name, d.def > 0.5f)); break;
            case PType::tC: layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, juce::StringArray::fromTokens (U8 (d.choices), "|", ""), (int) d.def)); break;
        }
    }
    return layout;
}

MJ7TuneProcessor::MJ7TuneProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("MJ7 analyse"),
      apvts (*this, nullptr, "MJ7Tune", createLayout())
{
    for (int pid = 0; pid < kNumParams; ++pid) raw[pid] = apvts.getRawParameterValue (paramDef (pid).id);
    meter[0].store (0.0f);
    startTimerHz (15);
}

MJ7TuneProcessor::~MJ7TuneProcessor() { stopTimer(); stopThread (15000); }

bool MJ7TuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && in.size() <= out.size();
}

juce::AudioProcessorEditor* MJ7TuneProcessor::createEditor() { return new MJ7TuneEditor (*this); }

uint16_t MJ7TuneProcessor::allowedNotes() const noexcept
{
    const int sc = (int) v (scale);
    if (sc == Personnalisee)
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i) if (on (n0 + i)) m = (uint16_t) (m | (1u << i));
        return m != 0 ? m : (uint16_t) 0x0FFF;
    }
    return absoluteMask ((int) v (key), scaleMask (sc));
}

void MJ7TuneProcessor::toggleNote (int note)
{
    note = ((note % 12) + 12) % 12;
    if ((int) v (scale) != Personnalisee)
    {
        const auto mask = allowedNotes();
        for (int i = 0; i < 12; ++i) setParam (n0 + i, (mask >> i) & 1u ? 1.0f : 0.0f);
        setParam (scale, (float) Personnalisee);
    }
    setParam (n0 + note, on (n0 + note) ? 0.0f : 1.0f);
}

// ================================================================================================
void MJ7TuneProcessor::prepareToPlay (double sampleRate, int)
{
    stopThread (15000);
    if (state.load() != done) state.store (idle);
    sr = (float) sampleRate;
    engine.prepare (sr);
    modeNow = (int) v (mode);
    engine.setLatencySamples (modeNow == 1 ? (int) std::round (0.005f * sr) : engine.alignedLatency());
    latency = engine.latency();
    bypassL.prepare ((int) (0.05f * sr) + 8); bypassR.prepare ((int) (0.05f * sr) + 8);
    bypassL.setDelay (latency); bypassR.setDelay (latency);
    outGainSm.setTime (20.0f, sr); outGainSm.reset (dbToGain (v (out_gain)));
    latencyNow.store (latency); setLatencySamples (latency);
    const int needed = (int) (analysisSeconds() * sr);
    if (needed != captureLen) { capture.assign ((size_t) needed, 0.0f); captureLen = needed; capturePos.store (0); }
}

void MJ7TuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0) return;
    const bool stereo = buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;
    if (stereo && getTotalNumInputChannels() < 2) std::memcpy (R, L, sizeof (float) * (size_t) n);
    for (int pos = 0; pos < n; pos += chunk)
    {
        const int len = std::min (chunk, n - pos);
        if (stereo) processChunk (L + pos, R + pos, len);
        else
        {
            std::memcpy (tmpR, L + pos, sizeof (float) * (size_t) len); processChunk (L + pos, tmpR, len);
            for (int i = 0; i < len; ++i) L[pos + i] = 0.5f * (L[pos + i] + tmpR[i]);
        }
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
}

void MJ7TuneProcessor::processChunk (float* L, float* R, int n)
{
    const int st = state.load (std::memory_order_relaxed);
    if (st == waiting)
    {
        float pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        if (pk > 0.003f) state.store (listening);
        else if ((waited += n) > (int) (30.0f * sr)) state.store (failed);
    }
    if (state.load (std::memory_order_relaxed) == listening)
    {
        const int p = capturePos.load (std::memory_order_relaxed), take = std::min (n, captureLen - p);
        for (int i = 0; i < take; ++i) capture[(size_t) (p + i)] = 0.5f * (L[i] + R[i]);
        capturePos.store (p + take);
        if (p + take >= captureLen) state.store (computing);
    }
    float pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), pk));

    // mode Mix / Live : change la latence
    const int md = (int) v (mode);
    if (md != modeNow)
    {
        modeNow = md;
        engine.setLatencySamples (md == 1 ? (int) std::round (0.005f * sr) : engine.alignedLatency());
        latency = engine.latency();
        bypassL.setDelay (latency); bypassR.setDelay (latency); bypassL.reset(); bypassR.reset();
        latencyNow.store (latency);
    }

    const bool bypassed = on (bypass);
    if (bypassed != wasBypassed) { bypassL.reset(); bypassR.reset(); wasBypassed = bypassed; }
    if (bypassed)
    {
        for (int i = 0; i < n; ++i) { L[i] = bypassL.process (L[i]); R[i] = bypassR.process (R[i]); }
        engine.process (L, R, 0);
    }
    else
    {
        TuneParams p;
        p.enabled = on (tune_on); p.notes = allowedNotes();
        p.speedMs = v (speed); p.amount = v (amount) * 0.01f; p.natural = v (natural) * 0.01f; p.humanize = v (humanize) * 0.01f;
        p.transpose = v (transpose); p.formant = v (formant); p.mix = v (mix) * 0.01f;
        engine.setParams (p);
        engine.process (L, R, n);
        const float target = dbToGain (v (out_gain));
        for (int i = 0; i < n; ++i) { const float g = outGainSm.next (target); L[i] *= g; R[i] *= g; }
    }
    pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), pk));
}

// ================================================================================================
//  Analyse : tonalite et justesse
// ================================================================================================
void MJ7TuneProcessor::startAnalysis()
{
    if (state.load() == computing) return;
    stopThread (15000);
    waited = 0; capturePos.store (0); summary.clear();
    state.store (waiting);
}

void MJ7TuneProcessor::run()
{
    result = analyseVoice (capture.data(), captureLen, sr, StyleTarget(), 4.0f, 3.0f);
    const uint16_t mask = result.key >= 0 ? absoluteMask (result.key, scaleMask (result.scale == 1 ? Majeure : MineureNat)) : allowedNotes();
    report = measureTuning (capture.data(), captureLen, sr, mask);
    resultReady.store (true);
}

void MJ7TuneProcessor::timerCallback()
{
    if (latencyNow.load() != getLatencySamples()) setLatencySamples (latencyNow.load());
    if (state.load() == computing && ! isThreadRunning() && ! resultReady.load()) startThread();
    if (resultReady.exchange (false)) applyAnalysis();
    if (state.load() == failed && summary.isEmpty())
        summary.add (U8 ("Aucune voix détectée. Lancez la lecture de la voix, puis appuyez de nouveau."));
}

void MJ7TuneProcessor::setParam (int pid, float realValue)
{
    if (auto* p = apvts.getParameter (paramDef (pid).id))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::abs (norm - p->getValue()) < 1.0e-6f) return;
        p->beginChangeGesture(); p->setValueNotifyingHost (norm); p->endChangeGesture();
    }
}

static juce::String noteWithOctave (float midi)
{
    const int m = (int) std::lround (midi);
    return U8 (noteName (m)) + juce::String (m / 12 - 1);
}

void MJ7TuneProcessor::applyAnalysis()
{
    const auto& r = result;
    summary.clear();
    if (! r.valid || ! report.valid)
    {
        state.store (failed);
        summary.add (U8 ("Pas assez de notes chantées pour analyser. Lancez un passage chanté (pas parlé) et recommencez."));
        return;
    }
    const bool keyOk = r.key >= 0 && r.keyConfidence >= 0.5f;
    if (keyOk)
    {
        undoKey = (int) v (key); undoScale = (int) v (scale); hasUndo = true;
        setParam (key, (float) r.key); setParam (scale, (float) (r.scale == 1 ? Majeure : MineureNat));
        summary.add (U8 ("Tonalité : ") + U8 (noteName (r.key)) + (r.scale == 1 ? U8 (" majeur") : U8 (" mineur"))
                     + (r.keyConfidence >= 0.75f ? U8 (" (sûr)") : U8 (" (probable, vérifiez à l'oreille)")));
        summary.add (U8 ("Gamme : ") + (r.scale == 1 ? U8 ("majeure") : U8 ("mineure")) + U8 (". Pour un chant oriental ou un passage blues, choisissez la gamme à la main."));
    }
    else
        summary.add (U8 ("Tonalité : incertaine sur cet extrait, réglage laissé tel quel. Analysez un refrain ou réglez-la à la main."));

    summary.add (U8 ("Tessiture : de ") + noteWithOctave (report.lowMidi) + U8 (" à ") + noteWithOctave (report.highMidi));
    summary.add (U8 ("Justesse : écart médian ") + juce::String (juce::roundToInt (report.medianCents)) + U8 (" cents, ")
                 + juce::String (juce::roundToInt (report.offPercent)) + U8 (" % des notes à plus de 25 cents"));
    juce::String advice;
    if (report.medianCents < 12.0f && report.offPercent < 15.0f)
        advice = U8 ("Conseil : voix déjà juste. Pour un rendu naturel : style « Naturel » ou « R&B souple ».");
    else if (report.medianCents < 25.0f)
        advice = U8 ("Conseil : justesse correcte. « Pop moderne » ou « Afro / Amapiano » corrigent sans s'entendre.");
    else
        advice = U8 ("Conseil : voix souvent à côté. Vitesse rapide (0 à 15 ms) ou réenregistrez les passages les plus faux.");
    summary.add (advice);
    state.store (done);
}

void MJ7TuneProcessor::undoAnalysis()
{
    if (hasUndo) { setParam (key, (float) undoKey); setParam (scale, (float) undoScale); }
    hasUndo = false; summary.clear();
    if (state.load() == done || state.load() == failed) state.store (idle);
}

// ================================================================================================
//  Styles et etat
// ================================================================================================
void MJ7TuneProcessor::loadFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    index = std::clamp (index, 0, (int) presets.size() - 1);
    std::vector<float> values ((size_t) kNumParams);
    for (int pid = 0; pid < kNumParams; ++pid) values[(size_t) pid] = paramDef (pid).def;
    for (const auto& [pid, value] : presets[(size_t) index].values) values[(size_t) pid] = value;
    for (int pid = 0; pid < kNumParams; ++pid)
    {
        const bool keep = pid == bypass || pid == key || pid == scale || pid == out_gain || (pid >= n0 && pid <= n11);
        if (! keep) setParam (pid, values[(size_t) pid]);
    }
    presetIndex = index;
}

void MJ7TuneProcessor::writeExtras (juce::ValueTree& tree) const
{
    tree.setProperty ("presetIndex", presetIndex, nullptr);
    tree.setProperty ("analysisSummary", summary.joinIntoString ("\n"), nullptr);
    tree.setProperty ("undoKey", hasUndo ? juce::var (undoKey) : juce::var(), nullptr);
    tree.setProperty ("undoScale", undoScale, nullptr);
}

void MJ7TuneProcessor::readExtras (const juce::ValueTree& tree)
{
    presetIndex = std::clamp ((int) tree.getProperty ("presetIndex", 0), 0, (int) factoryPresets().size() - 1);
    summary = juce::StringArray::fromLines (tree.getProperty ("analysisSummary").toString()); summary.removeEmptyStrings();
    hasUndo = tree.hasProperty ("undoKey") && ! tree.getProperty ("undoKey").isVoid();
    if (hasUndo) { undoKey = (int) tree.getProperty ("undoKey"); undoScale = (int) tree.getProperty ("undoScale", 0); }
    if (! summary.isEmpty()) state.store (done);
    else if (state.load() == done || state.load() == failed) state.store (idle);
}

void MJ7TuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = apvts.copyState(); writeExtras (tree);
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
}

void MJ7TuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            readExtras (tree);
        }
}

juce::File MJ7TuneProcessor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MJ7 Tune").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool MJ7TuneProcessor::savePresetFile (const juce::File& file)
{
    auto tree = apvts.copyState(); writeExtras (tree);
    if (auto xml = tree.createXml()) return xml->writeTo (file);
    return false;
}

bool MJ7TuneProcessor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::MemoryBlock block; copyXmlToBinary (*xml, block);
            setStateInformation (block.getData(), (int) block.getSize());
            return true;
        }
    return false;
}

void MJ7TuneProcessor::toggleAB()
{
    abState[abIndex] = apvts.copyState();
    abIndex ^= 1;
    if (abState[abIndex].isValid()) apvts.replaceState (abState[abIndex].createCopy());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MJ7TuneProcessor(); }
