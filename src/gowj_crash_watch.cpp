#include "gowj_crash_watch.h"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <rex/ppc/func.h>

namespace {

// Written with plain Win32 file handles, never through the logger: the thread
// that dies in a guest AV frequently owns the logging mutex (measured
// 2026-09-24 09:25, where boot_play1.log stops exactly at the last line before
// the exit code), so an REXLOG call from here can deadlock and lose the report.
constexpr char kLogName[] = "gowj_crash.log";

void AppendLog(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  // Next to the program the player runs (GOWJ_HOME, set by the single-file launcher), else
  // next to this exe.
  char path[MAX_PATH];
  const DWORD room = DWORD(sizeof(path) - sizeof(kLogName) - 1);
  DWORD n = GetEnvironmentVariableA("GOWJ_HOME", path, room);
  if (n && n < room) {
    if (path[n - 1] != '\\') {
      path[n++] = '\\';
    }
    memcpy(path + n, kLogName, sizeof(kLogName));
  } else {
    n = GetModuleFileNameA(nullptr, path, sizeof(path) - sizeof(kLogName));
    if (!n) {
      return;
    }
    char* slash = strrchr(path, '\\');
    if (!slash) {
      return;
    }
    memcpy(slash + 1, kLogName, sizeof(kLogName));
  }

  HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return;
  }
  SetFilePointer(h, 0, nullptr, FILE_END);
  DWORD written;
  WriteFile(h, buf, static_cast<DWORD>(strlen(buf)), &written, nullptr);
  CloseHandle(h);
}

uintptr_t g_image_base = 0;
uintptr_t g_image_end = 0;

// PPCFuncMappings is codegen's own { guest address, host entry point } table,
// terminated by { 0, nullptr } and sorted by GUEST address. Host addresses are
// not sorted, so attributing a RIP means a linear scan - fine for a crash path,
// and it is the only mapping available: the shipped exe carries no symbol table.
struct GuestFrame {
  uint32_t guest = 0;
  uintptr_t host = 0;
};

bool FindGuest(uintptr_t addr, GuestFrame* out) {
  if (addr < g_image_base || addr >= g_image_end) {
    return false;
  }
  GuestFrame best;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
    uintptr_t host = reinterpret_cast<uintptr_t>(m->host);
    if (host <= addr && (!best.host || host > best.host)) {
      best.guest = static_cast<uint32_t>(m->guest);
      best.host = host;
    }
  }
  if (!best.host) {
    return false;
  }
  *out = best;
  return true;
}

bool Readable(const void* p, size_t len) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
    return false;
  }
  DWORD prot = mbi.Protect & 0xFF;
  if (prot == PAGE_NOACCESS || prot == PAGE_GUARD) {
    return false;
  }
  return reinterpret_cast<uintptr_t>(p) + len <=
         reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
  const CONTEXT* ctx = ep->ContextRecord;
  AppendLog("=== gowj crash watch: code=%08lx addr=%p rip=%p rsp=%p ===\n",
            rec->ExceptionCode, rec->ExceptionAddress,
            reinterpret_cast<void*>(ctx->Rip), reinterpret_cast<void*>(ctx->Rsp));

  GuestFrame f;
  if (FindGuest(reinterpret_cast<uintptr_t>(ctx->Rip), &f)) {
    AppendLog("  RIP in guest sub_%08X (host %p + 0x%llX)\n", f.guest,
              reinterpret_cast<void*>(f.host),
              static_cast<unsigned long long>(ctx->Rip - f.host));
  } else {
    // Name the owning module: "not inside any recompiled function" alone cannot
    // tell a rexruntime guest-memory helper (bad guest pointer) from ntdll
    // memcpy or a GPU driver, and those imply completely different fixes.
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(ctx->Rip), &mod)) {
      char name[MAX_PATH] = {0};
      GetModuleFileNameA(mod, name, sizeof(name) - 1);
      AppendLog("  RIP %p in module %s (+0x%llX)\n",
                reinterpret_cast<void*>(ctx->Rip), name,
                static_cast<unsigned long long>(
                    ctx->Rip - reinterpret_cast<uintptr_t>(mod)));
    } else {
      AppendLog("  RIP %p not inside any recompiled function or loaded module\n",
                reinterpret_cast<void*>(ctx->Rip));
    }
  }

  // Translate-time return addresses: the recompiled caller's entry point is
  // still on the host stack, so scanning it reconstructs the guest call chain
  // that led here - which is what distinguishes "this function is broken" from
  // "its caller passed it garbage".
  size_t reported = 0;
  uint32_t last_guest = 0;
  for (uintptr_t sp = ctx->Rsp; sp < ctx->Rsp + 0x20000; sp += 8) {
    if (!Readable(reinterpret_cast<const void*>(sp), 8)) {
      break;
    }
    uintptr_t value = *reinterpret_cast<const uintptr_t*>(sp);
    GuestFrame frame;
    if (!FindGuest(value, &frame) || frame.host == ctx->Rip) {
      continue;
    }
    if (frame.guest != last_guest) {
      last_guest = frame.guest;
      AppendLog("  chain[%zu] sub_%08X (host %p + 0x%llX)\n", reported,
                frame.guest, reinterpret_cast<void*>(frame.host),
                static_cast<unsigned long long>(value - frame.host));
      if (++reported >= 24) {
        break;
      }
    }
  }
  AppendLog("=== end of crash report ===\n");
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

namespace rex::glue {

void InstallCrashWatch() {
  HMODULE self = GetModuleHandleA(nullptr);
  g_image_base = reinterpret_cast<uintptr_t>(self);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
      reinterpret_cast<uint8_t*>(self) +
      reinterpret_cast<IMAGE_DOS_HEADER*>(self)->e_lfanew);
  g_image_end = g_image_base + nt->OptionalHeader.SizeOfImage;
  SetUnhandledExceptionFilter(UnhandledFilter);
  AppendLog("crash watch installed, image [%p - %p)\n",
            reinterpret_cast<void*>(g_image_base),
            reinterpret_cast<void*>(g_image_end));
}

}  // namespace rex::glue
