#!/usr/bin/env python3
"""Generate the deterministic 18 px zh-CN UI font subset."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path

from check_font_coverage import parse_manifest


EXPECTED_INPUT_SHA256 = "84bbd4ace91d327b3ad1a581c688196278a4e41308520176f419180064e4af2b"
FONT_SYMBOL = "lv_font_zh_cn_ui_18"
FONT_FILENAME = "SourceHanSansSC-Regular.otf"
OUTPUT_FILENAME = "lv_font_zh_cn_ui_18.c"
GENERATOR_VERSION = "lv_font_conv 1.5.3"
FONT_VERSION = "2.004"
FONT_SOURCE_URL = (
    "https://github.com/adobe-fonts/source-han-sans/releases/download/2.004R/"
    "SourceHanSansSC.zip"
)


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--font-conv", nargs=argparse.REMAINDER, default=["lv_font_conv"])
    return parser.parse_args()


def render_metadata(manifest: Path, generated: bytes) -> str:
    return "\n".join(
        [
            "Font asset: Simplified Chinese firmware UI subset, 18 px, 4 bpp",
            "Original font: Source Han Sans SC Regular",
            f"Original font version: {FONT_VERSION}",
            f"Source archive: {FONT_SOURCE_URL}",
            "Copyright: Copyright 2014-2021 Adobe (http://www.adobe.com/)",
            "License: SIL Open Font License 1.1 (SPDX: OFL-1.1)",
            "License file: Library/LvglLib/Fonts/OFL-SourceHanSans.txt",
            "Reserved Font Name: Source",
            f"Input SHA-256: {EXPECTED_INPUT_SHA256}",
            f"Manifest: Library/LvglLib/Fonts/{manifest.name}",
            f"Manifest SHA-256: {sha256(manifest)}",
            f"Generator: {GENERATOR_VERSION}",
            f"Output: {OUTPUT_FILENAME}",
            f"Output SHA-256: {sha256_bytes(generated)}",
            f"Public symbol: {FONT_SYMBOL}",
            "Generation policy: ASCII U+0020-U+007E plus every remaining manifest codepoint",
            "",
        ]
    )


def normalize_generated(content: str) -> bytes:
    provenance = (
        "/* SPDX-License-Identifier: OFL-1.1\n"
        " * Generated from Source Han Sans SC Regular.\n"
        f" * Input SHA-256: {EXPECTED_INPUT_SHA256}\n"
        " * The public subset name does not use the Reserved Font Name.\n"
        " */\n"
    )
    return (provenance + content.lstrip()).rstrip().encode("utf-8") + b"\n"


def compare_or_write(path: Path, expected: bytes, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_bytes() != expected:
            raise SystemExit(f"error: generated output differs: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(expected)


def main() -> int:
    args = parse_args()
    if not args.font.is_file():
        raise SystemExit(f"error: missing OpenType font: {args.font}")
    actual_font_hash = sha256(args.font)
    if actual_font_hash != EXPECTED_INPUT_SHA256:
        raise SystemExit(
            f"error: unexpected input font SHA-256: {actual_font_hash} "
            f"(expected {EXPECTED_INPUT_SHA256})"
        )
    if not args.font_conv:
        raise SystemExit("error: --font-conv command is empty")

    codepoints = parse_manifest(args.manifest)
    ascii_codepoints = set(range(0x20, 0x7F))
    manifest_ascii = {value for value in codepoints if value <= 0x7E}
    if manifest_ascii != ascii_codepoints:
        raise SystemExit("error: manifest must contain exactly ASCII U+0020-U+007E")
    symbols = "".join(chr(codepoint) for codepoint in codepoints if codepoint > 0x7E)

    with tempfile.TemporaryDirectory(prefix="lv-font-zh-cn-ui-18-") as temp_name:
        temp_dir = Path(temp_name)
        temp_font = temp_dir / FONT_FILENAME
        temp_output = temp_dir / OUTPUT_FILENAME
        shutil.copyfile(args.font, temp_font)
        command = [
            *args.font_conv,
            "--bpp", "4",
            "--size", "18",
            "--no-compress",
            "--no-prefilter",
            "--font", FONT_FILENAME,
            "-r", "0x20-0x7E",
            "--symbols", symbols,
            "--format", "lvgl",
            "--lv-font-name", FONT_SYMBOL,
            "--lv-include", "lvgl.h",
            "--force-fast-kern-format",
            "-o", OUTPUT_FILENAME,
        ]
        print(f"running {GENERATOR_VERSION} for {len(codepoints)} manifest codepoints")
        subprocess.run(command, cwd=temp_dir, check=True)
        generated = normalize_generated(temp_output.read_text(encoding="utf-8"))

    metadata = render_metadata(args.manifest, generated).encode("utf-8")
    compare_or_write(args.output, generated, args.check)
    compare_or_write(args.metadata, metadata, args.check)
    print("PASS: generated font and metadata are deterministic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
