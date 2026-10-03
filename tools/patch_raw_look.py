"""Post-codegen patch: give the guest's look axes a real mouse delta.

Why this site and not the stick value
-------------------------------------
The guest has no mouse channel, so the host has always fed look through the
right thumbstick: mouse pixels -> int16 deflection -> the guest's own integrator.
That path has a hard ceiling the host cannot tune away. The camera integrates

    angle = k * deflection * dt      with   |deflection| <= 32767

so the fastest possible turn is k * 32767 deg/s, no matter how fast the guest
polls, how high mnk_sensitivity is, or how many pixels a flick covers. Raising
gain buys nothing: it raises the total angle of a flick and the time it takes to
drain by the same factor, which is the "sliding on ice" tail the player feels.
The ONLY fix is to hand the camera an angle instead of a deflection.

Where that happens
------------------
sub_829A18C8 is the one function that turns a gamepad record into per-axis
virtual calls. Its stack frame holds an X_INPUT_GAMEPAD based at r1+98, proven
by the two single-byte trigger loads at +102/+103 and the two-byte wButtons
load at +100 - which pins the four int16 sticks at +104 LX, +106 LY, +108 RX,
+110 RY. Each becomes an axis call with arg1 = float(stick) * f29 (the pool's
normalization constant) and arg2 = f30 (this frame's delta):

    +108 RX -> extsh -> std 152 -> lfd -> fcfid -> frsp f6 -> fmuls f1,f6,f29   (YAW)
    +110 RY -> extsh -> neg     -> std 112 -> lfd -> fcfid -> frsp f3 -> fmuls f1,f3,f29

RY is the only axis negated, which is the XInput-up-to-look-up flip and confirms
110 is pitch. +104/+106 are the LEFT stick (walking) and are never touched here.

The patch rewrites the two float arguments of each look-axis call, and the reason
it has to rewrite BOTH is the whole point:

    arg1 = float(stick) * f29     f29 measured = 1/32768, i.e. arg1 is a
                                  NORMALIZED deflection, domain [-1, 1]
    arg2 = f30                    this frame's delta time (measured 0.016-0.025)
    turn    = arg1 * arg2

Putting the mouse's angle into arg1 (the first attempt) therefore saturates: a
231-pixel flick asked for arg1 = 47 and the engine kept 1, discarding every
surplus unit. A saturated arg1 also makes the turn a function of TIME alone and
not of mouse distance, which is why no gain setting ever fixed it. The player's
"it acts exactly like emulating a joystick should, especially with fast flicks
that it can't handle" was this, not the +/-32767 stick clamp - that clamp is one
function further out, and the real ceiling is this +/-1 one step further in.

So GowjRawLookAxis pins arg1 at exactly full deflection, where there is nothing
left to clamp, and moves the flick's whole angle into arg2. The single call then
delivers the exact angle in one frame with no bank and no drain, and it stays
proportional to pixels at any poll rate because the value handed over is the
angle, not a request to turn toward it.

    python tools/patch_raw_look.py generated/default/*.cpp

Exit 0 and print per-file counts. Idempotent. FAILS CLOSED: if the structural
invariants that pin the axis mapping do not hold, it refuses rather than
guessing - a patch on the wrong slot turns the character's walk into a spin.
"""
from __future__ import annotations

import glob
import os
import re
import sys

DECL_MARK = "GOWJ RAW LOOK"
FUNC = "sub_829A18C8"
AXIS = "GowjRawLookAxis"
TARGET = "GowjRawLookTarget"
# One extern per line, marker on every line: strip_decl then drops the block by
# marker alone, so an older tree's differently-wrapped declarations still revert
# cleanly.
DECL = (f'extern "C" int {AXIS}(int axis, float axis_sign, float stick_angle, '
        f'float f29, float dt, float *out_axis, float *out_dt);  // {DECL_MARK}\n'
        f'extern "C" void {TARGET}(int axis, uint32_t guest_addr);  // '
        f'{DECL_MARK}\n')

# The four int16 stick slots, in X_INPUT_GAMEPAD order off the record base.
SLOT_LX, SLOT_LY, SLOT_RX, SLOT_RY = 104, 106, 108, 110

