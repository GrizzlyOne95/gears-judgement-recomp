"""Post-codegen patch: make the guest's thumbstick low-pass filter configurable.

Why: the guest does not use the deflection it is handed - sub_8299C3D0 (the
Xbox 360 input-state reader) runs every stick axis through a first-order filter

    out = prev + alpha * (new - prev)

with alpha loaded from the input pool at 0x82082928 (measured 0.5, i.e. the view
reaches only ~63% of a step after two guest polls, ~87% after four). At the
~125 polls/s measured here that is a 15-30 ms lag plus a smeared tail, which is
exactly what "the mouse feels like it's dragging through treacle, make it RAW"
is about. The filter exists because a thumbstick a hand rests on drifts; a mouse
does not, so for mouse-driven look it is pure loss.

The patch reroutes that one load through GowjInputSmoothAlpha(), which returns
1.0 (no filtering, deflection reaches the guest untouched) unless the user asks
for the console behaviour back with mnk_guest_smoothing = true.

Scoped to sub_8299C3D0 on purpose: the same three-instruction sequence appears in
two unrelated functions (sub_82752EE0, sub_825CC728) that read a different
object's +56 float, and patching those would change behaviour nobody asked for.

    python tools/patch_mouse_smoothing.py generated/default/*.cpp

Exit 0 and print per-file counts. Idempotent: re-running finds the hook already
in place and reports it as preexisting.
"""
from __future__ import annotations

import glob
import os
import re
import sys

DECL_MARK = "GOWJ RAW MOUSE"
FUNC = "sub_8299C3D0"
HOOK = "GowjInputSmoothAlpha"
DECL = f'extern "C" float {HOOK}(float guest_alpha);  // {DECL_MARK}\n'

ANCHOR = (
    "\t// lfs f0,56(r20)\n"
    "\ttemp.u32 = REX_LOAD_U32(ctx.r20.u32 + 56);\n"
    "\tctx.f0.f64 = double(temp.f32);\n"
)
REPLACEMENT = (
    "\t// lfs f0,56(r20)\n"
    "\ttemp.u32 = REX_LOAD_U32(ctx.r20.u32 + 56);\n"
    f"\tctx.f0.f64 = double({HOOK}(temp.f32));\n"
)


def func_body_span(text):
    """(start, end) offsets of FUNC's body, or None."""
    m = re.search(rf"^DEFINE_REX_FUNC\({FUNC}\) \{{", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^DEFINE_REX_FUNC\(", text[m.end():], re.M)
    return m.start(), m.end() + (nxt.start() if nxt else len(text) - m.end())


def patch_file(path):
    """Return (applied, preexisting) substitution counts for one TU."""
    with open(path, encoding="utf-8") as f:
        src = f.read()
    span = func_body_span(src)
    if not span:
        return 0, (1 if f"{HOOK}(temp.f32)" in src else 0)
    lo, hi = span
    body = src[lo:hi]
    n = body.count(REPLACEMENT)
    if n:
        if DECL not in src:
            src = insert_decl(src)
            write(path, src)
        return 0, n
    n = body.count(ANCHOR)
    if n > 1:
        raise SystemExit(f"{path}: {n} smoothing sites inside {FUNC}, expected 1")
    if not n:
        return 0, 0
    out = src[:lo] + body.replace(ANCHOR, REPLACEMENT, 1) + src[hi:]
    out = insert_decl(out)
    write(path, out)
    return 1, 0


def insert_decl(src):
    """Put the extern "C" declaration after the file's first include/define block."""
    if DECL in src:
        return src
    m = re.search(r'^(#include [^\n]*|// [^\n]*)$', src[:8000], re.M)
    cut = src.find("\n", m.end()) + 1 if m else src.index("\n") + 1
    return (src[:cut] + "\n"
            + f"// {DECL_MARK}: the guest's own stick filter is a host cvar now\n"
            + DECL + src[cut:])


def write(path, text):
    tmp = path + ".smooth.tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def main(argv):
    # CMake hands us the glob literally through cmd.exe; nothing expands it here.
    files = []
    for a in argv[1:]:
        if not a.endswith(".cpp"):
            continue
        if any(ch in a for ch in "*?["):
            files.extend(sorted(glob.glob(a)))
        else:
            files.append(a)
    if not files:
        files = glob_default()
    applied = pre = 0
    touched = []
    for path in files:
        a, p = patch_file(path)
        applied += a
        pre += p
        if a or p:
            touched.append(path)
    if applied + pre != 1:
        raise SystemExit(f"mouse-smoothing patch: {applied} applied + {pre} preexisting "
                         f"in {len(files)} files, expected exactly 1 total")
    print(f"patch_mouse_smoothing: {FUNC} alpha hooked in {touched[0]} "
          f"({'new' if applied else 'already patched'})")
    return 0


def glob_default():
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                     "gowj", "generated", "default")
    return [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".cpp")]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
