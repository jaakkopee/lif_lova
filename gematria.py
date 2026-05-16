#!/usr/bin/env python3
"""Gematria utility for English Extended cipher and tonal context JSON generation."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

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
    rows: list[GematriaResult] = []
    for item in items:
        normalized = normalize_letters(item)
        rows.append(GematriaResult(item, normalized, gematria_value(item)))
    return rows


def default_contexts() -> list[TonalContext]:
    presets = [
        ("Orphic Black Moon", 1, False, 0.58, 0.08, 0.52, 0, 1, 2),
        ("Aether Blood Sun", 1, False, 0.50, 0.14, 0.44, 1, 3, 4),
        ("Nostradamus Dream Sigil", 1, False, 0.76, 0.03, 0.82, 2, 4, 6),
        ("Rasputin Oracle Tide", 1, False, 0.64, -0.08, 0.68, 3, 2, 5),
        ("Loki Clockwork Rune", 10, True, 0.55, 0.22, 0.24, 0, 0, 0),
        ("Seraphim Astral Archive", 2, False, 0.70, 0.18, 0.90, 4, 5, 1),
    ]
    contexts: list[TonalContext] = []
    for name, channel, percussion, density, brightness, color, tonic, sub, dom in presets:
        contexts.append(
            TonalContext(
                name=name,
                phrase=name,
                gematria_code=gematria_value(name),
                midi_channel=channel,
                percussion_mode=percussion,
                density=density,
                brightness=brightness,
                color=color,
                tonic_offset=tonic,
                subdominant_offset=sub,
                dominant_offset=dom,
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
    payload = json.loads(path.read_text(encoding="utf-8"))
    items = payload.get("contexts", payload if isinstance(payload, list) else [])
    contexts: list[TonalContext] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        contexts.append(clamp_context(TonalContext(**item)))
    return contexts


def write_contexts(path: Path, contexts: list[TonalContext]) -> None:
    payload = {"contexts": [asdict(clamp_context(c)) for c in contexts]}
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="English Extended gematria calculator and tonal-context JSON tool."
    )
    sub = parser.add_subparsers(dest="command")

    calc = sub.add_parser("calc", help="Calculate English Extended gematria values.")
    calc.add_argument("items", nargs="*", help="Words or phrases to evaluate.")
    calc.add_argument("--file", help="Optional text file with one phrase per line.")

    init = sub.add_parser("init-contexts", help="Write mystical preset contexts JSON.")
    init.add_argument("--output", default="gematria_tonal_contexts.json", help="Output JSON path.")

    make = sub.add_parser("make-context", help="Create or append one tonal context to JSON.")
    make.add_argument("--name", required=True, help="Context name.")
    make.add_argument("--phrase", help="Phrase used for gematria (defaults to name).")
    make.add_argument("--gematria-code", type=int, help="Override computed gematria code.")
    make.add_argument("--midi-channel", type=int, default=1)
    make.add_argument("--percussion-mode", action="store_true")
    make.add_argument("--density", type=float, default=0.65)
    make.add_argument("--brightness", type=float, default=0.0)
    make.add_argument("--color", type=float, default=0.55)
    make.add_argument("--tonic-offset", type=int, default=0)
    make.add_argument("--subdominant-offset", type=int, default=0)
    make.add_argument("--dominant-offset", type=int, default=0)
    make.add_argument("--output", default="gematria_tonal_contexts.json", help="JSON file path.")
    make.add_argument("--append", action="store_true", help="Append to existing file instead of overwrite.")

    return parser


def run_calc(args: argparse.Namespace) -> int:
    items = list(args.items)
    if args.file:
        lines = Path(args.file).read_text(encoding="utf-8").splitlines()
        items.extend(line.strip() for line in lines if line.strip())
    if not items:
        print("No input provided. Pass items or --file.")
        return 1

    rows = calculate_many(items)
    width = max(len(r.text) for r in rows)
    print(f"{'Text'.ljust(width)} | Normalized | EnglishExtended")
    print(f"{'-' * width}-+------------+----------------")
    for r in rows:
        print(f"{r.text.ljust(width)} | {r.normalized:<10} | {r.value}")
    return 0


def run_init_contexts(args: argparse.Namespace) -> int:
    out = Path(args.output)
    write_contexts(out, default_contexts())
    print(f"Wrote {len(default_contexts())} preset contexts to {out}")
    return 0


def run_make_context(args: argparse.Namespace) -> int:
    phrase = args.phrase or args.name
    code = args.gematria_code if args.gematria_code is not None else gematria_value(phrase)
    context = clamp_context(
        TonalContext(
            name=args.name,
            phrase=phrase,
            gematria_code=code,
            midi_channel=args.midi_channel,
            percussion_mode=args.percussion_mode,
            density=args.density,
            brightness=args.brightness,
            color=args.color,
            tonic_offset=args.tonic_offset,
            subdominant_offset=args.subdominant_offset,
            dominant_offset=args.dominant_offset,
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

    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
