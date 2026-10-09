# MJ7 Tune

Autotune pour FL Studio (VST3 sur Windows et Mac, Audio Unit sur Mac) : de la correction invisible à
l'effet robot, avec détection de la tonalité, gammes orientales et courbe de hauteur en direct.

## Obtenir le plugin prêt à installer

1. Onglet **Actions** : la compilation « Compiler le plugin » démarre à chaque envoi de fichiers (10 à 20 minutes).
2. Quand elle est verte, ouvrez **Releases** et téléchargez `MJ7-Tune-Windows.zip` ou `MJ7-Tune-macOS.zip`.
3. Suivez `INSTALLATION.txt`, présent dans chaque archive.

## Fonctions

- **ANALYSER** : écoute 20 secondes de chant, trouve la tonalité et la gamme, mesure la justesse
  (écart médian en cents) et la tessiture, puis conseille un style.
- **Vitesse** : de 0 ms (saut instantané, effet robot) à 400 ms (très doux).
- **Vibrato naturel** : la correction suit le centre de la note, le vibrato du chanteur reste.
- **Humaniser** : plus une note est tenue, plus la correction s'adoucit.
- **Clavier** : notes permises, note chantée en direct ; un clic coupe ou ajoute une note.
- **12 gammes** : chromatique, majeure, mineure, mineure harmonique, pentatoniques, blues, dorien,
  mixolydien, phrygien, orientale (Hijaz), personnalisée.
- **Transposition** et **formants** (timbre) indépendants.
- **Mode Mix** (environ 30 ms, correction alignée sur la mesure de hauteur) ou **Live** (5 ms).

## Tests

- `tests/dsp_tests.cpp` : correction d'une note fausse, vibrato aplati ou gardé, note hors gamme,
  mesure de justesse, à 44,1, 48 et 96 kHz ; lancé à chaque compilation sur GitHub.
- `tests/host_test.cpp` : plugin complet (tous les styles, latence des deux modes, clavier,
  ANALYSER sur une mélodie, sauvegarde du projet, captures de l'interface).
