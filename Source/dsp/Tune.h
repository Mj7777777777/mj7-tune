// MJ7 Tune - correction de justesse (autotune) : gammes, notes actives, vibrato naturel, humanisation.
#pragma once
#include "Pitch.h"
#include <array>

namespace mj7
{
/** Gammes proposees. Chaque masque donne les 12 degres autorises a partir de la tonique (bit 0 = tonique). */
enum Scale { Chromatique = 0, Majeure, MineureNat, MineureHarm, PentaMaj, PentaMin, Blues, Dorien, Mixolydien, Phrygien, Hijaz, Personnalisee, kNumScales };

inline uint16_t scaleMask (int scale) noexcept
{
    // degres : 0  1  2  3  4  5  6  7  8  9 10 11
    static const char* rows[kNumScales] = {
        "111111111111",   // chromatique
        "101011010101",   // majeure
        "101101011010",   // mineure naturelle
        "101101011001",   // mineure harmonique
        "101010010100",   // pentatonique majeure
        "100101010010",   // pentatonique mineure
        "100101110010",   // blues
        "101101010110",   // dorien
        "101011010110",   // mixolydien
        "110101011010",   // phrygien
        "110011011010",   // hijaz (oriental : seconde augmentee)
        "111111111111" }; // personnalisee : remplacee par les notes choisies
    const char* r = rows[std::clamp (scale, 0, kNumScales - 1)];
    uint16_t m = 0;
    for (int i = 0; i < 12; ++i) if (r[i] == '1') m = (uint16_t) (m | (1u << i));
    return m;
}

/** Masque absolu (bit i = note i, Do = 0) a partir de la tonique et d'un masque relatif. */
inline uint16_t absoluteMask (int key, uint16_t relative) noexcept
{
    uint16_t m = 0;
    for (int d = 0; d < 12; ++d) if (relative & (1u << d)) m = (uint16_t) (m | (1u << ((d + key) % 12)));
    return m != 0 ? m : (uint16_t) 0x0FFF;
}

inline bool noteAllowed (uint16_t absMask, int midiNote) noexcept { return (absMask >> (((midiNote % 12) + 12) % 12)) & 1u; }

struct TuneParams
{
    bool enabled = true;
    uint16_t notes = 0x0FFF;      // masque absolu des notes autorisees
    float speedMs = 25.0f;        // 0 = effet robot
    float amount = 1.0f;          // part de la correction appliquee
    float natural = 0.3f;         // 0 = note plate, 1 = vibrato garde entierement
    float humanize = 0.2f;        // ralentit la correction sur les notes tenues
    float transpose = 0.0f, formant = 0.0f, mix = 1.0f;
};

/** Moteur d'autotune (PSOLA, meme principe que le MJ7 Vocal Chain).
    - vitesse : temps mis pour rejoindre la note juste ; 0 ms = saut instantane (effet robot) ;
    - vibrato naturel : la correction suit le centre de la note, pas chaque oscillation ;
    - humaniser : plus une note est tenue, plus la correction s'adoucit (les notes courtes restent nettes). */
class TuneEngine
{
public:
    static constexpr int kMaxGrains = 12, kHistory = 512;

    void prepare (float sampleRate)
    {
        sr = sampleRate; det.prepare (sr);
        maxPeriod = sr / 75.0f; unvoicedPeriod = sr / 200.0f;
        const int size = (int) (0.2f * sr);
        inL.prepare (size); inR.prepare (size); maxDelay = (float) (inL.mask - 4);
        lat = alignedLatency();
        aEnable = coefFromMs (12.0f, sr); aAvg = coefFromMs (10.0f, sr); aMix = coefFromMs (20.0f, sr);
        reset();
    }
    void setLatencySamples (int n) noexcept { if (n != lat) { lat = n; resetGrains(); } }
    /** Latence du mode Mix : la correction est appliquee exactement sur le passage ou la hauteur a ete
        mesuree (le vibrato rapide est alors bien suivi). En dessous (mode Live), elle arrive un peu en retard. */
    int alignedLatency() const noexcept { return det.lagSamples() + (int) std::round (0.0015f * sr); }
    int latency() const noexcept { return lat; }

