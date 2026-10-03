// gowj - ReXGlue Recompiled Project
//
// Removes Gears' thumbstick look acceleration from mouse look, and measures the
// final look axes where the game consumes them.
//
// UGearPlayerInput.ViewAcceleration / ViewPitchAcceleration (natives, found via
// the FNativeFunctionLookup table: name 0x821B6F40 -> exec 0x82AF0DE8, name
// 0x821B6F68 -> exec 0x82AF0E78) run every frame on the already-summed look
// axes. Read from the implementations (sub_82A8CA78 / sub_82A8CBE8):
//
//   if |aTurn| > threshold:  timer += dt / ramp_time   (clamped to max)
//   else:                    timer -= dt * k / ramp_time (clamped to 0)
//   aTurn *= 1 + timer * gain
//
// with aTurn at +276 / timer +432 (yaw) and aLookUp at +288 / timer +436
// (pitch). A mouse turn therefore scaled with how long the hand had already been
// moving. While raw look is live the exec still runs (it consumes the script's
// parameter from the FFrame), then the axis is restored and the timer held at 0.

#include "gowj_view_accel.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

extern "C" bool GowjRawLookLive();
extern "C" double GowjTakeFrameWant(int axis);

REXCVAR_DEFINE_BOOL(mnk_view_accel, false, "mnk",
                    "Keep the game's thumbstick look acceleration on mouse look. "
                    "False (default) removes it so turn is 1:1 with the mouse.");
REXCVAR_DEFINE_BOOL(mnk_uncapped_look, true, "mnk",
                    "Write the mouse's full angle into the game's look axes after "
                    "the input handler's +/-1 clamp, so fast flicks are not capped "
                    "at the thumbstick's maximum turn rate.");

namespace {

std::atomic<int64_t> g_last_qpc[2]{};

int64_t QpcFreq() {
  static const int64_t f = [] {
    LARGE_INTEGER q;
    QueryPerformanceFrequency(&q);
    return q.QuadPart;
  }();
  return f;
}

constexpr uint32_t kYawExec = 0x82AF0DE8;
constexpr uint32_t kPitchExec = 0x82AF0E78;

PPCFunc* g_yaw_orig = nullptr;
PPCFunc* g_pitch_orig = nullptr;
std::atomic<bool> g_installed{false};

// Per-second peaks, [axis][0=pre accel, 1=post accel].
std::atomic<float> g_peak[2][2]{};
std::atomic<uint32_t> g_calls[2]{};
std::atomic<uint64_t> g_window_start{0};

float LoadF(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

uint32_t LoadRaw(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return v;
}

void StoreRaw(uint8_t* base, uint32_t addr, uint32_t v) { std::memcpy(base + addr, &v, 4); }

void Peak(std::atomic<float>& slot, float v) {
  v = std::fabs(v);
  float prev = slot.load(std::memory_order_relaxed);
  while (v > prev && !slot.compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
  }
}

void MaybeReport() {
  const uint64_t now = GetTickCount64();
  uint64_t start = g_window_start.load(std::memory_order_relaxed);
  if (start == 0) {
    g_window_start.store(now, std::memory_order_relaxed);
    return;
  }
  if (now - start < 1000 ||
      !g_window_start.compare_exchange_strong(start, now, std::memory_order_relaxed)) {
    return;
  }
  REXLOG_INFO("VIEWACCEL aTurn pre={:.1f} post={:.1f} aLookUp pre={:.1f} post={:.1f} "
              "calls={}x{} strip={}",
              g_peak[0][0].exchange(0), g_peak[0][1].exchange(0), g_peak[1][0].exchange(0),
              g_peak[1][1].exchange(0), g_calls[0].exchange(0), g_calls[1].exchange(0),
              !REXCVAR_GET(mnk_view_accel) && GowjRawLookLive());
}

template <int kAxis, uint32_t kAxisOff, uint32_t kTimerOff, PPCFunc** kOrig>
void Hook(PPCContext& ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  const bool strip = self && !REXCVAR_GET(mnk_view_accel) && GowjRawLookLive();
  const uint32_t raw = self ? LoadRaw(base, self + kAxisOff) : 0;
  if (self) Peak(g_peak[kAxis][0], LoadF(base, self + kAxisOff));
  (*kOrig)(ctx, base);
  if (strip) {
    StoreRaw(base, self + kAxisOff, raw);
    StoreRaw(base, self + kTimerOff, 0);  // +0.0f
  }
  // Uncapped mouse look: the axis arriving here was clamped to +/-1 by the input
  // handler, so replace it with the frame's whole mouse angle divided by the
  // frame's own duration - the rate the game then integrates back over that
  // same frame. Exact at any flick speed and any frame rate.
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const int64_t last = g_last_qpc[kAxis].exchange(now.QuadPart, std::memory_order_relaxed);
  const double want = GowjTakeFrameWant(kAxis);
  if (self && want != 0.0 && GowjRawLookLive() && REXCVAR_GET(mnk_uncapped_look)) {
    const double gap = last ? double(now.QuadPart - last) / double(QpcFreq()) : 1.0;
    if (gap <= 0.1) {
      const double dt = gap < 0.004 ? 0.004 : gap;
      const float value = static_cast<float>(want / dt);
      uint32_t bits;
      std::memcpy(&bits, &value, 4);
      StoreRaw(base, self + kAxisOff, __builtin_bswap32(bits));
    }
  }
  if (self) Peak(g_peak[kAxis][1], LoadF(base, self + kAxisOff));
  g_calls[kAxis].fetch_add(1, std::memory_order_relaxed);
  MaybeReport();
}

}  // namespace

namespace rex::glue {

void InstallViewAccelHook() {
  if (g_installed.load(std::memory_order_acquire)) return;
  auto* rt = rex::Runtime::instance();
  auto* fd = rt ? rt->function_dispatcher() : nullptr;
  if (!fd) return;
  PPCFunc* yaw = fd->GetFunction(kYawExec);
  PPCFunc* pitch = fd->GetFunction(kPitchExec);
  if (!yaw || !pitch) return;
  if (g_installed.exchange(true)) return;
  g_yaw_orig = yaw;
  g_pitch_orig = pitch;
  const bool a = fd->SetFunction(kYawExec, &Hook<0, 276, 432, &g_yaw_orig>);
  const bool b = fd->SetFunction(kPitchExec, &Hook<1, 288, 436, &g_pitch_orig>);
  REXLOG_INFO("VIEWACCEL hooked UGearPlayerInput.ViewAcceleration={} "
              "ViewPitchAcceleration={}",
              a, b);
}

}  // namespace rex::glue
