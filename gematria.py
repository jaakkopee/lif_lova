#!/usr/bin/env python3
"""Gematria utility for English Extended cipher and tonal context JSON generation.

Tonal-context parameters (density, brightness, color, offsets) are derived from
the phrase gematria value using two complementary approaches:

1. **Numerological tree** – each letter's gematria value is reduced to its digital
   root (1–9) via repeated digit-sum.  The roots are mapped to tonal-function
   weights (T/S/D) via the standard diatonic-degree classification:
       T (tonic)       : roots 1, 3, 6, 8, 9
       S (subdominant) : roots 2, 4, 6
       D (dominant)    : roots 5, 7, 2

2. **FFT spectral analysis** – letter gematria values are treated as a sampled
   signal at three granularity levels (word → syllable → letter, mirroring the
   order in which language develops in infants and historically).  At each level
   the magnitude spectrum is compared via cosine-similarity to reference spectral
   profiles for T, S and D.

Both estimates are blended (letters weighted most heavily) into a single
(t_frac, s_frac, d_frac) triple that drives all context parameters.
Numerological-path intermediate nodes determine degree offsets so that the full
tree is taken into account, not just the final root.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

try:
    import numpy as np
    from numpy.fft import rfft as _rfft

    _HAS_NUMPY = True
except ImportError:  # pragma: no cover
    _HAS_NUMPY = False

# English Extended cipher:
# A..J -> 1..10, K..R -> 20..90, S..Z -> 100..800.
ENGLISH_EXTENDED_VALUES = {
    **{chr(ord("A") + i): i + 1 for i in range(10)},
    "K": 20,
    "L": 30,
    "M": 40,
    "N": 50,
    "O": 60,
    "P": 70,
    "Q": 80,
    "R": 90,
    "S": 100,
    "T": 200,
    "U": 300,
    "V": 400,
    "W": 500,
    "X": 600,
    "Y": 700,
    "Z": 800,
}


@dataclass(frozen=True)
class GematriaResult:
    text: str
    normalized: str
    value: int


@dataclass
class TonalContext:
    name: str
    phrase: str
    gematria_code: int
    midi_channel: int = 1
    percussion_mode: bool = False
    density: float = 0.65
    brightness: float = 0.0
    color: float = 0.55
    tonic_offset: int = 0
    subdominant_offset: int = 0
    dominant_offset: int = 0


def normalize_letters(text: str) -> str:
    return "".join(ch for ch in text.upper() if ch in ENGLISH_EXTENDED_VALUES)


def gematria_value(text: str) -> int:
    return sum(ENGLISH_EXTENDED_VALUES[ch] for ch in normalize_letters(text))


def calculate_many(items: Iterable[str]) -> list[GematriaResult]:
    results: list[GematriaResult] = []
    for item in items:
        normalized = normalize_letters(item)
        results.append(GematriaResult(item, normalized, gematria_value(item)))
    return results


# ─────────────────────────────────────────────────────────────────────────────
#  NUMEROLOGICAL TREE
# ─────────────────────────────────────────────────────────────────────────────

def digital_root(n: int) -> int:
    """Reduce *n* to its digital root (1–9) via repeated digit-sum.

    The decimal-system numerological tree has nine roots (1–9).  This
    function implements the closed-form equivalent of iterating digit sums.
    Examples: 123 → 6, 999 → 27 → 9, 506 → 11 → 2, 0.026 (×1000→26) → 8.
    Zero and negative inputs return 0.
    """
    if n <= 0:
        return 0
    return 1 + (n - 1) % 9


def numerological_path(n: int) -> list[int]:
    """Return the full reduction chain [n, …, digital_root(n)].

    Interior nodes between the original value and the root encode
    intermediate character; all are taken into account when deriving
    parameters via the numerological tree.

    Examples::

        506  → [506, 11, 2]
        999  → [999, 27, 9]
        1350 → [1350, 9]    (root reached in one step)
        1241 → [1241, 8]
    """
    path = [n]
    while n > 9:
        n = sum(int(d) for d in str(n))
        path.append(n)
    return path


# ─────────────────────────────────────────────────────────────────────────────
#  SYLLABIFICATION
# ─────────────────────────────────────────────────────────────────────────────

_VOWELS: frozenset[str] = frozenset("AEIOU")


def _simple_syllabify(word: str) -> list[str]:
    """Rough syllabification of *word* by vowel-consonant structure.

    Implements the maximum-onset principle: leading consonants attach to
    the following vowel nucleus; a single coda consonant is taken only when
    the next character is also a consonant (so it does not steal the onset
    of the following syllable).
    """
    letters = normalize_letters(word)
    if not letters:
        return []
    syllables: list[str] = []
    i = 0
    while i < len(letters):
        syl = ""
        # Leading consonant cluster
        while i < len(letters) and letters[i] not in _VOWELS:
            syl += letters[i]
            i += 1
        # Vowel nucleus
        while i < len(letters) and letters[i] in _VOWELS:
            syl += letters[i]
            i += 1
        # Optional coda: one consonant whose successor is also a consonant
        if (
            i < len(letters)
            and letters[i] not in _VOWELS
            and (i + 1 >= len(letters) or letters[i + 1] not in _VOWELS)
        ):
            syl += letters[i]
            i += 1
        if syl:
            syllables.append(syl)
    return syllables if syllables else [letters]


# ─────────────────────────────────────────────────────────────────────────────
#  T / S / D FUNCTIONAL CLASSIFICATION  (numerological tree branch)
# ─────────────────────────────────────────────────────────────────────────────

# Standard diatonic-degree classification into tonal functions.
# Each digital root (1–9) maps to (t_weight, s_weight, d_weight) summing to 1.
# Degree 6 is shared T/S; degree 2 is shared S/D.
_DEGREE_TSD: dict[int, tuple[float, float, float]] = {
    1: (1.0, 0.0, 0.0),   # scale degree 1 – root / tonic
    2: (0.0, 0.5, 0.5),   # scale degree 2 – supertonic (shared S/D)
    3: (1.0, 0.0, 0.0),   # scale degree 3 – mediant (tonic family)
    4: (0.0, 1.0, 0.0),   # scale degree 4 – subdominant
    5: (0.0, 0.0, 1.0),   # scale degree 5 – dominant
    6: (0.5, 0.5, 0.0),   # scale degree 6 – submediant (shared T/S)
    7: (0.0, 0.0, 1.0),   # scale degree 7 – leading tone (dominant family)
    8: (1.0, 0.0, 0.0),   # scale degree 8 = octave of 1 (tonic)
    9: (0.6, 0.2, 0.2),   # ninefold completion – mostly tonic, some ambiguity
}


def _letter_tsd(letter: str) -> tuple[float, float, float]:
    """T/S/D weights for a single letter via its gematria digital root."""
    val = ENGLISH_EXTENDED_VALUES.get(letter.upper(), 0)
    root = digital_root(val)
    return _DEGREE_TSD.get(root, (1 / 3, 1 / 3, 1 / 3))


def tsd_from_text(text: str) -> tuple[float, float, float]:
    """Aggregate normalised T/S/D fractions from each letter's digital-root function."""
    t = s = d = 0.0
    for letter in normalize_letters(text):
        wt, ws, wd = _letter_tsd(letter)
        t += wt
        s += ws
        d += wd
    total = t + s + d
    if total == 0:
        return (1 / 3, 1 / 3, 1 / 3)
    return (t / total, s / total, d / total)