    void reset() noexcept
    {
        inL.reset(); inR.reset(); det.reset(); now = 0;
        shift = desired = transSm = 0.0f; lastTarget = -1; histN = 0; voiced = false; detVoiced = false; detHold = 0.0f;
        period = unvoicedPeriod; midiDet = centre = 60.0f; enableSm = 0.0f; mixSm = 1.0f; holdSec = 0.0f;
        detectedMidi.store (0.0f); targetMidi.store (0.0f); outputMidi.store (0.0f);
        qRead = qWrite = 0;
        for (int i = 0; i < kHistory; ++i) { histIn[i] = 0.0f; histOut[i] = 0.0f; }
        histPos.store (0);
        resetGrains();
    }
    void setParams (const TuneParams& p) noexcept
    {
        prm = p;
        formantRatio = std::pow (2.0f, clampf (p.formant, -12.0f, 12.0f) / 12.0f);
        aTrans = coefFromMs (10.0f, sr);
        aCentre = coefFromMs (140.0f, sr);
    }

    void process (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            if (det.push (0.5f * (L[i] + R[i]))) update();
            inL.write (L[i]); inR.write (R[i]); ++now;
            while (qRead != qWrite && queue[qRead].at <= now) { apply (queue[qRead]); qRead = (qRead + 1) % kQueue; }

            const float en = enableSm = (prm.enabled ? 1.0f : 0.0f) + aEnable * (enableSm - (prm.enabled ? 1.0f : 0.0f));
            const float mx = mixSm = prm.mix + aMix * (mixSm - prm.mix);
            // vitesse effective : plus la note est tenue, plus la correction est douce (humaniser)
            const float hold = clampf (holdSec / 0.6f, 0.0f, 1.0f);
            const float speed = prm.speedMs + prm.humanize * hold * (180.0f + prm.speedMs);
            const float aSpeed = coefFromMs (speed, sr);
            shift = desired + aSpeed * (shift - desired);
            transSm = prm.transpose + aTrans * (transSm - prm.transpose);
            const float tIn = voiced ? period : unvoicedPeriod;
            const float outMidi = midiDet + shift + transSm;

            const float dryL = inL.read (lat + 1), dryR = inR.read (lat + 1);
            float yl = dryL, yr = dryR;
            const float wet = en * mx;
            if (wet > 0.001f)
            {
                spawn (voiced ? periodOf (outMidi) : tIn, tIn);
                float gl = 0.0f, gr = 0.0f;
                render (gl, gr);
                yl = dryL + wet * (gl - dryL); yr = dryR + wet * (gr - dryR);
            }
            L[i] = yl; R[i] = yr;
            lastOut = voiced ? outMidi : 0.0f;
        }
    }

    std::atomic<float> detectedMidi { 0.0f }, targetMidi { 0.0f }, outputMidi { 0.0f };   // pour l'affichage (0 = pas de note)
    /** Historique pour la courbe : hauteur chantee et hauteur corrigee (0 = silence), une valeur par analyse (~6 ms). */
    float histIn[kHistory] = {}, histOut[kHistory] = {};
    std::atomic<int> histPos { 0 };

private:
    struct Grain { double src = 0.0, centre = 0.0; float half = 1.0f; bool active = false; };

