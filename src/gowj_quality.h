// gowj - capability-driven quality tier + frame-rate auto-tuner (see gowj_quality.cpp).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rex::glue {

struct QualityPlan {
  int tier = 1;  // 0 low (handheld / integrated), 1 medium, 2 high
  const char* tier_name = "medium";
  int scale = 1;  // draw_resolution_scale_x/y
  bool smaa = true;
  bool fsr = true;
  int aniso = 4;  // anisotropic_override (3 = 4x, 4 = 8x, 5 = 16x)
  bool msaa2x = true;
  int tex_soft_mb = 1024, tex_hard_mb = 2048, tex_rt_mb = 128;
  int pso_threads = 2;
  bool rov = false;  // render_target_path_d3d12 = "rov" (else runtime auto)
  // What the probe saw.
  std::string gpu_name;
  uint32_t vendor = 0;
  uint64_t vram_mb = 0, budget_mb = 0;
  bool uma = false, rov_supported = false;
  unsigned cores = 0;
  std::string source;  // "auto", "override:<x>", "tuned(-N)"
  std::string reason;
  std::string summary;  // one log line describing the decision (logging is not up yet when it is made)
};

// Probes the adapter the runtime will use (cvar d3d12_adapter, else the most-VRAM one),
// and picks a tier. Call before the GPU initializes (ApplyDisplayDefaults).
// `config_path` is read directly (gowj_quality, d3d12_adapter) because the config file is only
// applied to the settings system after the point where the plan is needed.
QualityPlan ChooseQualityPlan(uint32_t monitor_height, const std::filesystem::path& config_path = {});

// Called once the config file is known: only a tier-chosen resolution (not one the user
// wrote) may be changed by the tuner.
void QualityStartTuner(const QualityPlan& plan, bool managed);

// Per guest frame (refresh stream), from the PERFWATCH hooks.
void QualityNoteFrame(uint64_t interval_us);

// Text the FPS overlay shows as its last line ("" = nothing).
std::string QualityOverlayLine();

}  // namespace rex::glue
