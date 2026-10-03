/**
 * @file        gowj_motion_blur.cpp
 * @brief       Kill the guest's motion blur at its script-facing setters
 * @copyright   BSD 3-Clause License
 */

#include "gowj_motion_blur.h"

#include <bit>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

// What the runtime's own settings dump says, and why this is the patchable surface.
//
// The disc's whole blur-named string set is shadowBlurX/Y, BlurFilter (2D flash,
// irrelevant), PPVolume_MotionBlurEffect and URadialBlurComponent's five Kismet
// natives. The PPVolume name has NO reference anywhere - not a data pointer to its
// address, not a lis/ori pair materialising it in .text (measured: exactly one
// instruction in the image carries its low half 0xD0B4, and it belongs to a
// 0x8209xxxx base, a different string). So the volume chain is cooked data with no
// host-side knob. The component's five natives are real guest code and each one
// ends the same way - read one parameter out of the script stream into a local,
// then a virtual call carrying it:
//
//   execSetEnabled               -> vtable[+364], r4 = (param != 0)
//   execSetBlurOpacity           -> vtable[+360], f1 = param
//   execSetBlurFalloffExponent   -> vtable[+356], f1 = param
//   execSetBlurScale             -> vtable[+352], f1 = param
//   execSetMaterial              -> vtable[+348], r4 = param (observed only: a
//                                   null material would be a new crash surface,
//                                   and "off" is already fully expressed by the
//                                   other four)
//
// Zeroing those arguments is off in every sense the component has: disabled, and
// with no opacity, falloff or scale left for anything to re-enable. Every site
// also logs, because one run has to answer a second question - if a full session
// produces no calls here, the blur the player sees is the volume path and this
// file is not the fix. That is measured next, not assumed now.
REXCVAR_DEFINE_BOOL(gowj_motion_blur, false, "gfx",
                    "Keep the game's motion blur. False (default) removes it: the "
                    "radial speed blur and the camera motion blur shaders are "
                    "patched to zero-length (gowj_blur_kill.cpp), and "
                    "URadialBlurComponent is forced off.");

namespace {
// Sparse guest addresses, so index by arrival order rather than hashing. The
// counters are relaxed atomics: a lost increment in a diagnostic line is worth
// nothing next to a lock on the render path.
constexpr size_t kMaxSites = 8;
constexpr uint64_t kLogFirst = 12;

struct Site {
  uint32_t addr{};
  std::atomic<uint64_t> calls{};
};
Site g_sites[kMaxSites];
std::atomic<uint32_t> g_site_count{};

uint32_t SiteIndex(uint32_t addr) {
  const uint32_t n = g_site_count.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < n; ++i) {
    if (g_sites[i].addr == addr) return i;
  }
  const uint32_t i = g_site_count.fetch_add(1, std::memory_order_relaxed);
  if (i < kMaxSites) g_sites[i].addr = addr;
  return i < kMaxSites ? i : kMaxSites;
}

const char* SiteName(uint32_t addr) {
  switch (addr) {
  case 0x8253D728: return "RadialBlur.SetEnabled";
  case 0x8253D690: return "RadialBlur.SetBlurOpacity";
  case 0x8253D5F8: return "RadialBlur.SetBlurFalloffExponent";
  case 0x8253D560: return "RadialBlur.SetBlurScale";
  case 0x82AF6130: return "RadialBlur.SetMaterial";
  case 0x82AE6710: return "GearPC.SetPostProcessValues";
  case 0x82A4B0F8: return "LocalPlayer.InsertPostProcessingChain";
  case 0x8253A7F0: return "LocalPlayer.OverridePostProcessSettings";
  default: return "?";
  }
}

bool IsFloatSite(uint32_t addr) {
  return addr == 0x8253D690 || addr == 0x8253D5F8 || addr == 0x8253D560;
}

// `frame` is the native's incoming r3 - the Kismet frame it runs against. The
// component itself is only reachable through the virtual call, so it is not
// claimed here.
void NoteCall(uint32_t addr, uint32_t arg_bits, uint32_t frame) {
  const uint32_t i = SiteIndex(addr);
  if (i >= kMaxSites) return;
  const uint64_t n = g_sites[i].calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n > kLogFirst && (n & 4095) != 0) return;
  if (IsFloatSite(addr)) {
    REXLOG_WARN("MOTIONBLUR {} calls={} guest={:X} arg={} f={} frame={:X}",
                SiteName(addr), n, addr, arg_bits,
                std::bit_cast<float>(arg_bits), frame);
  } else {
    REXLOG_WARN("MOTIONBLUR {} calls={} guest={:X} arg={:X} frame={:X}",
                SiteName(addr), n, addr, arg_bits, frame);
  }
}
}  // namespace

extern "C" float GowjBlurFloatArg(float requested, uint32_t site, uint32_t frame) {
  uint32_t bits{};
  std::memcpy(&bits, &requested, sizeof(bits));
  NoteCall(site, bits, frame);
  return REXCVAR_GET(gowj_motion_blur) ? requested : 0.0f;
}

extern "C" uint32_t GowjBlurIntArg(uint32_t requested, uint32_t site, uint32_t frame) {
  NoteCall(site, requested, frame);
  return REXCVAR_GET(gowj_motion_blur) ? requested : 0u;
}

extern "C" uint32_t GowjBlurPeekArg(uint32_t requested, uint32_t site, uint32_t frame) {
  NoteCall(site, requested, frame);
  return requested;
}

extern "C" void GowjBlurEntry(uint32_t site, uint32_t frame) {
  NoteCall(site, 0, frame);
}
