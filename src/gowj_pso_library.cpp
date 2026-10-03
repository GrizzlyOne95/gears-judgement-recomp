// gowj - persistent D3D12 pipeline library.
//
// Hooks ID3D12Device::CreateGraphicsPipelineState on the shared device vtable and answers
// creates from a pipeline library stored in %LOCALAPPDATA%\GearsOfWarJudgmentPC\cache.
// GOWJ_PSOLIB=0 disables it. Anything unexpected (no library support, a stale file, a fault
// while loading) falls back to a normal create.

#include "gowj_pso_library.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/logging.h>

namespace {

// ---- persistent pipeline library ----------------------------------------------------
// The driver recompiles large pipelines (several hundred milliseconds each) on every run,
// and the renderer sometimes creates them on its own thread. An ID3D12PipelineLibrary kept
// on disk turns every create after the first-ever one into a load of about a millisecond.
// The key is a hash of the whole description (shader bytes plus fixed-function state,
// field by field so padding cannot leak in); the library validates the match itself, so a
// collision only costs a normal create.
struct Fnv {
  uint64_t a = 14695981039346656037ull, b = 0x9E3779B97F4A7C15ull;
  void Add(const void* p, size_t n) {
    const uint8_t* q = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
      a = (a ^ q[i]) * 1099511628211ull;
      b = (b + q[i] + 0x7F4A7C15ull) * 0xFF51AFD7ED558CCDull;
      b ^= b >> 29;
    }
  }
  void U32(uint32_t v) { Add(&v, 4); }
};

void HashBytecode(Fnv& h, const D3D12_SHADER_BYTECODE& bc) {
  h.U32(uint32_t(bc.BytecodeLength));
  if (bc.pShaderBytecode && bc.BytecodeLength) h.Add(bc.pShaderBytecode, bc.BytecodeLength);
}

bool HashDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, wchar_t* name /*[40]*/) {
  if (d.StreamOutput.NumEntries || d.CachedPSO.CachedBlobSizeInBytes) return false;
  Fnv h;
  h.U32(0x504C4231);  // layout version salt
  HashBytecode(h, d.VS);
  HashBytecode(h, d.PS);
  HashBytecode(h, d.DS);
  HashBytecode(h, d.HS);
  HashBytecode(h, d.GS);
  h.U32(d.BlendState.AlphaToCoverageEnable);
  h.U32(d.BlendState.IndependentBlendEnable);
  for (const auto& r : d.BlendState.RenderTarget) {
    h.U32(r.BlendEnable); h.U32(r.LogicOpEnable);
    h.U32(r.SrcBlend); h.U32(r.DestBlend); h.U32(r.BlendOp);
    h.U32(r.SrcBlendAlpha); h.U32(r.DestBlendAlpha); h.U32(r.BlendOpAlpha);
    h.U32(r.LogicOp); h.U32(r.RenderTargetWriteMask);
  }
  h.U32(d.SampleMask);
  const auto& ra = d.RasterizerState;
  h.U32(ra.FillMode); h.U32(ra.CullMode); h.U32(ra.FrontCounterClockwise);
  h.U32(uint32_t(ra.DepthBias)); h.Add(&ra.DepthBiasClamp, 4); h.Add(&ra.SlopeScaledDepthBias, 4);
  h.U32(ra.DepthClipEnable); h.U32(ra.MultisampleEnable); h.U32(ra.AntialiasedLineEnable);
  h.U32(ra.ForcedSampleCount); h.U32(ra.ConservativeRaster);
  const auto& ds = d.DepthStencilState;
  h.U32(ds.DepthEnable); h.U32(ds.DepthWriteMask); h.U32(ds.DepthFunc);
  h.U32(ds.StencilEnable); h.U32(ds.StencilReadMask); h.U32(ds.StencilWriteMask);
  for (const auto* f : {&ds.FrontFace, &ds.BackFace}) {
    h.U32(f->StencilFailOp); h.U32(f->StencilDepthFailOp); h.U32(f->StencilPassOp);
    h.U32(f->StencilFunc);
  }
  h.U32(d.InputLayout.NumElements);
  for (UINT i = 0; i < d.InputLayout.NumElements && d.InputLayout.pInputElementDescs; ++i) {
    const auto& e = d.InputLayout.pInputElementDescs[i];
    if (e.SemanticName) h.Add(e.SemanticName, strlen(e.SemanticName) + 1);
    h.U32(e.SemanticIndex); h.U32(e.Format); h.U32(e.InputSlot);
    h.U32(e.AlignedByteOffset); h.U32(e.InputSlotClass); h.U32(e.InstanceDataStepRate);
  }
  h.U32(d.IBStripCutValue); h.U32(d.PrimitiveTopologyType); h.U32(d.NumRenderTargets);
  for (auto f : d.RTVFormats) h.U32(f);
  h.U32(d.DSVFormat); h.U32(d.SampleDesc.Count); h.U32(d.SampleDesc.Quality);
  h.U32(d.NodeMask); h.U32(d.Flags);
  swprintf(name, 40, L"%016llx%016llx", (unsigned long long)h.a, (unsigned long long)h.b);
  return true;
}

