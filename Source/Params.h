// MJ7 Tune - liste unique des parametres, des modules et des styles d'usine.
#pragma once
#include "dsp/Tune.h"
#include "dsp/Analyzer.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace mj7
{
inline juce::String U8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// X (identifiant, nom, type, min, max, defaut, centre de course (0 = lineaire), unite, choix)
// Les identifiants ne doivent JAMAIS changer : les projets FL Studio et l'automation en dependent.
#define MJ7_PARAMS(X) \
    X (tune_on,    "Tune actif",        tB, 0, 1, 1, 0, "", "") \
    X (key,        "Tonalité",          tC, 0, 11, 9, 0, "", "Do|Do#|Ré|Ré#|Mi|Fa|Fa#|Sol|Sol#|La|La#|Si") \
    X (scale,      "Gamme",             tC, 0, 11, 2, 0, "", "Chromatique|Majeure|Mineure|Mineure harmonique|Penta majeure|Penta mineure|Blues|Dorien|Mixolydien|Phrygien|Orientale (Hijaz)|Personnalisée") \
    X (speed,      "Vitesse",           tF, 0, 400, 25, 60, "ms", "") \
    X (amount,     "Quantité",          tF, 0, 100, 100, 0, "%", "") \
    X (natural,    "Vibrato naturel",   tF, 0, 100, 30, 0, "%", "") \
    X (humanize,   "Humaniser",         tF, 0, 100, 20, 0, "%", "") \
    X (transpose,  "Transposition",     tI, -12, 12, 0, 0, "dt", "") \
    X (formant,    "Formants",          tF, -12, 12, 0, 0, "dt", "") \
    X (mix,        "Mix",               tF, 0, 100, 100, 0, "%", "") \
    X (mode,       "Mode",              tC, 0, 1, 0, 0, "", "Mix (précis, 30 ms)|Live (5 ms)") \
    X (out_gain,   "Gain sortie",       tF, -24, 24, 0, 0, "dB", "") \
    X (n0,  "Note Do",   tB, 0, 1, 1, 0, "", "") X (n1,  "Note Do#",  tB, 0, 1, 1, 0, "", "") \
    X (n2,  "Note Ré",   tB, 0, 1, 1, 0, "", "") X (n3,  "Note Ré#",  tB, 0, 1, 1, 0, "", "") \
    X (n4,  "Note Mi",   tB, 0, 1, 1, 0, "", "") X (n5,  "Note Fa",   tB, 0, 1, 1, 0, "", "") \
    X (n6,  "Note Fa#",  tB, 0, 1, 1, 0, "", "") X (n7,  "Note Sol",  tB, 0, 1, 1, 0, "", "") \
    X (n8,  "Note Sol#", tB, 0, 1, 1, 0, "", "") X (n9,  "Note La",   tB, 0, 1, 1, 0, "", "") \
    X (n10, "Note La#",  tB, 0, 1, 1, 0, "", "") X (n11, "Note Si",   tB, 0, 1, 1, 0, "", "") \
    X (bypass,     "Bypass",            tB, 0, 1, 0, 0, "", "")

enum PID
{
#define X(id, name, type, mn, mx, def, centre, unit, choices) id,
    MJ7_PARAMS (X)
#undef X
    kNumParams
};

enum class PType { tF, tI, tB, tC };
struct ParamDef { const char* id; const char* name; PType type; float min, max, def, centre; const char* unit; const char* choices; };

inline const ParamDef& paramDef (int pid)
{
    static const ParamDef defs[] = {
#define X(id, name, type, mn, mx, def, centre, unit, choices) { #id, name, PType::type, (float) (mn), (float) (mx), (float) (def), (float) (centre), unit, choices },
        MJ7_PARAMS (X)
#undef X
    };
    return defs[pid];
}

inline const char* noteName (int n)
{
    static const char* names[12] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    return names[((n % 12) + 12) % 12];
}

enum Family { Correction = 0, Dynamique, Couleur, Espace, Neutre };
struct ModuleDef { const char* name; const char* title; const char* help; Family family; int bypass; int meter; std::vector<int> params; };
enum Meter { kNumMeters = 1 };

inline const std::vector<ModuleDef>& modules()
{
    static const std::vector<ModuleDef> m = {
        { "Tonalité", "Tonalité et gamme", "La tonalité du morceau. ANALYSER la trouve. Personnalisée : cliquez les notes du clavier pour choisir celles qui sont permises.", Correction, -1, -1,
          { key, scale } },
        { "Correction", "Correction", "Vitesse : 0 ms = effet robot, 20 à 50 ms = moderne, 80 ms et plus = naturel. Vibrato naturel et Humaniser gardent le chant vivant.", Correction, tune_on, -1,
          { speed, amount, natural, humanize } },
        { "Hauteur", "Hauteur et timbre", "Transposition par demi-tons. Formants : timbre plus grave ou plus aigu sans changer la note.", Couleur, -1, -1,
          { transpose, formant } },
        { "Sortie", "Sortie", "Mix entre voix d'origine et voix corrigée. Mode Live : 5 ms de latence pour s'écouter en enregistrant (correction un peu moins précise sur le vibrato).", Neutre, -1, -1,
          { mix, mode, out_gain } },
    };
    return m;
}

struct FactoryPreset { const char* name; std::vector<std::pair<int, float>> values; };

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> p = {
        { "Naturel (correction invisible)", { { speed, 70 }, { amount, 85 }, { natural, 80 }, { humanize, 50 } } },
        { "Pop moderne",                    { { speed, 30 }, { natural, 45 }, { humanize, 30 } } },
        { "Afro / Amapiano",                { { speed, 12 }, { natural, 25 }, { humanize, 20 } } },
        { "R&B souple",                     { { speed, 45 }, { natural, 60 }, { humanize, 40 } } },
        { "Trap hard-tune",                 { { speed, 0 }, { natural, 0 }, { humanize, 0 } } },
        { "Robot (effet total)",            { { speed, 0 }, { amount, 100 }, { natural, 0 }, { humanize, 0 }, { formant, 1 } } },
        { "Voix grave pitchée",             { { speed, 5 }, { natural, 10 }, { humanize, 0 }, { transpose, -3 }, { formant, -2 } } },
        { "Voix aiguë / chipmunk",          { { speed, 5 }, { natural, 10 }, { humanize, 0 }, { transpose, 5 }, { formant, 3 } } },
        { "Live scène (faible latence)",    { { speed, 20 }, { natural, 40 }, { humanize, 30 }, { mode, 1 } } },
        { "Neutre (point de départ)",       {} },
    };
    return p;
}
} // namespace mj7
