#!/usr/bin/env python3
"""Simple gematria calculator for Latin-script words and phrases.

Default system: English Ordinal (A=1, ..., Z=26).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from typing import Iterable


LETTER_VALUES = {chr(ord('A') + i): i + 1 for i in range(26)}


@dataclass(frozen=True)
class GematriaResult:
    text: str
    normalized: str
    value: int


def normalize_letters(text: str) -> str:
    return "".join(ch for ch in text.upper() if ch in LETTER_VALUES)


def gematria_value(text: str) -> int:
    return sum(LETTER_VALUES[ch] for ch in normalize_letters(text))


def calculate_many(items: Iterable[str]) -> list[GematriaResult]:
    results: list[GematriaResult] = []
    for item in items:
        normalized = normalize_letters(item)
        results.append(GematriaResult(item, normalized, gematria_value(item)))
    return results


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compute English-ordinal gematria values.")
    parser.add_argument("items", nargs="*", help="Words or phrases to evaluate.")
    parser.add_argument(
        "--file",
        help="Optional text file with one word/phrase per line.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    items: list[str] = list(args.items)

    if args.file:
        with open(args.file, "r", encoding="utf-8") as f:
            items.extend(line.strip() for line in f if line.strip())

    if not items:
        print("No input provided. Pass items or --file.")
        return 1

    rows = calculate_many(items)
    width = max(len(r.text) for r in rows)
    print(f"{'Text'.ljust(width)} | Normalized | Gematria")
    print(f"{'-' * width}-+------------+---------")
    for r in rows:
        print(f"{r.text.ljust(width)} | {r.normalized:<10} | {r.value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
