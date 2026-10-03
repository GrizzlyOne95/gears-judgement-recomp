// gowj - ReXGlue Recompiled Project
//
// Host switches for the ported xenia-canary game patches
// (tools/patch_game_patches.py). The generated code asks GowjGamePatch(id)
// at each patched instruction, so every patch is a live cvar.

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, gowj_motion_blur);

REXCVAR_DEFINE_BOOL(gowj_film_grain, false, "gfx",
                    "Keep the game's animated film grain. False (default) removes it: "
                    "per-frame noise is a large part of the image 'shimmering'.");
REXCVAR_DEFINE_BOOL(gowj_game_fxaa, false, "gfx",
                    "Keep the game's own FXAA pass. False (default) disables it (sharper, "
                    "more edge aliasing). Pair true with gowj_scaling_fix for "
                    "anti-aliasing that is not blurred at draw_resolution_scale 2.");
REXCVAR_DEFINE_BOOL(gowj_scaling_fix, true, "gfx",
                    "Community Resolution Scaling Fix: makes the post-process chain "
                    "(including the game's FXAA) resolution-scaling friendly.");

extern "C" int GowjGamePatch(int id) {
  switch (id) {
    case 1: return !REXCVAR_GET(gowj_film_grain);
    case 2: return REXCVAR_GET(gowj_scaling_fix);
    case 3: return !REXCVAR_GET(gowj_game_fxaa);
    case 4: return !REXCVAR_GET(gowj_motion_blur);
    default: return 0;
  }
}
