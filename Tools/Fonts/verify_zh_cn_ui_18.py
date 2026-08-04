#!/usr/bin/env python3
"""Run the repository-local verification for lv_font_zh_cn_ui_18."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = REPO_ROOT / "Library" / "LvglLib" / "Fonts"
EXPECTED_INPUT_SHA256 = "84bbd4ace91d327b3ad1a581c688196278a4e41308520176f419180064e4af2b"
EXPECTED_SOURCE_URL = (
    "https://github.com/adobe-fonts/source-han-sans/releases/download/2.004R/"
    "SourceHanSansSC.zip"
)


def main() -> int:
    license_path = FONT_DIR / "OFL-SourceHanSans.txt"
    license_text = license_path.read_text(encoding="utf-8")
    required_license_text = (
        "Copyright 2014-2021 Adobe",
        "Reserved Font Name 'Source'",
        "SIL OPEN FONT LICENSE Version 1.1",
    )
    for required in required_license_text:
        if required not in license_text:
            raise SystemExit(f"error: {license_path}: missing license text: {required}")

    metadata_path = FONT_DIR / "lv_font_zh_cn_ui_18.txt"
    metadata = metadata_path.read_text(encoding="utf-8")
    required_metadata = (
        "Original font version: 2.004",
        f"Source archive: {EXPECTED_SOURCE_URL}",
        f"Input SHA-256: {EXPECTED_INPUT_SHA256}",
    )
    for required in required_metadata:
        if required not in metadata:
            raise SystemExit(f"error: {metadata_path}: missing metadata: {required}")

    command = [
        sys.executable,
        str(Path(__file__).with_name("check_font_coverage.py")),
        "--manifest", str(FONT_DIR / "zh_cn_ui_18_glyphs.txt"),
        "--font", str(FONT_DIR / "lv_font_zh_cn_ui_18.c"),
        "--metadata", str(metadata_path),
        "--symbol", "lv_font_zh_cn_ui_18",
        "--expect-count", "511",
        "--expect-exact",
    ]
    subprocess.run(command, cwd=REPO_ROOT, check=True)
    print("PASS: zh-CN UI font license and provenance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