ID3D12PipelineLibrary* g_lib = nullptr;
std::vector<char> g_lib_blob;  // must stay alive as long as g_lib (D3D12 requirement)
std::atomic<bool> g_lib_dirty{false};
std::atomic<uint64_t> g_lib_hits{0}, g_lib_misses{0}, g_lib_stored{0};
std::wstring g_lib_path;
bool g_lib_enabled = true;

std::wstring LibPath() {
  wchar_t base[MAX_PATH]{};
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) return L"";
  CreateDirectoryW((std::wstring(base) + L"\\GearsOfWarJudgmentPC").c_str(), nullptr);
  const std::wstring dir = std::wstring(base) + L"\\GearsOfWarJudgmentPC\\cache";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\pso_library.bin";
}

void LibWriter() {
  for (;;) {
    Sleep(30000);  // serializing is ~160 MB; keep it off SD-card / handheld hot paths
    if (!g_lib || !g_lib_dirty.exchange(false)) continue;
    const SIZE_T n = g_lib->GetSerializedSize();
    std::vector<char> buf(n);
    if (!n || FAILED(g_lib->Serialize(buf.data(), n))) continue;
    const std::wstring tmp = g_lib_path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) continue;
    DWORD w = 0;
    const bool ok = WriteFile(f, buf.data(), DWORD(n), &w, nullptr) && w == n;
    CloseHandle(f);
    if (ok) MoveFileExW(tmp.c_str(), g_lib_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    REXLOG_INFO("PSOLIB saved {} bytes ok={} hits={} misses={} stored={}", n, ok,
                g_lib_hits.load(), g_lib_misses.load(), g_lib_stored.load());
  }
}

// No C++ objects with destructors here: __try needs a plain frame.
HRESULT SafeLoad(ID3D12PipelineLibrary* lib, const wchar_t* name,
                 const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID r, void** out,
                 bool* faulted) {
  __try {
    return lib->LoadGraphicsPipeline(name, desc, r, out);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *faulted = true;
    return E_FAIL;
  }
}