# ─────────────────────────────────────────────────────────────────────────────
#  FFT SPECTRAL ANALYSIS  (Fourier branch)
# ─────────────────────────────────────────────────────────────────────────────

# Eight-bin T/S/D reference spectra.
# These encode the characteristic spectral shape of each tonal function:
#   T: energy concentrated in low harmonics (stability, consonance)  → decaying
#   S: mid-harmonic peak (motion, expansion, plagal lift)            → plateau
#   D: rising toward high harmonics (tension, tritone, leading-tone) → ascending
# Values are relative amplitudes; cosine similarity is scale-invariant.
_T_REF: list[float] = [1.00, 0.85, 0.65, 0.40, 0.25, 0.15, 0.08, 0.03]
_S_REF: list[float] = [0.25, 0.45, 0.80, 0.95, 0.75, 0.50, 0.25, 0.10]
_D_REF: list[float] = [0.05, 0.15, 0.30, 0.50, 0.75, 0.90, 0.85, 0.65]


def _cosine_sim(a: list[float], b: list[float]) -> float:
    """Cosine similarity between two equal-length lists."""
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(y * y for y in b))
    if na == 0 or nb == 0:
        return 0.0
    return dot / (na * nb)


def _resize_spectrum(spec: list[float], n: int) -> list[float]:
    """Linearly interpolate *spec* to exactly *n* bins."""
    if not spec:
        return [0.0] * n
    if len(spec) == n:
        return list(spec)
    result: list[float] = []
    for i in range(n):
        t_pos = i * (len(spec) - 1) / max(n - 1, 1)
        lo = int(t_pos)
        hi = min(lo + 1, len(spec) - 1)
        frac = t_pos - lo
        result.append(spec[lo] * (1.0 - frac) + spec[hi] * frac)
    return result