    float periodOf (float midi) const noexcept { return clampf (sr / (440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f)), 12.0f, 2.0f * maxPeriod); }
    void resetGrains() noexcept { for (auto& g : grains) g.active = false; nextCentre = (double) now + maxPeriod; hasLast = false; avgW = 1.0f; }

    void spawn (float tOut, float tIn) noexcept
    {
        const float half = clampf (tIn / formantRatio, 8.0f, 2.0f * maxPeriod);
        const double t = (double) now;
        if (nextCentre < t - 4.0 * maxPeriod) { nextCentre = t + half; hasLast = false; }
        int guard = 0;
        while (t >= nextCentre - half && guard++ < 4)
        {
            Grain* g = nullptr;
            for (auto& c : grains) if (! c.active) { g = &c; break; }
            if (g != nullptr)
            {
                const double ideal = nextCentre - (double) lat;
                double src = ideal;
                if (voiced && hasLast)
                {
                    const double k = std::round ((ideal - lastSrc) / tIn);
                    if (std::abs (k) <= 6.0) src = lastSrc + k * tIn;
                }
                const double hf = (double) half * formantRatio;
                for (int s = 0; s < 6 && (src + hf > nextCentre + half - 3.0 || src - hf > nextCentre - half - 3.0); ++s) src -= tIn;
                g->src = src; g->centre = nextCentre; g->half = half; g->active = true;
                lastSrc = src; hasLast = voiced;
            }
            nextCentre += std::max (8.0f, tOut);
        }
    }
    void render (float& outL, float& outR) noexcept
    {
        const double t = (double) now;
        float sumW = 0.0f, l = 0.0f, r = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float dt = (float) (t - g.centre), u = dt / g.half;
            if (u >= 1.0f) { g.active = false; continue; }
            if (u <= -1.0f) continue;
            const float w = 0.5f + 0.5f * std::cos (kPi * u);
            const float delay = clampf ((float) (t - (g.src + (double) dt * formantRatio)), 1.0f, maxDelay);
            l += w * inL.readCubic (delay + 1.0f); r += w * inR.readCubic (delay + 1.0f);
            sumW += w;
        }
        avgW = sumW + aAvg * (avgW - sumW);
        const float norm = 1.0f / std::max (sumW, clampf (avgW, 0.4f, 1.0f));
        outL = l * norm; outR = r * norm;
    }
    void pushHistory (float in, float out) noexcept
    {
        const int p = histPos.load (std::memory_order_relaxed);
        histIn[p] = in; histOut[p] = out;
        histPos.store ((p + 1) % kHistory, std::memory_order_release);
    }
    void update() noexcept
    {
        const auto r = det.last;
        const float hopSec = (float) det.hopSamples() / sr;
        const long long at = now + std::max (0, lat - det.lagSamples());
        if (! r.voiced)
        {
            detVoiced = false; histN = 0; detHold = 0.0f;
            push ({ at, false, midiDetLast, 0.0f, 0.0f });
            detectedMidi.store (0.0f); targetMidi.store (0.0f); outputMidi.store (0.0f);
            pushHistory (0.0f, 0.0f);
            return;
        }
        float midi = 69.0f + 12.0f * std::log2 (r.freq / 440.0f);
        hist[histN % 3] = midi; ++histN;
        if (histN >= 3) { const float a = hist[0], b = hist[1], c = hist[2]; midi = std::max (std::min (a, b), std::min (std::max (a, b), c)); }
        if (! detVoiced) centre = midi;                                    // debut de note
        detVoiced = true; midiDetLast = midi;
        // centre de la note : moyenne glissante (~140 ms), le vibrato oscille autour
        const float aC = std::pow (aCentre, (float) det.hopSamples());
        centre = midi + aC * (centre - midi);
        if (std::abs (midi - centre) > 1.2f) centre = midi;               // changement de note franc

        const float ref = midi + prm.natural * (centre - midi);           // ce que l'on corrige
        int best = (int) std::lround (ref); float bestDist = 100.0f;
        for (int nte = (int) std::lround (ref) - 6; nte <= (int) std::lround (ref) + 6; ++nte)
            if (noteAllowed (prm.notes, nte)) { const float d = std::abs ((float) nte - ref); if (d < bestDist) { bestDist = d; best = nte; } }
        if (lastTarget >= 0 && noteAllowed (prm.notes, lastTarget) && std::abs ((float) lastTarget - ref) < bestDist + 0.2f) best = lastTarget;
        detHold = best == lastTarget ? detHold + hopSec : 0.0f;
        lastTarget = best;
        push ({ at, true, midi, ((float) best - ref) * prm.amount, detHold });
        detectedMidi.store (midi); targetMidi.store ((float) best);
        outputMidi.store (lastOut > 0.0f ? lastOut : midi);
        pushHistory (midi, lastOut > 0.0f ? lastOut : midi);
    }

    struct Snap { long long at; bool voiced; float midi, desired, hold; };
    static constexpr int kQueue = 64;
    void push (const Snap& s) noexcept
    {
        const int next = (qWrite + 1) % kQueue;
        if (next == qRead) { apply (queue[qRead]); qRead = (qRead + 1) % kQueue; }   // ne devrait pas arriver
        queue[qWrite] = s; qWrite = next;
        if (s.at <= now) while (qRead != qWrite && queue[qRead].at <= now) { apply (queue[qRead]); qRead = (qRead + 1) % kQueue; }
    }
    void apply (const Snap& s) noexcept
    {
        if (! s.voiced) { voiced = false; desired = 0.0f; holdSec = 0.0f; return; }
        if (! voiced) shift = 0.0f;                                         // debut de note : pas de glissement herite
        voiced = true; midiDet = s.midi; desired = s.desired; holdSec = s.hold;
        period = clampf (sr / (440.0f * std::pow (2.0f, (s.midi - 69.0f) / 12.0f)), 16.0f, maxPeriod);
    }
    Snap queue[kQueue] = {}; int qRead = 0, qWrite = 0;
    float midiDetLast = 60.0f, detHold = 0.0f; bool detVoiced = false;

    PitchDetector det; DelayLine inL, inR; Grain grains[kMaxGrains]; TuneParams prm;
    long long now = 0; double nextCentre = 0.0, lastSrc = 0.0; bool hasLast = false;
    float sr = 48000.0f, maxPeriod = 640.0f, unvoicedPeriod = 240.0f, period = 240.0f, maxDelay = 8000.0f, avgW = 1.0f;
    int lat = 768, lastTarget = -1, histN = 0;
    float shift = 0.0f, desired = 0.0f, transSm = 0.0f, hist[3] = {}, midiDet = 60.0f, centre = 60.0f, holdSec = 0.0f, lastOut = 0.0f;
    float formantRatio = 1.0f, aEnable = 0.0f, aAvg = 0.0f, aMix = 0.0f, aTrans = 0.0f, aCentre = 0.0f, enableSm = 0.0f, mixSm = 1.0f;
    bool voiced = false;
};

