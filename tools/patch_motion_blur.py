"""Post-codegen patch: force the guest's motion blur off at its script setters.

Why these eight sites and not something else. The disc's entire blur-named string
set is shadowBlurX/Y and BlurFilter (2D Flash, nothing to do with a camera),
PPVolume_MotionBlurEffect, and URadialBlurComponent's five Kismet natives. The
volume name is not reachable from code at all:

  - no 4-byte big-endian pointer to 0x8208D0B4 exists in any section,
  - no instruction in .text materialises it: exactly one instruction in the whole
    image carries its low half (ori r3,r5,0xD0B4 at 0x82316E90) and the base
    register there is 0x8209, so it belongs to a different string,
  - guest_xrefs.py's lis/ori sweep over every section returns zero hits.

So the volume chain is cooked data with no host-side knob. The five component
natives ARE guest code, and all five end the same way - read one parameter out of
the script stream into a local, then load a virtual slot off the object and
call it. That virtual call is where the value becomes "how blurry", so it is where
the value is forced. The slot numbers are read out of the bodies, not guessed:

  sub_8253D728  execSetEnabled              vtable[+364], argument r4 (bool)
  sub_8253D690  execSetBlurOpacity          vtable[+360], argument f1
  sub_8253D5F8  execSetBlurFalloffExponent  vtable[+356], argument f1
  sub_8253D560  execSetBlurScale            vtable[+352], argument f1
  sub_82AF6130  execSetMaterial             vtable[+348], argument r4 (pointer)

execSetMaterial is only observed, never forced: substituting a null material is a
new crash surface and "blur off" is already fully expressed by the other four.

The remaining three sites are log-only entry probes on the post-process chain
natives (AGearPCexecSetPostProcessValues, ULocalPlayerexecInsertPostProcessingChain,
ULocalPlayerexecOverridePostProcessSettings - resolved from the same name/function
binding table). They exist because this patch's own hypothesis has to be testable:
if a full gameplay session logs zero component calls and the blur is still on
screen, the blur is being delivered through the chain and this file is not the fix.
Nothing is forced there because removing a post-process chain would take bloom,
DOF and colour grading with it.

The injection is one line immediately before the final virtual slot load, so the
guest's own parameter reading, its stream-offset advance and its call sequence all
stay intact - only the number handed to the component changes.

    python tools/patch_motion_blur.py generated/default/*.cpp

Exit 0 and print per-site counts. Idempotent: a body that already carries the host
call is reported as preexisting. Any anchor that does not match exactly once is a
hard failure, because a silently missing patch is how a build ships a feature that
is not in the binary.
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "GOWJ MOTION BLUR"
FLOAT_HOOK = "GowjBlurFloatArg"
UINT_HOOK = "GowjBlurIntArg"
PEEK_HOOK = "GowjBlurPeekArg"
ENTRY_HOOK = "GowjBlurEntry"

DECLS = {
    FLOAT_HOOK: f'extern "C" float {FLOAT_HOOK}(float requested, uint32_t site, '
                f"uint32_t frame);  // {MARK}\n",
    UINT_HOOK: f'extern "C" uint32_t {UINT_HOOK}(uint32_t requested, uint32_t site, '
               f"uint32_t frame);  // {MARK}\n",
    PEEK_HOOK: f'extern "C" uint32_t {PEEK_HOOK}(uint32_t requested, uint32_t site, '
               f"uint32_t frame);  // {MARK}\n",
    ENTRY_HOOK: f'extern "C" void {ENTRY_HOOK}(uint32_t site, uint32_t frame);  // {MARK}\n',
}

# kind "vtable": force the argument register of the final virtual call.
# kind "entry": probe at function entry, change nothing.
# dst/src/base are the registers codegen used for the slot load, read out of each
# body: execSetMaterial keeps its vtable in r8 and its slot in r7, which is why
# they are spelled per site instead of assumed once.
SITES = [
    dict(addr=0x8253D728, kind="vtable", slot=364, arg="uint", hook=UINT_HOOK,
         dst=6, src=7, base=7),
    dict(addr=0x8253D690, kind="vtable", slot=360, arg="float", hook=FLOAT_HOOK,
         dst=6, src=7, base=7),
    dict(addr=0x8253D5F8, kind="vtable", slot=356, arg="float", hook=FLOAT_HOOK,
         dst=6, src=7, base=7),
    dict(addr=0x8253D560, kind="vtable", slot=352, arg="float", hook=FLOAT_HOOK,
         dst=6, src=7, base=7),
    dict(addr=0x82AF6130, kind="vtable", slot=348, arg="peek", hook=PEEK_HOOK,
         dst=7, src=8, base=8),
    dict(addr=0x82AE6710, kind="entry"),
    dict(addr=0x82A4B0F8, kind="entry"),
    dict(addr=0x8253A7F0, kind="entry"),
]


def func_span(text, func):
    """(start, end) offsets of one generated function body, or None."""
    m = re.search(rf"^DEFINE_REX_FUNC\({func}\) \{{", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^DEFINE_REX_FUNC\(", text[m.end():], re.M)
    return m.start(), m.end() + (nxt.start() if nxt else len(text) - m.end())


def vtable_anchor(site):
    """The final virtual slot load, exactly as codegen wrote it."""
    slot, dst, base = site["slot"], site["dst"], site["base"]
    return (f"\t// lwz r{dst},{slot}(r{base})\n"
            f"\tctx.r{dst}.u64 = REX_LOAD_U32(ctx.r{base}.u32 + {slot});\n"
            f"\t// mtctr r{dst}\n")


def vtable_repl(site, anchor):
    addr = f"0x{site['addr']:X}u"
    if site["arg"] == "float":
        call = (f"\tctx.f1.f64 = double({FLOAT_HOOK}(float(ctx.f1.f64), {addr}, "
                "uint32_t(ctx.r31.u64)));  // " + MARK + "\n")
    elif site["arg"] == "uint":
        call = (f"\tctx.r4.u64 = {UINT_HOOK}(uint32_t(ctx.r4.u64), {addr}, "
                "uint32_t(ctx.r31.u64));  // " + MARK + "\n")
    else:
        call = (f"\tctx.r4.u64 = {PEEK_HOOK}(uint32_t(ctx.r4.u64), {addr}, "
                "uint32_t(ctx.r31.u64));  // " + MARK + "\n")
    return call + anchor


def patch_body(body, site, path):
    """Return (new_body, hook_name) or (body, None) when already patched."""
    func = f"sub_{site['addr']:X}"
    if site["kind"] == "entry":
        if ENTRY_HOOK in body:
            return body, None
        prologue = "\tREX_FUNC_PROLOGUE();\n"
        if body.count(prologue) != 1:
            raise SystemExit(f"{path}: {func} has {body.count(prologue)} prologues, "
                             "expected 1")
        call = (prologue +
                f"\t{ENTRY_HOOK}(0x{site['addr']:X}u, uint32_t(ctx.r3.u64));  // {MARK}\n")
        return body.replace(prologue, call, 1), ENTRY_HOOK

    anchor = vtable_anchor(site)
    if body.count(anchor) != 1:
        raise SystemExit(f"{path}: {func} final virtual call (slot {site['slot']}) "
                         f"matched {body.count(anchor)} times, expected exactly 1 - "
                         "codegen changed shape, refusing to guess")
    # The frame argument the injection reads. Every one of these natives saves the
    # incoming r3 into r31 before the call; if that ever stops being true the
    # injected line would report a stale pointer, so the patch refuses.
    if "\tctx.r31.u64 = ctx.r3.u64;\n" not in body:
        raise SystemExit(f"{path}: {func} has no 'mr r31,r3', frame argument invalid")
    hook = site["hook"]
    if hook in body:
        return body, None
    return body.replace(anchor, vtable_repl(site, anchor), 1), hook


def insert_decl(src, hook):
    decl = DECLS[hook]
    if decl in src:
        return src
    m = re.search(r'^(#include [^\n]*|// [^\n]*)$', src[:8000], re.M)
    cut = src.find("\n", m.end()) + 1 if m else src.index("\n") + 1
    return (src[:cut] + "\n" + f"// {MARK}: radial blur is forced off at the guest's\n"
            f"// own Kismet setters; gowj_motion_blur=true restores them.\n"
            + decl + src[cut:])


def write(path, text):
    tmp = path + ".blur.tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def patch_file(path):
    """Return the site addresses patched or already present in this TU.

    One read, all eight sites, one atomic write: several of these natives share a
    translation unit with each other, and rewriting the file per site would let a
    later site's read miss an earlier site's patch.
    """
    with open(path, encoding="utf-8") as f:
        src = f.read()
    original = src
    done = []
    for site in SITES:
        func = f"sub_{site['addr']:X}"
        span = func_span(src, func)
        if not span:
            continue
        lo, hi = span
        body = src[lo:hi]
        new_body, hook = patch_body(body, site, path)
        if new_body == body:
            # Either this run already patched it (the site address appears in an
            # injected call) or another TU owns this function; only the first
            # counts as done.
            if f"0x{site['addr']:X}u" in body:
                done.append(site["addr"])
            continue
        src = src[:lo] + new_body + src[hi:]
        if hook:
            src = insert_decl(src, hook)
        done.append(site["addr"])
    if src != original:
        write(path, src)
    return done


def collect(argv):
    files = []
    for a in argv[1:]:
        if not a.endswith(".cpp"):
            continue
        if any(ch in a for ch in "*?["):
            files.extend(sorted(glob.glob(a)))  # CMake hands the glob through cmd.exe
        else:
            files.append(a)
    if not files:
        d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                         "gowj", "generated", "default")
        files = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".cpp")]
    return files


def main(argv):
    files = collect(argv)
    touched = {}
    for path in files:
        for addr in patch_file(path):
            touched[addr] = path
    missing = [f"0x{s['addr']:X}" for s in SITES if s["addr"] not in touched]
    if missing:
        raise SystemExit(f"motion-blur patch: sites not patched: {', '.join(missing)} "
                         f"({len(files)} files scanned)")
    print("patch_motion_blur: " + ", ".join(
        f"sub_{a:X}->{os.path.basename(touched[a])}" for a in sorted(touched)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
