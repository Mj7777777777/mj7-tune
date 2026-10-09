// Test d'integration du MJ7 Tune (sans carte son) :
//   cmake -B build -DMJ7_BUILD_TESTS=ON && cmake --build build --target MJ7HostTest
#include "PluginEditor.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << std::endl; } } while (0)
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Melodie chantee : suite de notes midi (0 = silence), chaque note 'noteSec' secondes, decalee de 'cents', avec vibrato. */
static juce::AudioBuffer<float> melody (double sr, const std::vector<int>& notes, float noteSec, float cents, float level = 0.25f)
{
    const int len = (int) (noteSec * sr);
    juce::AudioBuffer<float> b (2, len * (int) notes.size()); b.clear();
    mj7::Biquad f1, f2; f1.setPeak (700, 2, 8, (float) sr); f2.setPeak (2600, 3, 6, (float) sr);
    auto* d = b.getWritePointer (0); double ph = 0;
    for (size_t k = 0; k < notes.size(); ++k)
        for (int i = 0; i < len; ++i)
        {
            const int idx = (int) k * len + i;
            if (notes[k] <= 0) { d[idx] = f2.process (f1.process (0.0f)); continue; }
            const float t = (float) (i / sr);
            const float f = 440.0f * std::pow (2.0f, ((float) notes[k] - 69.0f + cents / 100.0f + 0.15f * std::sin (2.0f * mj7::kPi * 5.5f * t)) / 12.0f);
            ph += 2.0 * 3.14159265 * f / sr;
            float v = 0; for (int h = 1; h <= 20; ++h) if (f * (float) h < 0.45f * (float) sr) v += (float) std::sin (ph * h) / (float) h;
            const float env = std::min (1.0f, t / 0.03f) * std::min (1.0f, (noteSec - t) / 0.05f);
            d[idx] = f2.process (f1.process (level * v * env));
        }
    b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    return b;
}

