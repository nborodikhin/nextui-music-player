#!/usr/bin/env python3

import re
import sys
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SOURCE_DIR = SCRIPT_DIR.parent / "src"
NUMBER = r"(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*"
SUPPRESSION = re.compile(r"^[\t ]*//noinspection HardcodedColor[\t ]*$", re.MULTILINE)

PATTERNS = (
    ("fixed COLOR name", re.compile(r"\bCOLOR_(?:WHITE|GRAY|BLACK|LIGHT_TEXT|DARK_TEXT|BUTTON_TEXT)\b")),
    ("fixed RGB name", re.compile(r"\bRGB_(?:WHITE|BLACK|GRAY|LIGHT_GRAY|DARK_GRAY)\b")),
    ("fixed TRIAD name", re.compile(r"\bTRIAD_(?:WHITE|BLACK|GRAY|LIGHT_GRAY|DARK_GRAY|LIGHT_TEXT|DARK_TEXT)\b")),
    ("numeric CFG_getColor argument", re.compile(rf"\bCFG_getColor\s*\(\s*{NUMBER}\s*\)")),
    ("numeric fill color", re.compile(r"\bSDL_FillRect\s*\([^;]*,\s*0[xX][0-9a-fA-F]{2,8}[uUlL]*\s*\)")),
    ("numeric fill color", re.compile(r"\bGFX_blitPillColor\s*\([^;]*,\s*0[xX][0-9a-fA-F]{2,8}[uUlL]*\s*,")),
)
MAP_CALL = re.compile(r"\bSDL_MapRGB(A)?\s*\([^;]*\)", re.DOTALL)
TRANSPARENT_MAP = re.compile(
    r"\bSDL_MapRGBA\s*\([^;]*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*,\s*0[uUlL]*\s*\)\s*$",
    re.DOTALL,
)
NUMERIC_RGBA_CHANNEL = re.compile(rf",\s*{NUMBER}\b\s*,", re.DOTALL)
NUMERIC_RGB_CHANNEL = re.compile(rf",\s*{NUMBER}\b\s*(?:,|\))", re.DOTALL)
SDL_COLOR_DECLARATION = re.compile(
    r"\bSDL_Color\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\])?\s*=\s*\{[^;]*;", re.DOTALL
)
NUMERIC_COLOR_INITIALIZER = re.compile(
    rf"=\s*\{{\s*\{{*\s*(?:{NUMBER}\b|\.[rgba]\s*=\s*{NUMBER}\b)", re.DOTALL
)
SDL_COLOR_LITERAL = re.compile(r"\(\s*SDL_Color\s*\)\s*\{[^;}]*\}", re.DOTALL)
NUMERIC_COLOR_LITERAL = re.compile(
    rf"\{{\s*(?:{NUMBER}\b|\.[rgba]\s*=\s*{NUMBER}\b)", re.DOTALL
)


# Multiline strings serve as here-documents. check_fixtures() writes each one to
# a temporary file so the self-test exercises the same file path as real scans.
PASS_FIXTURES = {
    "marked_multiline.c": """//noinspection HardcodedColor
uint32_t color = SDL_MapRGB(
    format,
    20, 30, 40);
""",
    "theme_color.c": """SDL_MapRGBA(format, color.r, color.g, color.b, color.a);
CFG_getColor(COLOR_BACKGROUND);
""",
    "theme_mask.c": """SDL_MapRGB(format,
           (color >> 24) & 0xff,
           (color >> 16) & 0xff,
           (color >> 8) & 0xff);
""",
    "transparent_clear.c": """SDL_FillRect(combined, NULL, 0);
""",
    "transparent_fill.c": """SDL_FillRect(surface, NULL, 0);
""",
    "transparent_map.c": """SDL_MapRGBA(format, 0, 0, 0, 0);
""",
}

FAIL_FIXTURES = {
    "after_marked_statement.c": """//noinspection HardcodedColor
draw(COLOR_WHITE); draw(COLOR_BLACK);
""",
    "call_format.c": """SDL_MapRGB(SDL_GetWindowSurface(window)->format, 255, 0, 0);
""",
    "dangling_mark.c": """//noinspection HardcodedColor
// The old statement is not present.
SDL_FillRect(surface, NULL, RGB_WHITE);
""",
    "fixed_color.c": """draw(COLOR_WHITE);
""",
    "fixed_rgb.c": """draw(RGB_BLACK);
""",
    "later_numeric_channel.c": """SDL_MapRGB(format, red, 255, 255);
""",
    "names_in_text.c": """// COLOR_WHITE is not removed before the check.
log_message("RGB_BLACK");
""",
    "numeric_cfg_color.c": """CFG_getColor(4);
""",
    "numeric_map.c": """SDL_MapRGBA(format, 10, 20, 30, alpha);
""",
    "numeric_sdl_color_array.c": """SDL_Color palette[2] = {{255, 0, 0}, {0, 255, 0}};
""",
    "numeric_sdl_color_declaration.c": """SDL_Color color = {10, 20, 30, 255};
""",
    "numeric_sdl_color_green.c": """SDL_Color color = {.g = 255, .r = 200};
""",
    "numeric_sdl_color_green_literal.c": """draw((SDL_Color){.g = 255});
""",
    "numeric_sdl_color_literal.c": """draw((SDL_Color){10, 20, 30, 255});
""",
    "numeric_suffix.c": """SDL_MapRGB(format, 255u, green, blue);
""",
    "opaque_black_map.c": """SDL_MapRGB(format, 0, 0, 0);
""",
    "packed_fill.c": """SDL_FillRect(screen, &rect, 0xffffffff);
""",
    "packed_pill.c": """GFX_blitPillColor(ASSET_WHITE_PILL, screen, &rect, 0xff00ffff, RGB_WHITE);
""",
    "semicolon_in_string.c": """//noinspection HardcodedColor
log_message("stop; continue");
draw(COLOR_WHITE);
""",
    "triad_name.c": """SDL_MapRGB(format, TRIAD_WHITE);
""",
    "unused_mark.c": """//noinspection HardcodedColor
draw(theme_color);
""",
}


