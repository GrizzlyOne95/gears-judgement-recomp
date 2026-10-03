// gowj - ReXGlue Recompiled Project
//
// XamContent* import shim: logs every guest content request with the guest
// still inside the calling frame, so the argument marshalling can be measured
// instead of guessed. See gowj_content_probe.cpp for why the two obvious
// interception points (PPCFuncMappings, &__imp__) do not see these calls.

#pragma once

#include <filesystem>
#include <string>

namespace rex::glue {

void InstallContentProbe();

// Host directory every GearsCheckpoint root alias maps to, and a root (e.g.
// "SG3_0:") that is already registered. Aliases the guest invents later are
// mounted on demand from inside XamContentCreateEx.
void SetCheckpointDir(const std::filesystem::path& dir);
void NoteMountedRoot(const std::string& root);
std::filesystem::path CheckpointDir();
bool CheckpointRootReadOnly(const std::string& root);

}  // namespace rex::glue
