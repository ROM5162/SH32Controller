# SH-32 Controller — plugin VST3 pour Cubase

Plugin VST3 (instrument, sans audio) qui **pilote le Roland SH-32 en MIDI** depuis Cubase :

- les 51 paramètres du SH-32 (table CC du manuel, p.115, mode Mod2) sont des **paramètres automatisables** de l'hôte, émis en Control Change dès qu'ils changent ;
- un **séquenceur mono / polyphonique** (16 pas, piano roll, 8 mémoires, piste de CC) synchronisé sur le transport de Cubase, ou sur une horloge interne ;
- les CC reçus du SH-32 mettent à jour les potards du plugin ;
- clavier virtuel pour écouter les sons.

## 1. Obtenir le .vst3

Aucun binaire Windows/macOS n'a pu être produit depuis l'environnement de développement (Linux). Deux options :

**A. GitHub Actions (rien à installer)**
1. Crée un dépôt GitHub et pousse ce dossier.
2. Onglet *Actions* → workflow *Build VST3* → télécharge l'artefact `SH32-Controller-windows-vst3` ou `-macos-vst3`.

**B. Compilation locale** (CMake ≥ 3.22, compilateur C++17 ; Visual Studio 2022 ou Xcode)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target SH32Controller_VST3
```
JUCE 9.0.5 est téléchargé automatiquement (ou `-DSH32_JUCE_DIR=chemin`).
Le résultat est `build/SH32Controller_artefacts/Release/VST3/SH-32 Controller.vst3`.

## 2. Installation

- Windows : copier le dossier `SH-32 Controller.vst3` dans `C:\Program Files\Common Files\VST3`
- macOS : `~/Library/Audio/Plug-Ins/VST3` (binaire non signé : si Gatekeeper bloque, `xattr -dr com.apple.quarantine "SH-32 Controller.vst3"`)

Relancer Cubase (ou *Studio → Gestion des plug-ins VST → Mettre à jour*).

## 3. Routage MIDI dans Cubase

Le plugin émet du MIDI ; il faut l'envoyer vers le port MIDI du SH-32 (les intitulés varient selon la version de Cubase) :

1. Ajouter une piste **Instrument** avec *SH-32 Controller*.
2. Créer une **piste MIDI** dont la **sortie** est le port MIDI du SH-32 (USB).
3. Dans la piste Instrument, router la **sortie MIDI du plugin** (souvent « Event Output » / sortie d'événements) vers cette piste MIDI, ou l'activer dans l'inspecteur.
4. Pour jouer le plugin depuis un clavier : armer la piste Instrument. Pour les séquences : lancer le transport (mode *Hôte*) ou utiliser le mode *Interne*.
5. Régler le canal MIDI du plugin sur celui du SH-32.

Notes : à très grande taille de buffer ASIO la précision de la sortie MIDI peut se dégrader ; VST3 ne permet pas d'émettre l'horloge MIDI (pas de MIDI clock out).

## 4. Fonctionnement

- **Paramètres** : un CC n'est envoyé que lorsque la valeur change (pas de rafale au chargement). Boutons « Tout envoyer » et option « Envoyer au Play ».
- **Séquenceur** : les événements sont calculés d'après la position PPQ de l'hôte ; le résultat ne dépend pas de la taille de bloc. Les notes en cours sont coupées à l'arrêt ou lors d'un saut.
- **Session** : paramètres et 8 patterns sont sauvegardés dans le projet Cubase.

## 5. Limites

- Non testé dans Cubase lui-même, ni sur un SH-32 réel : **vérifier la table CC sur l'instrument**.
- Pas de pitch bend, modulation, sustain, SysEx ni horloge MIDI.
- Le plugin compile et passe les tests sous Linux (JUCE 9) ; les builds Windows/macOS sont à valider via GitHub Actions.

## 6. Tests

- `Tests/engine_test.cpp` : moteur du séquenceur (190 vérifications), `-DSH32_BUILD_TESTS=ON`, lancé par `ctest`.
- `Tests/ProcessorTest.cpp` : intégration processeur + éditeur (56 vérifications), `-DSH32_BUILD_HOST_TESTS=ON`.
- Le validateur Steinberg VST3 donne 47/47 sur la version Linux.

## 7. Licences

JUCE 9 : AGPLv3 ou licence commerciale JUCE. Steinberg VST3 SDK : MIT. En distribuant le binaire, respecte la licence JUCE choisie.
