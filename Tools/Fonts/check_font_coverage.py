#!/usr/bin/env python3
"""Validate Unicode coverage in generated LVGL C font sources."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


FONT_GLYPH_RE = re.compile(r"/\*\s*U\+([0-9A-Fa-f]{4,6})\b")
INTEGER_RE = re.compile(r"0[xX][0-9A-Fa-f]+|[0-9]+")
ARRAY_RE = re.compile(
    r"(?:uint8_t|uint16_t|uint32_t)\s+([A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
CMAP_RE = re.compile(
    r"\.range_start\s*=\s*(\d+)\s*,\s*"
    r"\.range_length\s*=\s*(\d+)\s*,.*?"
    r"\.unicode_list\s*=\s*([^,]+)\s*,\s*"
    r"\.glyph_id_ofs_list\s*=\s*([^,]+)\s*,\s*"
    r"\.list_length\s*=\s*(\d+)",
    re.DOTALL,
)
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
DECLARED_COUNT_RE = re.compile(r"^# codepoints: ([0-9]+)$", re.MULTILINE)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_integer_list(body: str) -> list[int]:
    body = COMMENT_RE.sub("", body)
    values = []
    for token in INTEGER_RE.findall(body):
        base = 16 if token.lower().startswith("0x") else 10
        values.append(int(token, base))
    return values


def parse_font_glyphs(path: Path) -> tuple[set[int], set[int], int]:
    text = path.read_text(encoding="utf-8")
    comments = {int(match.group(1), 16) for match in FONT_GLYPH_RE.finditer(text)}
    arrays = {name: parse_integer_list(body) for name, body in ARRAY_RE.findall(text)}
    cmap_start = text.find("static const lv_font_fmt_txt_cmap_t cmaps[]")
    if cmap_start < 0:
        raise ValueError(f"{path}: missing LVGL cmap table")
    cmap_end = text.find("/*--------------------", cmap_start)
    cmap_text = text[cmap_start:] if cmap_end < 0 else text[cmap_start:cmap_end]

    cmap_glyphs: set[int] = set()
    cmap_count = 0
    for match in CMAP_RE.finditer(cmap_text):
        range_start = int(match.group(1), 10)
        range_length = int(match.group(2), 10)
        unicode_list_name = match.group(3).strip()
        glyph_offsets_name = match.group(4).strip()
        list_length = int(match.group(5), 10)
        if unicode_list_name != "NULL":
            values = arrays.get(unicode_list_name)
            if values is None or len(values) != list_length:
                raise ValueError(f"{path}: cmap references invalid {unicode_list_name}")
            cmap_glyphs.update(range_start + value for value in values)
        elif glyph_offsets_name != "NULL":
            values = arrays.get(glyph_offsets_name)
            if values is None or len(values) != list_length:
                raise ValueError(f"{path}: cmap references invalid {glyph_offsets_name}")
            cmap_glyphs.update(range_start + index for index in range(list_length))
        else:
            cmap_glyphs.update(range(range_start, range_start + range_length))
        cmap_count += 1

    if not comments:
        raise ValueError(f"{path}: missing generated glyph descriptors")
    if not cmap_glyphs:
        raise ValueError(f"{path}: empty cmap table")
    if comments != cmap_glyphs:
        missing_comments = sorted(cmap_glyphs - comments)
        missing_cmaps = sorted(comments - cmap_glyphs)
        raise ValueError(
            f"{path}: cmap/comment mismatch "
            f"(without comments: {format_codepoints(missing_comments)}, "
            f"without cmap: {format_codepoints(missing_cmaps)})"
        )
    return cmap_glyphs, comments, cmap_count


def parse_manifest(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    declared_match = DECLARED_COUNT_RE.search(text)
    if declared_match is None:
        raise ValueError(f"{path}: missing '# codepoints: N' declaration")

    glyphs: list[int] = []
    seen: set[int] = set()
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if not re.fullmatch(r"U\+[0-9A-F]{4,6}", line):
            raise ValueError(f"{path}:{line_number}: invalid canonical codepoint: {line}")
        value = int(line[2:], 16)
        if value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
            raise ValueError(f"{path}:{line_number}: invalid Unicode scalar: {line}")
        if value in seen:
            raise ValueError(f"{path}:{line_number}: duplicate codepoint: {line}")
        if glyphs and value <= glyphs[-1]:
            raise ValueError(f"{path}:{line_number}: codepoints are not strictly sorted: {line}")
        glyphs.append(value)
        seen.add(value)

    if not glyphs:
        raise ValueError(f"{path}: empty manifest")
    declared = int(declared_match.group(1), 10)
    if declared != len(glyphs):
        raise ValueError(f"{path}: declares {declared} codepoints but contains {len(glyphs)}")
    return glyphs


def format_codepoint(value: int) -> str:
    return f"U+{value:04X}"


def format_codepoints(values: list[int], limit: int = 12) -> str:
    if not values:
        return "none"
    rendered = ",".join(format_codepoint(value) for value in values[:limit])
    return rendered + (",..." if len(values) > limit else "")


def check_metadata(path: Path, manifest: Path, font: Path) -> None:
    text = path.read_text(encoding="utf-8")
    expected = {
        "Manifest SHA-256": sha256(manifest),
        "Output SHA-256": sha256(font),
    }
    for label, value in expected.items():
        if f"{label}: {value}" not in text:
            raise ValueError(f"{path}: stale or missing {label}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--symbol")
    parser.add_argument("--expect-count", type=int)
    parser.add_argument("--expect-exact", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest_values = parse_manifest(args.manifest)
        manifest = set(manifest_values)
        font_glyphs, comments, cmap_count = parse_font_glyphs(args.font)
        if args.expect_count is not None and len(manifest) != args.expect_count:
            raise ValueError(
                f"{args.manifest}: expected {args.expect_count} codepoints, got {len(manifest)}"
            )
        if args.symbol is not None:
            font_text = args.font.read_text(encoding="utf-8")
            pattern = rf"\blv_font_t\s+{re.escape(args.symbol)}\s*="
            if re.search(pattern, font_text) is None:
                raise ValueError(f"{args.font}: missing public symbol {args.symbol}")
        missing = sorted(manifest - font_glyphs)
        extra = sorted(font_glyphs - manifest)
        if missing:
            raise ValueError(f"{args.font}: missing glyphs: {format_codepoints(missing)}")
        if args.expect_exact and extra:
            raise ValueError(f"{args.font}: unexpected glyphs: {format_codepoints(extra)}")
        if args.metadata is not None:
            check_metadata(args.metadata, args.manifest, args.font)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(
        f"PASS: {args.font.name}: manifest={len(manifest_values)} "
        f"glyphs={len(font_glyphs)} descriptors={len(comments)} cmaps={cmap_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