def spectral_tsd(values: list[int]) -> tuple[float, float, float]:
    """FFT-based T/S/D profile for a sequence of gematria values.

    Each value in *values* is treated as a frequency sample of the phrase
    signal.  Subtracting the mean removes the DC component so only the
    oscillatory (intervallic) structure is analysed.  A Hanning window
    reduces spectral leakage.  The resulting magnitude spectrum is resized
    to eight bins and compared to T/S/D reference profiles via cosine
    similarity, then normalised to sum to 1.

    Falls back to a pure-Python DFT when numpy is unavailable, and to
    equal thirds when *values* is empty.
    """
    if not values:
        return (1 / 3, 1 / 3, 1 / 3)

    if _HAS_NUMPY:
        sig = np.array(values, dtype=float)
        sig -= sig.mean()
        if len(sig) > 1:
            sig *= np.hanning(len(sig))
        spectrum: list[float] = np.abs(_rfft(sig)).tolist()
    else:
        # Pure-Python fallback DFT
        n = len(values)
        mean = sum(values) / n
        sig_f = [v - mean for v in values]
        spectrum = []
        for k in range(n // 2 + 1):
            re = sum(sig_f[j] * math.cos(2 * math.pi * k * j / n) for j in range(n))
            im = sum(sig_f[j] * math.sin(2 * math.pi * k * j / n) for j in range(n))
            spectrum.append(math.sqrt(re * re + im * im))

    max_val = max(spectrum) if spectrum else 0.0
    spec_norm = [v / max_val for v in spectrum] if max_val > 0 else [0.0] * len(spectrum)
    spec8 = _resize_spectrum(spec_norm, 8)

    t_sim = _cosine_sim(spec8, _T_REF)
    s_sim = _cosine_sim(spec8, _S_REF)
    d_sim = _cosine_sim(spec8, _D_REF)

    total = t_sim + s_sim + d_sim
    if total > 0:
        return (t_sim / total, s_sim / total, d_sim / total)
    return (1 / 3, 1 / 3, 1 / 3)


# ─────────────────────────────────────────────────────────────────────────────
#  PARAMETER DERIVATION
# ─────────────────────────────────────────────────────────────────────────────

def derive_context_params(phrase: str) -> dict[str, float | int]:
    """Derive TonalContext parameters from *phrase* via gematria analysis.

    Two complementary methods are blended at three granularity levels
    (word → syllable → letter), mirroring the developmental order in which
    infants and human history have acquired language:

    **Numerological tree** (digital-root → scale-degree → T/S/D weight)
        captures the fundamental tonal character of every letter without FFT.

    **FFT spectral profile**
        treats the sequence of gematria values as a sampled signal, applies
        a windowed FFT, and compares the magnitude spectrum to eight-bin
        T/S/D reference profiles via cosine similarity.

    Letters carry 50% of the final weight (finest granularity), syllables
    30%, and whole words 20%.  Within each level the two methods are
    averaged equally.

    The blended (t_frac, s_frac, d_frac) triple drives MIDI parameters as
    follows (all values land within the ranges the MIDI engine clamps to):

    +------------------------+------------------------------------------+
    | Parameter              | Derivation                               |
    +========================+==========================================+
    | density  [0, 1]        | T → full chords; D → sparse              |
    | brightness [−1, 1]     | D dominant → +, T dominant → −           |
    | color  [0, 1]          | D+S bring extensions; T keeps triads     |
    | tonic_offset  [0, 6]   | 0 when T dominates (land on tonic);      |
    |                        | (root−1) % 7 otherwise                   |
    | subdominant_offset     | first intermediate path node % 7         |
    |   [0, 6]               | (root % 7 when no intermediate exists)   |
    | dominant_offset  [0,6] | (root × first_intermediate) % 7;         |
    |                        | (root×2+2) % 7 when no intermediate      |
    +------------------------+------------------------------------------+
    """
    phrase_code = gematria_value(phrase)
    path = numerological_path(phrase_code)
    root = path[-1]              # digital root 1–9
    intermediates = path[1:-1]  # interior nodes (empty for short paths)

    words = phrase.split()

    # ── Level 1: whole words ──────────────────────────────────────────────
    word_vals = [gematria_value(w) for w in words if normalize_letters(w)]
    word_dr_tsd = tsd_from_text(phrase)
    word_fft_tsd = spectral_tsd(word_vals)

    # ── Level 2: syllables ────────────────────────────────────────────────
    syl_strs: list[str] = []
    for w in words:
        syl_strs.extend(_simple_syllabify(w))
    syl_vals = [gematria_value(s) for s in syl_strs if gematria_value(s) > 0]
    syl_dr_tsd = tsd_from_text(" ".join(syl_strs))
    syl_fft_tsd = spectral_tsd(syl_vals) if syl_vals else word_fft_tsd

    # ── Level 3: individual letters ───────────────────────────────────────
    letter_vals = [ENGLISH_EXTENDED_VALUES[ch] for ch in normalize_letters(phrase)]
    letter_dr_tsd = tsd_from_text(phrase)   # same letters as phrase
    letter_fft_tsd = spectral_tsd(letter_vals)

    # ── Blend: FFT and digital-root averaged within each level ───────────
    def _avg(
        dr: tuple[float, float, float], fft: tuple[float, float, float]
    ) -> tuple[float, float, float]:
        return ((dr[0] + fft[0]) / 2, (dr[1] + fft[1]) / 2, (dr[2] + fft[2]) / 2)

    lt = _avg(letter_dr_tsd, letter_fft_tsd)
    st = _avg(syl_dr_tsd, syl_fft_tsd)
    wt = _avg(word_dr_tsd, word_fft_tsd)

    # Letters 50%, syllables 30%, words 20%
    t = 0.50 * lt[0] + 0.30 * st[0] + 0.20 * wt[0]
    s = 0.50 * lt[1] + 0.30 * st[1] + 0.20 * wt[1]
    d = 0.50 * lt[2] + 0.30 * st[2] + 0.20 * wt[2]
    total = t + s + d
    if total > 0:
        t_frac, s_frac, d_frac = t / total, s / total, d / total
    else:
        t_frac = s_frac = d_frac = 1 / 3

    # ── density  ──────────────────────────────────────────────────────────
    # High T → full chords (stable, settled); high D → sparser (tense space).
    # Range ≈ [0.35, 0.80]; MIDI engine threshold for colorStack note is 0.35.
    density = min(1.0, max(0.0,
        0.35 + 0.45 * t_frac + 0.25 * s_frac + 0.10 * d_frac
    ))

    # ── brightness  ───────────────────────────────────────────────────────
    # D dominant → positive (pushes MIDI threshold toward dominant function).
    # T dominant → negative (biases toward tonic outputs, honoring the
    #   "land on tonic every once in a while" preference).
    # The engine scales contextBias = brightness × 0.10, so full ±1 is safe.
    brightness = max(-1.0, min(1.0, (d_frac - t_frac) * 0.80))

    # ── color  ────────────────────────────────────────────────────────────
    # >0.45 unlocks extension stack; >0.62 unlocks dominant b5/#5 pivot.
    # D and S bring harmonic color; T keeps clean triads.
    color = min(1.0, max(0.0,
        0.15 + 0.60 * d_frac + 0.40 * s_frac + 0.05 * t_frac
    ))

    # ── offsets (via numerological path interior nodes)  ──────────────────
    # The MIDI engine applies wrap7 so any integer is valid; we keep 0–6.

    # Tonic offset: 0 when T is the dominant function so that the tonic
    # degree family (0/5/2) is rotated only by gematriaCode%7 – staying
    # close to the natural tonic.  Otherwise (root−1)%7 rotates it.
    if t_frac >= s_frac and t_frac >= d_frac:
        tonic_offset = 0
    else:
        tonic_offset = (root - 1) % 7

    # Subdominant offset: first interior path node encodes mid-level character.
    subdominant_offset = (intermediates[0] % 7) if intermediates else (root % 7)

    # Dominant offset: root × first interior node creates a "tension product"
    # that interacts musically with the dominant degree family (4/6).
    if intermediates:
        dominant_offset = (root * intermediates[0]) % 7
    else:
        dominant_offset = (root * 2 + 2) % 7

    return {
        "density": round(density, 4),
        "brightness": round(brightness, 4),
        "color": round(color, 4),
        "tonic_offset": int(tonic_offset),
        "subdominant_offset": int(subdominant_offset),
        "dominant_offset": int(dominant_offset),
    }


def default_contexts() -> list[TonalContext]:
    """Return the six preset tonal contexts with parameters derived from gematria."""
    presets = [
        ("Orphic Black Moon",       1,  False),
        ("Aether Blood Sun",        1,  False),
        ("Nostradamus Dream Sigil", 1,  False),
        ("Rasputin Oracle Tide",    1,  False),
        ("Loki Clockwork Rune",     10, True),
        ("Seraphim Astral Archive", 2,  False),
    ]
    contexts: list[TonalContext] = []
    for name, channel, percussion in presets:
        params = derive_context_params(name)
        contexts.append(
            TonalContext(
                name=name,
                phrase=name,
                gematria_code=gematria_value(name),
                midi_channel=channel,
                percussion_mode=percussion,
                **params,
            )
        )
    return contexts


def clamp_context(context: TonalContext) -> TonalContext:
    context.gematria_code = max(1, int(context.gematria_code))
    context.midi_channel = max(1, min(16, int(context.midi_channel)))
    context.density = max(0.0, min(1.0, float(context.density)))
    context.brightness = max(-1.0, min(1.0, float(context.brightness)))
    context.color = max(0.0, min(1.0, float(context.color)))
    return context


def read_existing_contexts(path: Path) -> list[TonalContext]:
    if not path.exists():
        return []
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        print(f"Warning: invalid JSON in {path}; ignoring existing contexts.")
        return []
    items = payload.get("contexts", payload if isinstance(payload, list) else [])
    contexts: list[TonalContext] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        try:
            contexts.append(clamp_context(TonalContext(**item)))
        except TypeError:
            print(f"Warning: skipping malformed context entry in {path}.")
    return contexts


def write_contexts(path: Path, contexts: list[TonalContext]) -> None:
    payload = {"contexts": [asdict(clamp_context(c)) for c in contexts]}
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def _format_tsd(t: float, s: float, d: float) -> str:
    return f"T={t:.3f}  S={s:.3f}  D={d:.3f}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="English Extended gematria calculator and tonal-context JSON tool."
    )
    sub = parser.add_subparsers(dest="command")

    calc = sub.add_parser("calc", help="Calculate English Extended gematria values.")
    calc.add_argument("items", nargs="*", help="Words or phrases to evaluate.")
    calc.add_argument("--file", help="Optional text file with one phrase per line.")

    init = sub.add_parser("init-contexts", help="Write gematria-derived preset contexts JSON.")
    init.add_argument("--output", default="gematria_tonal_contexts.json", help="Output JSON path.")

    make = sub.add_parser("make-context", help="Create or append one tonal context to JSON.")
    make.add_argument("--name", required=True, help="Context name.")
    make.add_argument("--phrase", help="Phrase used for gematria (defaults to name).")
    make.add_argument("--gematria-code", type=int, help="Override computed gematria code.")
    make.add_argument("--midi-channel", type=int, default=1)
    make.add_argument("--percussion-mode", action="store_true")
    # Tonal parameters default to None → auto-derived from gematria when omitted.
    make.add_argument("--density", type=float, default=None,
                      help="Override gematria-derived density [0,1].")
    make.add_argument("--brightness", type=float, default=None,
                      help="Override gematria-derived brightness [−1,1].")
    make.add_argument("--color", type=float, default=None,
                      help="Override gematria-derived color [0,1].")
    make.add_argument("--tonic-offset", type=int, default=None,
                      help="Override gematria-derived tonic degree offset.")
    make.add_argument("--subdominant-offset", type=int, default=None,
                      help="Override gematria-derived subdominant degree offset.")
    make.add_argument("--dominant-offset", type=int, default=None,
                      help="Override gematria-derived dominant degree offset.")
    make.add_argument("--output", default="gematria_tonal_contexts.json", help="JSON file path.")
    make.add_argument("--append", action="store_true",
                      help="Append to existing file instead of overwrite.")

    analyze = sub.add_parser(
        "analyze-phrase",
        help="Show the three-level spectral T/S/D analysis and derived parameters.",
    )
    analyze.add_argument("phrase", help="Phrase to analyze.")

    return parser


