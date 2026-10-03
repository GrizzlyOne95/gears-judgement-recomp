// gowj - FPS / frame-time overlay (see gowj_fps_overlay.cpp).

#pragma once

#include <cstddef>
#include <cstdint>

namespace rex::glue {

void InstallFpsOverlay();
// kind 0 = Present, 1 = guest output refresh (the frames the game really rendered).
void FpsOverlayNote(size_t kind, uint64_t interval_us);

}  // namespace rex::glue
