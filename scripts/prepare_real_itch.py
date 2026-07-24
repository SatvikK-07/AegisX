#!/usr/bin/env python3
"""Prepare a provenance-tracked single-symbol segment from real Nasdaq ITCH."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "python"))

from aegisx_research.itch_data import prepare_symbol_segment, write_provenance


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument("--session-date", required=True)
    parser.add_argument("--venue", required=True)
    parser.add_argument("--source-url", required=True)
    parser.add_argument("--official-archive-expected-bytes", type=int, required=True)
    parser.add_argument("--allow-truncated-archive", action="store_true")
    parser.add_argument("--provenance", type=Path)
    args = parser.parse_args()

    result = prepare_symbol_segment(
        args.archive,
        args.output,
        symbol=args.symbol,
        session_date=args.session_date,
        venue=args.venue,
        source_url=args.source_url,
        official_archive_expected_bytes=args.official_archive_expected_bytes,
        allow_truncated_archive=args.allow_truncated_archive,
    )
    provenance = args.provenance or args.output.with_suffix(f"{args.output.suffix}.provenance.json")
    write_provenance(result, provenance)
    print(json.dumps({"segment": str(args.output), "provenance": str(provenance), **result.__dict__}, indent=2))


if __name__ == "__main__":
    main()