def run_calc(args: argparse.Namespace) -> int:
    items = list(args.items)
    if args.file:
        lines = Path(args.file).read_text(encoding="utf-8").splitlines()
        items.extend(line.strip() for line in lines if line.strip())
    if not items:
        print("No input provided. Pass items or --file.")
        return 1

    results = calculate_many(items)
    width = max(len(r.text) for r in results)
    print(f"{'Text'.ljust(width)} | Normalized | EnglishExtended")
    print(f"{'-' * width}-+------------+----------------")
    for r in results:
        print(f"{r.text.ljust(width)} | {r.normalized:<10} | {r.value}")
    return 0


def run_init_contexts(args: argparse.Namespace) -> int:
    out = Path(args.output)
    write_contexts(out, default_contexts())
    print(f"Wrote {len(default_contexts())} gematria-derived preset contexts to {out}")
    return 0


def run_analyze_phrase(args: argparse.Namespace) -> int:
    phrase: str = args.phrase
    words = phrase.split()

    phrase_code = gematria_value(phrase)
    path = numerological_path(phrase_code)
    root = path[-1]
    intermediates = path[1:-1]

    print(f"\n{'═' * 60}")
    print(f"  Phrase  : {phrase!r}")
    print(f"  Gematria: {phrase_code}  path={path}  root={root}")
    if not _HAS_NUMPY:
        print("  (numpy not available – using pure-Python DFT fallback)")
    print(f"{'═' * 60}")

    # ── Level 1: words ────────────────────────────────────────────────────
    print("\n── Level 1: WORDS ──")
    word_vals = []
    for w in words:
        v = gematria_value(w)
        dr = digital_root(v)
        p = numerological_path(v)
        dr_tsd = tsd_from_text(w)
        fft_tsd = spectral_tsd([v])
        print(f"  {w!r:25s}  gem={v:5d}  path={p}  "
              f"dr-TSD={_format_tsd(*dr_tsd)}  fft-TSD={_format_tsd(*fft_tsd)}")
        if v > 0:
            word_vals.append(v)
    word_fft_tsd = spectral_tsd(word_vals)
    print(f"  {'[word-sequence FFT]':25s}  {'':5s}                  "
          f"fft-TSD={_format_tsd(*word_fft_tsd)}")

    # ── Level 2: syllables ────────────────────────────────────────────────
    print("\n── Level 2: SYLLABLES ──")
    syl_vals = []
    for w in words:
        syls = _simple_syllabify(w)
        for syl in syls:
            v = gematria_value(syl)
            p = numerological_path(v)
            dr_tsd = tsd_from_text(syl)
            fft_tsd = spectral_tsd([v])
            print(f"  {syl!r:25s}  gem={v:5d}  path={p}  "
                  f"dr-TSD={_format_tsd(*dr_tsd)}  fft-TSD={_format_tsd(*fft_tsd)}")
            if v > 0:
                syl_vals.append(v)
    syl_fft_tsd = spectral_tsd(syl_vals) if syl_vals else (1 / 3, 1 / 3, 1 / 3)
    print(f"  {'[syllable-sequence FFT]':25s}  {'':5s}                  "
          f"fft-TSD={_format_tsd(*syl_fft_tsd)}")

    # ── Level 3: letters ──────────────────────────────────────────────────
    print("\n── Level 3: LETTERS ──")
    letter_vals = []
    for ch in normalize_letters(phrase):
        v = ENGLISH_EXTENDED_VALUES[ch]
        dr = digital_root(v)
        wt, ws, wd = _letter_tsd(ch)
        letter_vals.append(v)
        print(f"  {ch}  gem={v:4d}  root={dr}  T/S/D={wt:.2f}/{ws:.2f}/{wd:.2f}")
    letter_fft_tsd = spectral_tsd(letter_vals)
    print(f"\n  [letter-sequence FFT] fft-TSD={_format_tsd(*letter_fft_tsd)}")

    # ── Final blend ───────────────────────────────────────────────────────
    params = derive_context_params(phrase)
    print(f"\n{'─' * 60}")
    print("  Derived context parameters:")
    for k, v in params.items():
        print(f"    {k:<24s} = {v}")
    print(f"{'─' * 60}\n")
    return 0