def line_number(content, offset):
    return content.count("\n", 0, offset) + 1


def collect_findings(content):
    findings = []
    for label, pattern in PATTERNS:
        for match in pattern.finditer(content):
            findings.append((match.start(), line_number(content, match.start()), label))

    for match in MAP_CALL.finditer(content):
        call = match.group(0)
        if match.group(1):
            if TRANSPARENT_MAP.search(call):
                continue
            has_number = NUMERIC_RGBA_CHANNEL.search(call)
        else:
            has_number = NUMERIC_RGB_CHANNEL.search(call)
        if has_number:
            findings.append((match.start(), line_number(content, match.start()), "numeric SDL map channels"))

    for match in SDL_COLOR_DECLARATION.finditer(content):
        if NUMERIC_COLOR_INITIALIZER.search(match.group(0)):
            findings.append((match.start(), line_number(content, match.start()), "numeric SDL_Color declaration"))

    for match in SDL_COLOR_LITERAL.finditer(content):
        if NUMERIC_COLOR_LITERAL.search(match.group(0)):
            findings.append((match.start(), line_number(content, match.start()), "numeric SDL_Color compound literal"))
    return findings


def filter_suppressed_regions(source):
    lines = source.splitlines(keepends=True)
    marked = False
    filtered = []
    for line in lines:
        body = line.removesuffix("\n").removesuffix("\r")
        if SUPPRESSION.fullmatch(body):
            filtered.append("\n" if line.endswith("\n") else "")
            marked = True
        elif marked:
            semicolon = line.find(";")
            if semicolon < 0:
                filtered.append("\n" if line.endswith("\n") else "")
            else:
                filtered.append(" " * (semicolon + 1) + line[semicolon + 1 :])
                marked = False
        else:
            filtered.append(line)
    return "".join(filtered)


def find_unused_suppressions(source, source_findings):
    findings = []
    for mark in SUPPRESSION.finditer(source):
        region_start = source.find("\n", mark.start())
        region_start = len(source) if region_start < 0 else region_start + 1
        region_end = source.find(";", region_start)
        region_end = len(source) if region_end < 0 else region_end
        first_line_end = source.find("\n", region_start)
        first_line_end = len(source) if first_line_end < 0 else first_line_end
        first_line = source[region_start:first_line_end]

        used = not re.match(r"^\s*(?://|$)", first_line)
        if used:
            used = any(region_start <= offset <= region_end for offset, _, _ in source_findings)
        if not used:
            findings.append((mark.start(), line_number(source, mark.start()), "lint suppression mark has no fixed color"))
    return findings


def scan_file(path, display=None, report=True):
    display = str(path) if display is None else display
    try:
        source = Path(path).read_text()
    except (OSError, UnicodeError) as error:
        if report:
            print(f"{display}:1: source scan failed: {error}", file=sys.stderr)
        return False

    source_findings = collect_findings(source)
    findings = collect_findings(filter_suppressed_regions(source))
    findings.extend(find_unused_suppressions(source, source_findings))
    findings.sort(key=lambda finding: (finding[1], finding[2]))
    if report:
        for _, line, label in findings:
            print(f"{display}:{line}: {label}", file=sys.stderr)
    return not findings


def check_fixtures():
    failed = False
    with tempfile.TemporaryDirectory(prefix="color-call-fixtures-") as directory:
        fixture_dir = Path(directory)
        for expected, fixtures in ((True, PASS_FIXTURES), (False, FAIL_FIXTURES)):
            for name, source in fixtures.items():
                path = fixture_dir / name
                path.write_text(source)
                if scan_file(path, display=name, report=False) is not expected:
                    expectation = "no error" if expected else "an error"
                    print(f"{name}: expected {expectation}", file=sys.stderr)
                    failed = True

        missing = fixture_dir / "missing-fixture.c"
        if scan_file(missing, display=missing.name, report=False):
            print(f"{missing.name}: expected a read error", file=sys.stderr)
            failed = True
    return not failed


def scan_paths(paths):
    succeeded = True
    for path in paths:
        if not scan_file(path):
            succeeded = False
    return succeeded


def main(argv):
    if argv[:1] == ["--scan"]:
        return 0 if scan_paths(argv[1:]) else 1
    if not check_fixtures():
        return 1

    paths = sorted(SOURCE_DIR.glob("*.c")) + sorted(SOURCE_DIR.glob("*.h"))
    if not scan_paths(paths):
        print("Fixed colors need a lint suppression mark.", file=sys.stderr)
        return 1
    print("Color call check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