# axis 0 = yaw (RX), axis 1 = pitch (RY) - must match GowjRawLookUnits' callers
# in gowj_kbm_input.cpp, which bank add_x into 0 and add_y into 1.
#
# `sign` is +1 for yaw and -1 for pitch because the guest computes its pitch
# argument as float(-RY) ("neg r11,r4"), while the bank holds a stick-convention
# RY-equivalent. The injection happens downstream of that negation, so it has to
# reapply it - forgetting to was the "up and down is reversed" report.
#
# `sign` is passed to the host as a float because the helper applies it to the
# bank, not to the guest's own value: by the pitch site the guest has already
# negated its stick read, and only the injected term needs correcting.
AXES = (
    {
        "axis": 0,
        "name": "yaw",
        "slot": SLOT_RX,
        "sign": 1,
        "anchor": "\t// fmuls f1,f6,f29\n"
                  "\tctx.f1.f64 = double(float(ctx.f6.f64 * ctx.f29.f64));\n",
    },
    {
        "axis": 1,
        "name": "pitch",
        "slot": SLOT_RY,
        "sign": -1,
        "anchor": "\t// fmuls f1,f3,f29\n"
                  "\tctx.f1.f64 = double(float(ctx.f3.f64 * ctx.f29.f64));\n",
    },
)


def replacement(a):
    sign = "1.0" if a["sign"] > 0 else "-1.0"
    return (
        a["anchor"]
        + "\t// " + DECL_MARK + ": this call's two float arguments are a NORMALIZED\n"
        "\t// deflection (f29 measured = 1/32768) and the frame's dt, and the engine\n"
        "\t// turns by their product - so arg1 saturates at +/-1 and every unit of a\n"
        "\t// fast flick above full deflection is discarded there, which is the\n"
        "\t// player's 'it acts exactly like emulating a joystick should, especially\n"
        "\t// with fast flicks that it can't handle'. GowjRawLookAxis rewrites both\n"
        "\t// arguments, but only as far as it must: a move that fits inside one real\n"
        "\t// frame goes through as an honest analog deflection with the guest's own\n"
        "\t// dt untouched, and ONLY a flick above the engine's maximum turn rate pins\n"
        "\t// arg1 at full deflection and carries the angle in arg2, because 'faster\n"
        "\t// than max rate' has no other representation in a rate channel. (Lying\n"
        "\t// about dt on every call is what an earlier build did, and every\n"
        "\t// time-based term downstream of this call ran at the wrong step: 'too\n"
        "\t// sensitive and slidey'.) axis_sign reapplies the guest's own pitch\n"
        "\t// negation; mnk_raw_look_scale is the gain, mnk_raw_look_mode the split,\n"
        "\t// and KBMRATE's dem= prints the deflection demanded so a rate-ceiling\n"
        "\t// problem can never again be confused with a smoothing one.\n"
        "\t{\n"
        "\t\tfloat gowj_axis = float(ctx.f1.f64);\n"
        "\t\tfloat gowj_dt = float(ctx.f2.f64);\n"
        f"\t\tif ({AXIS}({a['axis']}, {sign}f, gowj_axis, float(ctx.f29.f64),\n"
        "\t\t\t\t\t  float(ctx.f30.f64), &gowj_axis, &gowj_dt)) {\n"
        "\t\t\tctx.f1.f64 = double(gowj_axis);\n"
        "\t\t\tctx.f2.f64 = double(gowj_dt);\n"
        "\t\t}\n"
        "\t}\n"
    )