static juce::AudioBuffer<float> run (MJ7TuneProcessor& p, const juce::AudioBuffer<float>& in, juce::Random& rnd)
{
    juce::AudioBuffer<float> out (2, in.getNumSamples()); juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples();)
    {
        const int n = juce::jmin (in.getNumSamples() - pos, 1 + rnd.nextInt (1024));
        juce::AudioBuffer<float> block (2, n);
        block.copyFrom (0, 0, in, 0, pos, n); block.copyFrom (1, 0, in, 1, pos, n);
        p.processBlock (block, midi);
        out.copyFrom (0, pos, block, 0, 0, n); out.copyFrom (1, pos, block, 1, 0, n);
        pos += n;
    }
    return out;
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static void set (MJ7TuneProcessor& p, const char* id, float realValue)
{
    auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

/** Hauteur moyenne (midi) mesuree sur la seconde moitie. */
static float meanPitch (const juce::AudioBuffer<float>& b, double sr)
{
    mj7::PitchDetector d; d.prepare ((float) sr); double sum = 0; int n = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (d.push (b.getSample (0, i)) && d.last.voiced && i > b.getNumSamples() / 2) { sum += 69.0 + 12.0 * std::log2 (d.last.freq / 440.0); ++n; }
    return n > 0 ? (float) (sum / n) : 0.0f;
}

static int impulseDelay (MJ7TuneProcessor& p, double sr, juce::Random& rnd)
{
    juce::AudioBuffer<float> imp (2, (int) sr); imp.clear();
    for (int i = 0; i < 64; ++i) { const float w = 0.25f * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 64.0f)); imp.setSample (0, 20000 + i, w); imp.setSample (1, 20000 + i, w); }
    const auto out = run (p, imp, rnd);
    int best = 0; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, best))) best = i;
    return best - 20032;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    juce::Random rnd (11);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "== " << sr << " Hz ==" << std::endl;
        MJ7TuneProcessor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        set (p, "key", 9.0f); set (p, "scale", (float) mj7::MineureNat);
        const auto off = melody (sr, { 69 }, 2.0f, 40.0f);
        for (int i = 0; i < (int) mj7::factoryPresets().size(); ++i)
        {
            for (const auto& [pid, value] : mj7::factoryPresets()[(size_t) i].values)
                CHECK (value >= mj7::paramDef (pid).min && value <= mj7::paramDef (pid).max, "valeur de preset hors limites");
            p.loadFactoryPreset (i);
            pump (100);
            const auto out = run (p, off, rnd);
            const float m = meanPitch (out, sr), peak = out.getMagnitude (0, out.getNumSamples());
            const float expected = 69.0f + (float) juce::roundToInt (p.apvts.getParameter ("transpose")->convertFrom0to1 (p.apvts.getParameter ("transpose")->getValue()));
            std::cout << "  " << juce::String (juce::CharPointer_UTF8 (mj7::factoryPresets()[(size_t) i].name)).paddedRight (' ', 32)
                      << " sortie " << juce::String (100.0f * (m - expected), 0) << " cents (entree +40), crete " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB" << std::endl;
            CHECK (allFinite (out), "valeur non finie");
            CHECK (peak < 1.0f, "saturation");
            CHECK (std::abs (m - expected) < 0.2f, "note non corrigee");
        }
        // latence des deux modes, et bypass
        for (int md : { 0, 1 })
        {
            p.loadFactoryPreset ((int) mj7::factoryPresets().size() - 1);
            set (p, "mode", (float) md); set (p, "tune_on", 0.0f);
            run (p, melody (sr, { 0 }, 0.1f, 0.0f), rnd); pump (200);
            const int d = impulseDelay (p, sr, rnd);
            std::cout << "  mode " << (md == 0 ? "Mix " : "Live") << " : latence declaree " << p.getLatencySamples() << " (" << juce::String (1000.0 * p.getLatencySamples() / sr, 1) << " ms), mesuree " << d << std::endl;
            CHECK (std::abs (d - p.getLatencySamples()) <= 2, "latence declaree differente de la latence mesuree");
            set (p, "bypass", 1.0f);
            CHECK (std::abs (impulseDelay (p, sr, rnd) - p.getLatencySamples()) <= 1, "le bypass n'a pas la meme latence");
            set (p, "bypass", 0.0f); set (p, "tune_on", 1.0f);
        }
        set (p, "mode", 0.0f); pump (200);

        // clavier : couper La -> la voix chantee sur La part sur Si (le plus proche)
        {
            p.loadFactoryPreset (4);                                             // trap hard-tune
            set (p, "key", 9.0f); set (p, "scale", (float) mj7::MineureNat);
            p.toggleNote (9);
            CHECK ((int) p.apvts.getParameter ("scale")->convertFrom0to1 (p.apvts.getParameter ("scale")->getValue()) == mj7::Personnalisee, "le clic ne passe pas en gamme personnalisee");
            CHECK (! ((p.allowedNotes() >> 9) & 1u) && ((p.allowedNotes() >> 11) & 1u) && ((p.allowedNotes() >> 0) & 1u), "notes personnalisees fausses");
            const float m = meanPitch (run (p, off, rnd), sr);
            std::cout << "  La coupe : La+40 cents -> " << juce::String (m, 2) << " (71 = Si)" << std::endl;
            CHECK (std::abs (m - 71.0f) < 0.15f, "la note coupee n'est pas evitee");
            set (p, "scale", (float) mj7::MineureNat);
        }

        // etat
        {
            set (p, "speed", 77.0f);
            juce::MemoryBlock st; p.getStateInformation (st);
            MJ7TuneProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
            bool same = true;
            for (int pid = 0; pid < mj7::kNumParams; ++pid)
                if (std::abs (p.apvts.getParameter (mj7::paramDef (pid).id)->getValue() - q.apvts.getParameter (mj7::paramDef (pid).id)->getValue()) > 1.0e-5f) same = false;
            CHECK (same, "l'etat recharge differe");
        }
    }

    // --- ANALYSER : melodie en Mi mineur, un peu fausse ---
    {
        const double sr = 48000.0;
        MJ7TuneProcessor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (1);
        set (p, "key", 0.0f); set (p, "scale", (float) mj7::Majeure);
        std::vector<int> mel;
        const int phrase[16] = { 64, 67, 71, 69, 67, 66, 64, 0, 64, 62, 64, 67, 66, 64, 59, 0 };   // Mi mineur
        for (int r = 0; r < 4; ++r) for (int n : phrase) mel.push_back (n);
        const auto voice = melody (sr, mel, 0.4f, 18.0f, 0.06f);
        p.startAnalysis();
        run (p, voice, rnd);
        for (int i = 0; i < 300 && p.analysisState() != MJ7TuneProcessor::done && p.analysisState() != MJ7TuneProcessor::failed; ++i) pump (100);
        pump (300);
        CHECK (p.analysisState() == MJ7TuneProcessor::done, "l'analyse n'a pas abouti");
        std::cout << "== resume de l'analyse ==" << std::endl;
        for (const auto& line : p.summary) std::cout << "  " << line << std::endl;
        const int key = (int) p.apvts.getParameter ("key")->convertFrom0to1 (p.apvts.getParameter ("key")->getValue());
        const int sc = (int) p.apvts.getParameter ("scale")->convertFrom0to1 (p.apvts.getParameter ("scale")->getValue());
        CHECK ((key == 4 && sc == mj7::MineureNat) || (key == 7 && sc == mj7::Majeure), "tonalite mal detectee (Mi mineur / Sol majeur attendu)");
        {
            juce::MemoryBlock st; p.getStateInformation (st);
            MJ7TuneProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
            CHECK (q.hasAnalysis() && q.summary.size() == p.summary.size(), "analyse non restauree avec le projet");
        }

        // interface
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* view = ed->getChildComponent (0);
        auto snap = [&] (const juce::String& name)
        {
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f = outDir.getChildFile (name); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        };
        auto click = [view] (const juce::String& text)
        {
            for (auto* c : view->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText() == text && b->onClick) b->onClick();
        };
        auto select = [view] (int index)
        {
            mj7ui::ChainTile* found = nullptr; int tile = 0;
            for (auto* c : view->getChildren()) if (auto* t = dynamic_cast<mj7ui::ChainTile*> (c)) if (tile++ == index) found = t;
            if (found != nullptr && found->onSelect) found->onSelect();
        };
        // la courbe se remplit pendant la lecture : on joue par petits morceaux en laissant l'interface tourner
        const auto bit = melody (sr, { 64, 67, 71, 69, 67, 66 }, 0.45f, 25.0f, 0.06f);
        pump (500);
        snap ("tune_analyse.png");
        click ("COURBE");
        for (int pos = 0; pos + 2048 < bit.getNumSamples(); pos += 2048)
        {
            juce::AudioBuffer<float> b (2, 2048); b.copyFrom (0, 0, bit, 0, pos, 2048); b.copyFrom (1, 0, bit, 1, pos, 2048);
            juce::MidiBuffer midi; p.processBlock (b, midi); pump (20);
        }
        snap ("tune_courbe.png");
        select (1); pump (100); snap ("tune_tonalite.png");
        select (0);
    }
    std::cout << (failures == 0 ? "\nTOUS LES TESTS PASSENT" : "\nECHECS : " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
