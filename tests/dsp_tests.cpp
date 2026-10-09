// Tests du moteur MJ7 Tune, sans JUCE :
//   g++ -std=c++20 -O2 -I Source tests/dsp_tests.cpp -o dsp_tests && ./dsp_tests
#include "dsp/Tune.h"
#include <cstdio>

using namespace mj7;
static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  ECHEC: %s\n", msg); } } while (0)

/** Voix de synthese : f0 (Hz) avec vibrato (cents) a 5,5 Hz. */
static std::vector<float> voice (float sr, float seconds, float f0, float vibCents)
{
    std::vector<float> x ((size_t) (sr * seconds)); double ph = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float t = (float) i / sr;
        const float f = f0 * std::pow (2.0f, vibCents / 1200.0f * std::sin (2.0f * kPi * 5.5f * t));
        ph += 2.0 * 3.14159265 * f / sr;
        float v = 0; for (int h = 1; h <= 16; ++h) if (f * (float) h < 0.45f * sr) v += (float) std::sin (ph * h) / (float) h;
        x[i] = 0.3f * v;
    }
    return x;
}

/** Hauteurs mesurees (midi) sur la seconde moitie du signal. */
static std::vector<float> pitches (const std::vector<float>& x, float sr)
{
    PitchDetector d; d.prepare (sr); std::vector<float> out;
    for (size_t i = 0; i < x.size(); ++i)
        if (d.push (x[i]) && d.last.voiced && i > x.size() / 2) out.push_back (69.0f + 12.0f * std::log2 (d.last.freq / 440.0f));
    return out;
}

static std::vector<float> runEngine (const std::vector<float>& in, float sr, const TuneParams& p, bool live = false)
{
    TuneEngine e; e.prepare (sr); if (live) e.setLatencySamples ((int) std::round (0.005f * sr)); e.setParams (p);
    std::vector<float> L (in), R (in);
    for (size_t pos = 0; pos < in.size(); pos += 300) e.process (L.data() + pos, R.data() + pos, (int) std::min<size_t> (300, in.size() - pos));
    return L;
}

int main()
{
    // gammes
    CHECK (scaleMask (Majeure) == 0b101010110101, "masque de la gamme majeure faux");
    const uint16_t aMinor = absoluteMask (9, scaleMask (MineureNat));
    CHECK (noteAllowed (aMinor, 69) && noteAllowed (aMinor, 72) && ! noteAllowed (aMinor, 70) && ! noteAllowed (aMinor, 73), "La mineur faux");
    CHECK (noteAllowed (absoluteMask (4, scaleMask (Hijaz)), 65) && noteAllowed (absoluteMask (4, scaleMask (Hijaz)), 68), "gamme orientale fausse");

    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        std::printf ("== %.0f Hz ==\n", sr);
        // voix fausse de +40 cents sur La : effet dur -> note juste, plate
        const float f0 = 440.0f * std::pow (2.0f, 40.0f / 1200.0f);
        {
            TuneParams p; p.notes = aMinor; p.speedMs = 0; p.natural = 0; p.humanize = 0;
            for (bool live : { false, true })
            {
                const auto out = pitches (runEngine (voice (sr, 2.0f, f0, 0.0f), sr, p, live), sr);
                float mean = 0, worst = 0; for (float m : out) { mean += m; worst = std::max (worst, std::abs (m - 69.0f)); }
                mean /= (float) std::max<size_t> (1, out.size());
                std::printf ("  +40 cents, dur%s : sortie %.1f cents (pire %.1f)\n", live ? " (live)" : "", 100.0f * (mean - 69.0f), 100.0f * worst);
                CHECK (out.size() > 50 && std::abs (mean - 69.0f) < 0.06f && worst < 0.15f, "la note n'est pas corrigee");
            }
        }
        // vibrato : 0 = aplati, 100 % = garde
        for (float nat : { 0.0f, 1.0f })
        {
            TuneParams p; p.notes = aMinor; p.speedMs = 0; p.natural = nat; p.humanize = 0;
            const auto out = pitches (runEngine (voice (sr, 2.5f, f0, 60.0f), sr, p), sr);
            float lo = 1e9f, hi = -1e9f, mean = 0;
            for (float m : out) { lo = std::min (lo, m); hi = std::max (hi, m); mean += m; }
            mean /= (float) std::max<size_t> (1, out.size());
            const float depth = 100.0f * (hi - lo) * 0.5f;
            std::printf ("  vibrato 60 cents, naturel %.0f %% : profondeur %.0f cents, centre %.1f cents\n", nat * 100.0f, depth, 100.0f * (mean - 69.0f));
            if (nat == 0.0f) CHECK (depth < 25.0f, "le vibrato n'est pas aplati en mode dur");
            else             CHECK (depth > 40.0f && std::abs (mean - 69.0f) < 0.12f, "le vibrato n'est pas garde autour de la note juste");
        }
        // quantite 0 : rien ne change
        {
            TuneParams p; p.notes = aMinor; p.amount = 0.0f;
            const auto out = pitches (runEngine (voice (sr, 2.0f, f0, 0.0f), sr, p), sr);
            float mean = 0; for (float m : out) mean += m; mean /= (float) std::max<size_t> (1, out.size());
            CHECK (std::abs (100.0f * (mean - 69.0f) - 40.0f) < 6.0f, "quantite 0 modifie la note");
        }
        // note coupee : Sib chante, gamme sans Sib -> La ou Si
        {
            TuneParams p; p.notes = aMinor; p.speedMs = 0; p.natural = 0;
            const auto out = pitches (runEngine (voice (sr, 2.0f, 440.0f * std::pow (2.0f, 1.2f / 12.0f), 0.0f), sr, p), sr);
            float mean = 0; for (float m : out) mean += m; mean /= (float) std::max<size_t> (1, out.size());
            std::printf ("  La# (hors gamme) -> %.2f (71 = Si)\n", mean);
            CHECK (std::abs (mean - 71.0f) < 0.1f, "une note hors gamme n'est pas ramenee");
        }
        // mesure de justesse
        {
            const auto x = voice (sr, 3.0f, f0, 0.0f);
            const auto rep = measureTuning (x.data(), (int) x.size(), sr, aMinor);
            std::printf ("  justesse mesuree : %.0f cents\n", rep.medianCents);
            CHECK (rep.valid && std::abs (rep.medianCents - 40.0f) < 5.0f, "mesure de justesse fausse");
        }
    }
    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\nECHECS : %d\n", failures);
    return failures == 0 ? 0 : 1;
}
