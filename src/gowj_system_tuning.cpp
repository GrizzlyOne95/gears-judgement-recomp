// gowj - process-level tuning.
//
// Opts the process out of Windows power and timer-resolution throttling (EcoQoS). On handhelds
// and hybrid-core CPUs Windows can otherwise park the game's threads on slow cores.

#include "gowj_system_tuning.h"

#include <windows.h>

#include <rex/logging.h>

namespace rex::glue {

void InstallSystemTuning() {
  // PROCESS_POWER_THROTTLING_STATE, with the constants spelled out for older SDK headers.
  struct State {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
  } st{1, 0x1 /*EXECUTION_SPEED*/ | 0x4 /*IGNORE_TIMER_RESOLUTION*/, 0 /*throttling off*/};
  const BOOL ok =
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof(st));
  REXLOG_INFO("Power throttling opt-out: {}", ok ? "applied" : "not supported here");
}

}  // namespace rex::glue
