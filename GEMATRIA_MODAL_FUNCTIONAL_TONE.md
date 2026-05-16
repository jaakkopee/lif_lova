# Gematria in Modal-Functional Tone Context

This project treats each **gematria tonal context** as a symbolic-musical profile used by the LIF MIDI engine.

## Concept

A gematria tonal context combines:

- a phrase (`name` / `phrase`)
- an English Extended gematria value (`gematria_code`)
- harmonic shaping parameters (`density`, `brightness`, `color`)
- modal-functional offsets (`tonic_offset`, `subdominant_offset`, `dominant_offset`)
- output behavior (`midi_channel`, `percussion_mode`)

The active context influences three stages of tonal generation:

1. **Modal-functional classification**
   - Energy bins are interpreted as tonic/subdominant/dominant functions.
   - `gematria_code` + `brightness` bias dominant/subdominant thresholds.

2. **Scale-degree selection**
   - Function families use base degree templates.
   - Context offsets and gematria-derived offsets rotate target degrees.

3. **Voicing density and color**
   - `density` controls how sparse or full the chord can be at low drive.
   - `color` controls upper extensions and dominant alterations.

When `percussion_mode=true`, the same gematria code rotates a 16-voice drum map instead of generating harmonic voicings.

## JSON Context File

The app loads contexts from `gematria_tonal_contexts.json`.

Supported shape:

```json
{
  "contexts": [
    {
      "name": "Orphic Black Moon",
      "phrase": "Orphic Black Moon",
      "gematria_code": 506,
      "midi_channel": 1,
      "percussion_mode": false,
      "density": 0.58,
      "brightness": 0.08,
      "color": 0.52,
      "tonic_offset": 0,
      "subdominant_offset": 1,
      "dominant_offset": 2
    }
  ]
}
```

The loader also accepts a plain JSON array of context objects.

## Python Interface (`gematria.py`)

### 1) Calculate English Extended gematria

```bash
python3 gematria.py calc "Orphic Black Moon"
python3 gematria.py calc --file phrases.txt
```

### 2) Generate preset context file

```bash
python3 gematria.py init-contexts --output gematria_tonal_contexts.json
```

### 3) Create or append custom contexts

```bash
python3 gematria.py make-context \
  --name "Hermetic Star Choir" \
  --phrase "Hermetic Star Choir" \
  --midi-channel 2 \
  --density 0.72 \
  --brightness 0.15 \
  --color 0.88 \
  --tonic-offset 1 \
  --subdominant-offset 4 \
  --dominant-offset 6 \
  --append \
  --output gematria_tonal_contexts.json
```

If `--gematria-code` is omitted, the script computes it from `--phrase` using the English Extended cipher.

## App Loading Order

At runtime, the app searches contexts in this order:

1. path from `LIF_LOVA_GEMATRIA_CONTEXTS`
2. `./gematria_tonal_contexts.json`
3. `<build>/gematria_tonal_contexts.json`
4. `<repo>/gematria_tonal_contexts.json` (when running from build tree)

If no JSON is found, built-in fallback contexts are used.
