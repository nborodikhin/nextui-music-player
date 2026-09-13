#!/usr/bin/env python3

# One function presents each frame of the app, and two functions write the GPU
# layers. This scan rejects a source that presents or writes a layer on its own,
# and a source that gives a layer a name of its own.

import re
import sys
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SOURCE_DIR = SCRIPT_DIR.parent / "src"

# The one function that presents, and the two that write a layer. A call
# outside these functions is a finding, whatever file holds it.
PRESENTER = ("module_common.c", {"ModuleCommon_frameEnd"})
LAYER_OWNER = ("ui_layers.c", {"UiLayer_clear", "UiLayer_blit"})

# A call that gives the display a frame, or a platform helper that presents
# inside its draw. Only the presenter of the frame may call one.
PRESENT_CALL = re.compile(
    r"\b(?:GFX_flip|GFX_flip_fixed_rate|GFX_flipHidden|GFX_sync|GFX_GL_Swap"
    r"|PLAT_flip|PLAT_flipHidden|PLAT_GPU_Flip|PLAT_GL_Swap"
    r"|GFX_scrollTextTexture|PLAT_scrollTextTexture|GFX_resetScrollText|PLAT_resetScrollText"
    r"|GFX_animateSurface|PLAT_animateSurface|GFX_animateSurfaceOpacity|PLAT_animateSurfaceOpacity"
    r"|GFX_animateAndFadeSurface|PLAT_animateAndFadeSurface)\s*\("
)

# A write of a GPU layer. Only the owner of the layers may call one, thus each
# write records the change for the presenter.
LAYER_CALL = re.compile(r"\b(?:GFX_clearLayers|PLAT_clearLayers|GFX_drawOnLayer|PLAT_drawOnLayer)\s*\(")

# A name of a layer that is not one of the app. The names of the platform
# describe what the platform draws, and a name of a screen describes one
# element. The app names a layer by its purpose, with the UI_LAYER_ prefix.
LAYER_NAME = re.compile(r"(?<![A-Za-z0-9_])LAYER_[A-Z0-9_]+\b")

# One pass over the source: a string, a character constant, a line comment or
# a block comment, whichever starts first. Thus a comment delimiter inside a
# string does not open a comment, and a quote inside a comment does not open a
# string.
LEXEME = re.compile(
    r'"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r"|//[^\n]*"
    r"|/\*.*?\*/",
    re.DOTALL,
)

# The start of a function definition at the left margin: a return type, a
# name, a parameter list, and the opening brace on the same line.
FUNCTION_START = re.compile(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{", re.MULTILINE)


PASS_FIXTURES = {
    "module_common.c": """void ModuleCommon_frameEnd(SDL_Surface* screen) {
    switch (change) {
        case FRAME_CHANGED_SURFACE: GFX_flip(screen);  break;
        case FRAME_CHANGED_LAYERS:  PLAT_GPU_Flip();   break;
    }
    GFX_sync();
}
""",
    "ui_layers.c": """void UiLayer_clear(UiLayer layer) {
    PLAT_clearLayers(layer);
}

void UiLayer_blit(SDL_Surface* surface, int x, int y, UiLayer layer) {
    PLAT_drawOnLayer(surface, x, y, surface->w, surface->h, 1.0f, false, layer);
}
""",
    "painter.c": """static void paint(void) {
    UiLayer_clear(UI_LAYER_ANIMATION);
    UiLayer_blit(surface, x, y, UI_LAYER_STATUS);
}
""",
    "comment_and_string.c": """// GFX_flip() is the call of the platform, and LAYER_SCROLLTEXT its name
log("PLAT_GPU_Flip LAYER_BUFFER");
/* a block comment with a "quote and GFX_sync() */
""",
    "comment_delimiter_in_string.c": """log("http://example"); draw();
""",
    "own_prefix.c": """int a = UI_LAYER_TOAST;
""",
}

FAIL_FIXTURES = {
    "flip.c": """GFX_flip(screen);
""",
    "sync.c": """GFX_sync();
""",
    "gpu_flip.c": """PLAT_GPU_Flip();
""",
    "scroll_text.c": """GFX_scrollTextTexture(font, text, x, y, w, h, color, 1.0f, NULL);
""",
    "clear_layer.c": """PLAT_clearLayers(4);
""",
    "draw_on_layer.c": """PLAT_drawOnLayer(surface, x, y, w, h, 1.0f, false, 3);
""",
    "platform_name.c": """PLAT_clearLayers(LAYER_SCROLLTEXT);
""",
    "screen_name.c": """#define LAYER_PLAYTIME 3
""",
    "module_common_layer.c": """void f(void) { PLAT_clearLayers(3); }
""",
    "comment_delimiter_hides_a_call.c": """log("http://example"); GFX_flip(screen);
""",
    "module_common.c": """static void helper(void) {
    GFX_flip(screen);
}

void ModuleCommon_frameEnd(SDL_Surface* screen) {
    helper();
}
""",
    "ui_layers.c": """static void helper(int layer) {
    PLAT_clearLayers(layer);
}

void UiLayer_clear(UiLayer layer) {
    helper(layer);
}
""",
}


def line_number(content, offset):
    return content.count("\n", 0, offset) + 1


def strip_comments_and_strings(source):
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))

    return LEXEME.sub(blank, source)