/** Mesure de justesse d'un enregistrement : ecart (en cents) entre chaque note chantee et la note
    autorisee la plus proche. Sert au bouton ANALYSER. */
struct TuningReport { bool valid = false; float medianCents = 0.0f, offPercent = 0.0f, lowMidi = 0.0f, highMidi = 0.0f; };

inline TuningReport measureTuning (const float* x, int n, float sr, uint16_t absMask)
{
    TuningReport rep;
    PitchDetector d; d.prepare (sr);
    std::vector<float> midis;
    for (int i = 0; i < n; ++i)
        if (d.push (x[i]) && d.last.voiced && d.last.clarity > 0.85f)
            midis.push_back (69.0f + 12.0f * std::log2 (d.last.freq / 440.0f));
    if (midis.size() < 40) return rep;
    std::vector<float> dev; dev.reserve (midis.size());
    for (float m : midis)
    {
        float best = 100.0f;
        for (int nte = (int) std::lround (m) - 6; nte <= (int) std::lround (m) + 6; ++nte)
            if (noteAllowed (absMask, nte)) best = std::min (best, std::abs (m - (float) nte));
        dev.push_back (100.0f * best);
    }
    auto sorted = dev; std::sort (sorted.begin(), sorted.end());
    rep.medianCents = sorted[sorted.size() / 2];
    int off = 0; for (float c : dev) if (c > 25.0f) ++off;
    rep.offPercent = 100.0f * (float) off / (float) dev.size();
    auto ms = midis; std::sort (ms.begin(), ms.end());
    rep.lowMidi = ms[ms.size() / 20]; rep.highMidi = ms[ms.size() - 1 - ms.size() / 20];
    rep.valid = true;
    return rep;
}
} // namespace mj7