ID3D12PipelineLibrary* GetLib(ID3D12Device* dev) {
  static std::once_flag once;
  std::call_once(once, [&] {
    ID3D12Device1* d1 = nullptr;
    if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&d1)))) {
      REXLOG_WARN("PSOLIB: device has no ID3D12Device1, library disabled");
      return;
    }
    D3D12_FEATURE_DATA_SHADER_CACHE sc{};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &sc, sizeof(sc))) ||
        !(sc.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY)) {
      REXLOG_WARN("PSOLIB: driver reports no pipeline-library support, library disabled");
      d1->Release();
      return;
    }
    g_lib_path = LibPath();
    std::vector<char>& blob = g_lib_blob;  // CreatePipelineLibrary keeps pointing into it
    HANDLE f = CreateFileW(g_lib_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER sz{};
      if (GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && sz.QuadPart < (1ll << 31)) {
        blob.resize(size_t(sz.QuadPart));
        DWORD r = 0;
        if (!ReadFile(f, blob.data(), DWORD(blob.size()), &r, nullptr) || r != blob.size()) {
          blob.clear();
        }
      }
      CloseHandle(f);
    }
    HRESULT hr = d1->CreatePipelineLibrary(blob.empty() ? nullptr : blob.data(), blob.size(),
                                           IID_PPV_ARGS(&g_lib));
    const bool loaded = SUCCEEDED(hr) && !blob.empty();
    if (FAILED(hr) && !blob.empty()) {  // stale blob (driver/adapter change): start empty
      REXLOG_WARN("PSOLIB: stored library rejected hr={:08x}, starting empty",
                  static_cast<uint32_t>(hr));
      hr = d1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&g_lib));
    }
    d1->Release();
    REXLOG_INFO("PSOLIB ready hr={:08x} loaded_existing={} file_bytes={}",
                static_cast<uint32_t>(hr), loaded, blob.size());
    if (SUCCEEDED(hr)) {
      std::thread(LibWriter).detach();
    } else {
      g_lib = nullptr;
    }
  });
  return g_lib;
}


using PsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,
                                          REFIID, void**);
PsoFn g_pso;

HRESULT STDMETHODCALLTYPE HookPso(ID3D12Device* d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                                  REFIID r, void** out) {
  HRESULT hr = E_FAIL;
  bool from_lib = false;
  wchar_t name[40];
  ID3D12PipelineLibrary* lib =
      (g_lib_enabled && desc && out && r == __uuidof(ID3D12PipelineState)) ? GetLib(d) : nullptr;
  const bool keyed = lib && HashDesc(*desc, name);
  if (keyed) {
    bool faulted = false;
    hr = SafeLoad(lib, name, desc, r, out, &faulted);
    if (faulted) {
      g_lib_enabled = false;  // never touch the library again this session
      REXLOG_WARN("PSOLIB: LoadGraphicsPipeline faulted, library disabled for this session");
    }
    from_lib = SUCCEEDED(hr);
    (from_lib ? g_lib_hits : g_lib_misses).fetch_add(1, std::memory_order_relaxed);
  }
  if (!from_lib) {
    hr = g_pso(d, desc, r, out);
    if (keyed && SUCCEEDED(hr) && *out &&
        SUCCEEDED(lib->StorePipeline(name, static_cast<ID3D12PipelineState*>(*out)))) {
      g_lib_stored.fetch_add(1, std::memory_order_relaxed);
      g_lib_dirty = true;
    }
  }
  return hr;
}

bool PatchSlot(void** vtable, size_t index, void* hook, void** orig) {
  DWORD old = 0;
  if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
  *orig = vtable[index];
  vtable[index] = hook;
  VirtualProtect(&vtable[index], sizeof(void*), old, &old);
  return true;
}

}  // namespace

namespace rex::glue {

void InstallPsoLibrary() {
  static bool done = false;
  if (done) return;
  done = true;
  char buf[8]{};
  if (GetEnvironmentVariableA("GOWJ_PSOLIB", buf, sizeof(buf)) && buf[0] == '0') {
    g_lib_enabled = false;
    return;
  }
  HMODULE d3d12 = LoadLibraryA("d3d12.dll");
  auto create = d3d12 ? reinterpret_cast<HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID,
                                                          void**)>(
                            GetProcAddress(d3d12, "D3D12CreateDevice"))
                      : nullptr;
  ID3D12Device* device = nullptr;
  if (!create || FAILED(create(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    REXLOG_WARN("PSOLIB: throwaway device failed, library not installed");
    return;
  }
  // The device vtable is shared by every device of this implementation, so patching it through
  // a throwaway device also covers the one the runtime creates. Slot 10 is
  // CreateGraphicsPipelineState.
  void** vt = *reinterpret_cast<void***>(device);
  const bool ok = PatchSlot(vt, 10, reinterpret_cast<void*>(&HookPso),
                            reinterpret_cast<void**>(&g_pso));
  device->Release();
  REXLOG_INFO("PSOLIB hook installed={}", ok);
}

}  // namespace rex::glue
