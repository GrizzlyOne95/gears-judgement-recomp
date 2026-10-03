// gowj - AMD FSR 1.0 (EASU + RCAS) on the final image (see gowj_fsr.cpp).

#pragma once

#include <cstdint>

struct ID3D12Resource;

extern "C" void GowjFsrNoteGuestOutput(ID3D12Resource* resource, uint32_t width,
                                       uint32_t height);

namespace rex::glue {

void InstallFsr();

}  // namespace rex::glue
