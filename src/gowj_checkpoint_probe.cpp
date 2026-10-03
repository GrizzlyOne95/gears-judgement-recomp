// gowj - ReXGlue Recompiled Project
//
// Checkpoint native trace. UGearEngine.SaveCheckpoint / LoadCheckpoint /
// SetCheckpoint / ParseCheckpointData and UOnlineSubsystemLive's save-game
// natives are the layer that turns checkpoint bytes into "valid" or "corrupted".
// Addresses come from the FNativeFunctionLookup table (name ptr, exec ptr).
// Each exec is wrapped through the function dispatcher; the wrapper logs the
// call, the calling object and the 32-bit value written to the RESULT pointer.

#include "gowj_checkpoint_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

namespace {

struct Native {
  uint32_t addr;
  const char* name;
  PPCFunc* orig;
};

Native g_natives[] = {
    {0x82AF4A90, "GearEngine.SaveCheckpoint", nullptr},
    {0x82AF4B18, "GearEngine.LoadCheckpoint", nullptr},
    {0x82AF4B28, "GearEngine.SetCheckpoint", nullptr},
    {0x82AF4C38, "GearEngine.ParseCheckpointData", nullptr},
    {0x829AC270, "OnlineSubsystemLive.ReadSaveGameData", nullptr},
    {0x829AC430, "OnlineSubsystemLive.GetSaveGameData", nullptr},
    {0x829AC6F8, "OnlineSubsystemLive.WriteSaveGameData", nullptr},
    {0x829AC950, "OnlineSubsystemLive.DeleteSaveGame", nullptr},
    {0x829ACAB8, "OnlineSubsystemLive.ClearSaveGames", nullptr},
};
constexpr size_t kCount = sizeof(g_natives) / sizeof(g_natives[0]);
std::atomic<bool> g_installed{false};

uint32_t Be32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}

template <size_t I>
void Wrap(PPCContext& ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32, stack = ctx.r4.u32, result = ctx.r5.u32;
  const uint32_t code = stack ? Be32(base, stack + 24) : 0;
  g_natives[I].orig(ctx, base);
  static std::atomic<int> budget{200};
  if (budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
    REXLOG_WARN("CKPT {} self={:08x} code={:08x} result@{:08x}={:08x} tid={:X}",
                g_natives[I].name, self, code, result, result ? Be32(base, result) : 0,
                GetCurrentThreadId());
  }
}

template <size_t... Is>
void InstallAll(rex::runtime::FunctionDispatcher* fd, std::index_sequence<Is...>) {
  ((g_natives[Is].orig = fd->GetFunction(g_natives[Is].addr),
    g_natives[Is].orig && fd->SetFunction(g_natives[Is].addr, &Wrap<Is>)),
   ...);
}

}  // namespace

namespace rex::glue {

void InstallCheckpointProbe() {
  if (g_installed.load(std::memory_order_acquire)) return;
  auto* rt = rex::Runtime::instance();
  auto* fd = rt ? rt->function_dispatcher() : nullptr;
  if (!fd || !fd->GetFunction(g_natives[0].addr)) return;
  if (g_installed.exchange(true)) return;
  InstallAll(fd, std::make_index_sequence<kCount>{});
  REXLOG_INFO("CKPT probe hooked {} checkpoint/save natives", kCount);
}

}  // namespace rex::glue
