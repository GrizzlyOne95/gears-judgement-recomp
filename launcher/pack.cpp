// Build tool: appends the game and its runtime files to the launcher stub, producing the
// single-file GearsOfWarJudgment.exe (see stub.cpp for the other half).
//
//   gowj_pack <output.exe> <stub.exe> <name>=<path> [<name>=<path> ...]
//
// Each file is compressed in 8 MB blocks with the Windows LZMS compressor, so the stub can
// unpack it with the matching system decompressor and no third-party code.

#include <windows.h>

#include <compressapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kMagic[8] = {'G', 'O', 'W', 'J', 'P', 'A', 'K', '1'};
constexpr uint32_t kBlock = 8u << 20;

#pragma pack(push, 1)
struct Footer {
  char magic[8];
  uint64_t payload_offset;
  uint64_t payload_size;
  uint64_t id;
};
struct EntryHeader {
  uint16_t name_len;
  uint64_t raw_size;
  uint32_t block_count;
};
struct BlockHeader {
  uint32_t raw_size;
  uint32_t comp_size;
};
#pragma pack(pop)

bool ReadFileAll(const std::wstring& path, std::vector<uint8_t>& out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  GetFileSizeEx(f, &sz);
  out.resize(size_t(sz.QuadPart));
  size_t done = 0;
  while (done < out.size()) {
    DWORD got = 0;
    const DWORD want = DWORD((std::min)(out.size() - done, size_t(1) << 30));
    if (!ReadFile(f, out.data() + done, want, &got, nullptr) || !got) break;
    done += got;
  }
  CloseHandle(f);
  return done == out.size();
}

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: gowj_pack <output.exe> <stub.exe> <name>=<path> ...\n");
    return 2;
  }
  std::vector<uint8_t> stub;
  if (!ReadFileAll(argv[2], stub)) {
    fprintf(stderr, "cannot read stub %ls\n", argv[2]);
    return 1;
  }
  COMPRESSOR_HANDLE comp = nullptr;
  if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &comp)) {
    fprintf(stderr, "CreateCompressor failed (%lu)\n", GetLastError());
    return 1;
  }

  std::vector<uint8_t> payload;
  uint64_t hash = 1469598103934665603ull;
  auto put = [&](const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    payload.insert(payload.end(), b, b + n);
  };

  uint64_t raw_total = 0;
  for (int i = 3; i < argc; ++i) {
    const std::wstring arg = argv[i];
    const size_t eq = arg.find(L'=');
    if (eq == std::wstring::npos) {
      fprintf(stderr, "bad entry %ls (expected name=path)\n", argv[i]);
      return 2;
    }
    const std::wstring name = arg.substr(0, eq);
    std::vector<uint8_t> data;
    if (!ReadFileAll(arg.substr(eq + 1), data)) {
      fprintf(stderr, "cannot read %ls\n", arg.substr(eq + 1).c_str());
      return 1;
    }
    const std::string n8 = Utf8(name);
    EntryHeader eh{uint16_t(n8.size()), data.size(), uint32_t((data.size() + kBlock - 1) / kBlock)};
    put(&eh, sizeof(eh));
    put(n8.data(), n8.size());
    std::vector<uint8_t> out;
    for (size_t off = 0; off < data.size(); off += kBlock) {
      const uint32_t n = uint32_t((std::min)(size_t(kBlock), data.size() - off));
      SIZE_T need = 0;
      Compress(comp, data.data() + off, n, nullptr, 0, &need);
      out.resize(need);
      SIZE_T got = 0;
      if (!Compress(comp, data.data() + off, n, out.data(), need, &got)) {
        fprintf(stderr, "compress failed for %ls (%lu)\n", name.c_str(), GetLastError());
        return 1;
      }
      BlockHeader bh{n, uint32_t(got)};
      put(&bh, sizeof(bh));
      put(out.data(), got);
    }
    raw_total += data.size();
    printf("  %-28ls %10zu bytes\n", name.c_str(), data.size());
  }
  CloseCompressor(comp);

  for (uint8_t b : payload) {
    hash ^= b;
    hash *= 1099511628211ull;
  }
  Footer ft{};
  memcpy(ft.magic, kMagic, 8);
  ft.payload_offset = stub.size();
  ft.payload_size = payload.size();
  ft.id = hash;

  HANDLE o = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (o == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "cannot write %ls\n", argv[1]);
    return 1;
  }
  auto write_all = [&](const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    while (n) {
      DWORD w = 0;
      const DWORD chunk = DWORD((std::min)(n, size_t(1) << 30));
      if (!WriteFile(o, b, chunk, &w, nullptr) || !w) return false;
      b += w;
      n -= w;
    }
    return true;
  };
  if (!write_all(stub.data(), stub.size()) || !write_all(payload.data(), payload.size()) ||
      !write_all(&ft, sizeof(ft))) {
    fprintf(stderr, "write failed\n");
    return 1;
  }
  CloseHandle(o);
  printf("packed %llu bytes into %llu (%.0f%%), id %016llx\n", (unsigned long long)raw_total,
         (unsigned long long)payload.size(), 100.0 * double(payload.size()) / double(raw_total),
         (unsigned long long)hash);
  return 0;
}