def func_body_span(text):
    m = re.search(rf"^DEFINE_REX_FUNC\({FUNC}\) \{{", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^DEFINE_REX_FUNC\(", text[m.end():], re.M)
    return m.start(), m.end() + (nxt.start() if nxt else len(text) - m.end())


def check_structure(body):
    """Pin the axis mapping. Raise SystemExit with the reason if it doesn't hold."""
    def load_at(slot):
        return body.find(f"REX_LOAD_U16(ctx.r1.u32 + {slot})")

    def trig_at(slot):
        return body.find(f"REX_LOAD_U8(ctx.r1.u32 + {slot})")

    buttons = body.find("REX_LOAD_U16(ctx.r1.u32 + 100)")
    if buttons < 0:
        raise SystemExit(f"{FUNC}: no wButtons load at r1+100 - the record base "
                         "moved, so the stick slots below are not trustworthy")
    lt, rt = trig_at(102), trig_at(103)
    if lt < 0 or rt < 0:
        raise SystemExit(f"{FUNC}: trigger bytes not at r1+102/+103 (found "
                         f"{lt}, {rt}) - struct layout assumption broken")

    slots = {s: load_at(s) for s in (SLOT_LX, SLOT_LY, SLOT_RX, SLOT_RY)}
    for s, at in slots.items():
        if at < 0:
            raise SystemExit(f"{FUNC}: no int16 load for stick slot r1+{s}")
    if not slots[SLOT_LX] < slots[SLOT_LY] < slots[SLOT_RX] < slots[SLOT_RY]:
        raise SystemExit(f"{FUNC}: stick slots are not in LX,LY,RX,RY order: {slots}")

    # RY must be the negated axis; that sign flip is what identifies pitch.
    if body.find("static_cast<int64_t>(-ctx.r4.u64)", slots[SLOT_RY]) < 0:
        raise SystemExit(f"{FUNC}: no negation after the r1+{SLOT_RY} load - cannot "
                         "confirm that slot is pitch")

    # Each axis's call must take f30 as its multiplier, between its own stick load
    # and its anchor, or the raw/f30 division would not cancel.
    for a in AXES:
        anchor = body.find(a["anchor"])
        if anchor < 0:
            raise SystemExit(f"{FUNC}: {a['name']} anchor not found")
        if not slots[a["slot"]] < anchor:
            raise SystemExit(f"{FUNC}: {a['name']} anchor precedes its own stick "
                             f"load at r1+{a['slot']} - wrong site")
        after = [body.find(b["anchor"], anchor + 1) for b in AXES if b is not a]
        end = min([x for x in after if x >= 0] or [len(body)])
        if "ctx.f2.f64 = ctx.f30.f64" not in body[slots[a["slot"]]:anchor]:
            raise SystemExit(f"{FUNC}: {a['name']} call does not pass f30 as arg2 "
                             "- the dt cancellation would not hold")
        if "REX_CALL_INDIRECT_FUNC" not in body[anchor:end]:
            raise SystemExit(f"{FUNC}: {a['name']} anchor is not followed by the "
                             "axis virtual call")


CALL_LINE = "\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);\n"


def target_line(a):
    return f"\t{TARGET}({a['axis']}, ctx.ctr.u32);  // {DECL_MARK}\n"


def insert_target(body, a):
    """Record where the axis virtual call actually went.

    The look handler is reached through a vtable slot, so its address is not in
    the generated code at all. Whether it clamps the injected value is the open
    question, and this is the only way to name the function to go read.

    BEFORE the call, not after: PPC CTR is caller-saved, so reading ctx.ctr on
    the return edge reports whatever the callee left there. The first capture
    did exactly that and named an atomic CAS helper (`lwarx`/`stwcx.`) that is
    not on the look path at all.
    """
    at = body.find(a["anchor"])
    if at < 0:
        raise SystemExit(f"{FUNC}: cannot place the {TARGET} note, {a['name']} "
                         "anchor is gone")
    cut = body.find(CALL_LINE, at)
    if cut < 0:
        raise SystemExit(f"{FUNC}: no virtual call after the {a['name']} anchor")
    head = body.rfind("\n\tctx.lr = ", 0, cut)
    if head < 0 or head + 1 < at:
        raise SystemExit(f"{FUNC}: no return-address store before the "
                         f"{a['name']} virtual call - refusing to note the "
                         "wrong site")
    return body[:head + 1] + target_line(a) + body[head + 1:]


def strip_injection(body):
    """Remove every injected block and note from a function body, any version."""
    out = []
    skipping = False
    for line in body.split("\n"):
        if not skipping and line.startswith(f"\t// {DECL_MARK}: "):
            skipping = True
            continue
        if skipping:
            if line == "\t}":
                skipping = False
            continue
        if f"{TARGET}(" in line and line.rstrip().endswith(f"// {DECL_MARK}"):
            continue
        out.append(line)
    if skipping:
        raise SystemExit(f"{FUNC}: unterminated {DECL_MARK} block, refusing to rewrite")
    return "\n".join(out)


def strip_decl(src):
    """Drop the marker comment and every host declaration line, any version.

    Marker-first, then named signatures for declarations an older patcher wrote
    without the marker on every line. A bare "extern" match would delete this
    function's own host declarations, so signatures are listed instead.
    """
    drop = (DECL_MARK,
            "GowjRawLookUnits(int axis);",
            "GowjRawLookNote(int axis,",
            "float f29, float dt, float *out_axis,",
            "float *out_dt);",
            f"{TARGET}(int axis,")
    return "\n".join(l for l in src.split("\n") if not any(d in l for d in drop))


def revert_file(path):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if DECL_MARK not in src:
        return 0
    span = func_body_span(src)
    if not span:
        raise SystemExit(f"{path}: marker present but {FUNC} absent - manual cleanup")
    lo, hi = span
    body = strip_injection(src[lo:hi])
    out = strip_decl(src[:lo] + body + src[hi:])
    write(path, out)
    return 1


def patch_file(path):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    span = func_body_span(src)
    if not span:
        return 0, 0
    lo, hi = span
    body = src[lo:hi]
    want = [replacement(a) for a in AXES]
    have = [body.count(w) for w in want]
    # Line-exact ("\n" + the line): a bare count substring-matches a deeper-
    # indented copy, which is how a stale one-tab-short note read as current.
    noted = [body.count("\n" + target_line(a)) for a in AXES]
    if have == [1, 1] and noted == [1, 1]:
        if DECL not in src:
            write(path, insert_decl(src))
        return 0, 2
    if DECL_MARK in body:
        raise SystemExit(
            f"{path}: {FUNC} carries an OUT OF DATE {DECL_MARK} injection "
            f"(injection {have} of [1, 1], target note {noted} of [1, 1]). "
            "Run --revert first; half-rewriting a look site is how the pitch "
            "sign got inverted.")
    check_structure(body)
    out = src
    for a, w in zip(AXES, want):
        b = out[lo:hi]
        if b.count(a["anchor"]) != 1:
            raise SystemExit(f"{path}: {b.count(a['anchor'])} {a['name']} anchors "
                             "in " + FUNC + ", expected exactly 1")
        out = out[:lo] + b.replace(a["anchor"], w, 1) + out[hi:]
        b = out[lo:hi]
        out = out[:lo] + insert_target(b, a) + out[hi:]
    out = insert_decl(out)
    write(path, out)
    return len(AXES), 0


def insert_decl(src):
    if DECL in src:
        return src
    m = re.search(r'^(#include [^\n]*|// [^\n]*)$', src[:8000], re.M)
    cut = src.find("\n", m.end()) + 1 if m else src.index("\n") + 1
    return (src[:cut] + "\n"
            + f"// {DECL_MARK}: mouse angle injected into {FUNC}'s look axes\n"
            + DECL + src[cut:])


def write(path, text):
    tmp = path + ".rawlook.tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    # CMake hands us the glob literally through cmd.exe; nothing expands it here.
    files = []
    for a in args:
        if not a.endswith(".cpp"):
            continue
        if any(ch in a for ch in "*?["):
            files.extend(sorted(glob.glob(a)))
        else:
            files.append(a)
    if not files:
        files = glob_default()

    if "--revert" in argv:
        done = [os.path.basename(p) for p in files if revert_file(p)]
        print(f"patch_raw_look: reverted {done or 'nothing (tree was clean)'}")
        return 0

    applied = pre = 0
    touched = []
    for path in files:
        a, p = patch_file(path)
        applied += a
        pre += p
        if a or p:
            touched.append(os.path.basename(path))
    total = applied + pre
    if total != len(AXES):
        raise SystemExit(f"patch_raw_look: {applied} applied + {pre} preexisting "
                         f"across {len(files)} files, expected exactly "
                         f"{len(AXES)} (yaw+pitch)")
    print(f"patch_raw_look: {FUNC} yaw+pitch injected in {touched} "
          f"({'new' if applied else 'already patched'})")
    return 0


def glob_default():
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                     "gowj", "generated", "default")
    return [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".cpp")]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
