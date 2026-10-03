// gowj - ReXGlue Recompiled Project
//
// Host-side counterpart of tools/patch_motion_blur.py.
//
// The generated TUs do not include this header: the patcher inserts its own
// `extern "C"` declaration line, which is what the other three post-codegen
// patchers do too (see patch_mouse_smoothing.py / patch_raw_look.py). This file
// exists so the definitions in gowj_motion_blur.cpp have a declared contract and
// the two sides cannot drift into different signatures without a compile error on
// the host side.
//
// The `site` argument on every entry point is the guest address of the native
// being intercepted, so one host translation unit can serve all of them and the
// log says which one fired. `frame` is the native's incoming r3.
#pragma once

#include <cstdint>

extern "C" {

// Float parameter destined for URadialBlurComponent's virtual setter. Returns 0
// (no blur) unless gowj_motion_blur is set, which hands the guest's request back.
float GowjBlurFloatArg(float requested, uint32_t site, uint32_t frame);

// Integer/boolean parameter, forced the same way. execSetEnabled lands here.
uint32_t GowjBlurIntArg(uint32_t requested, uint32_t site, uint32_t frame);

// Observed only, value untouched. execSetMaterial goes here: a null material
// would be a new crash surface and "off" is already fully expressed by the four
// forcing sites.
uint32_t GowjBlurPeekArg(uint32_t requested, uint32_t site, uint32_t frame);

// Entry probe on the post-process chain natives. Log only - taking a whole
// post-process chain away would remove bloom, DOF and colour grading with it.
void GowjBlurEntry(uint32_t site, uint32_t frame);

}