def run_make_context(args: argparse.Namespace) -> int:
    phrase = args.phrase or args.name
    code = args.gematria_code if args.gematria_code is not None else gematria_value(phrase)

    # Auto-derive tonal parameters from gematria; explicit flags override.
    derived = derive_context_params(phrase)
    context = clamp_context(
        TonalContext(
            name=args.name,
            phrase=phrase,
            gematria_code=code,
            midi_channel=args.midi_channel,
            percussion_mode=args.percussion_mode,
            density=args.density if args.density is not None else derived["density"],
            brightness=args.brightness if args.brightness is not None else derived["brightness"],
            color=args.color if args.color is not None else derived["color"],
            tonic_offset=args.tonic_offset if args.tonic_offset is not None else derived["tonic_offset"],
            subdominant_offset=args.subdominant_offset if args.subdominant_offset is not None else derived["subdominant_offset"],
            dominant_offset=args.dominant_offset if args.dominant_offset is not None else derived["dominant_offset"],
        )
    )

    out = Path(args.output)
    contexts = read_existing_contexts(out) if args.append else []
    contexts.append(context)
    write_contexts(out, contexts)
    print(f"Saved context '{context.name}' (gematria={context.gematria_code}) to {out}")
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "calc":
        return run_calc(args)
    if args.command == "init-contexts":
        return run_init_contexts(args)
    if args.command == "make-context":
        return run_make_context(args)
    if args.command == "analyze-phrase":
        return run_analyze_phrase(args)

    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