def function_at(code, offset):
    """The name of the function whose definition starts last before `offset`."""
    name = None
    for match in FUNCTION_START.finditer(code):
        if match.start() > offset:
            break
        name = match.group(1)
    return name


def call_is_allowed(name, code, offset, owner):
    file_name, functions = owner
    return name == file_name and function_at(code, offset) in functions


def collect_findings(name, source):
    code = strip_comments_and_strings(source)
    findings = []
    for match in PRESENT_CALL.finditer(code):
        if call_is_allowed(name, code, match.start(), PRESENTER):
            continue
        findings.append((line_number(code, match.start()), "a present outside ModuleCommon_frameEnd()"))
    for match in LAYER_CALL.finditer(code):
        if call_is_allowed(name, code, match.start(), LAYER_OWNER):
            continue
        findings.append((line_number(code, match.start()), "a layer write outside UiLayer_clear() and UiLayer_blit()"))
    for match in LAYER_NAME.finditer(code):
        findings.append((line_number(code, match.start()), f"a layer name that is not of the app: {match.group(0)}"))
    return findings


def scan_file(path, display=None, report=True):
    path = Path(path)
    display = str(path) if display is None else display
    try:
        source = path.read_text()
    except (OSError, UnicodeError) as error:
        if report:
            print(f"{display}:1: source scan failed: {error}", file=sys.stderr)
        return False

    findings = sorted(collect_findings(path.name, source))
    if report:
        for line, label in findings:
            print(f"{display}:{line}: {label}", file=sys.stderr)
    return not findings


def check_fixtures():
    failed = False
    with tempfile.TemporaryDirectory(prefix="present-call-fixtures-") as directory:
        fixture_dir = Path(directory)
        # A pass fixture and a fail fixture can share a name, because the owner
        # files are exempt by name and function: each set has a directory.
        for expected, fixtures in ((True, PASS_FIXTURES), (False, FAIL_FIXTURES)):
            set_dir = fixture_dir / ("pass" if expected else "fail")
            set_dir.mkdir()
            for name, source in fixtures.items():
                path = set_dir / name
                path.write_text(source)
                if scan_file(path, display=name, report=False) is not expected:
                    expectation = "no error" if expected else "an error"
                    print(f"{name}: expected {expectation}", file=sys.stderr)
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
        print("Only ModuleCommon_frameEnd() presents, and only ui_layers.c writes a layer.", file=sys.stderr)
        return 1
    print("Present call check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
