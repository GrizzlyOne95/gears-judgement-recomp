// Gears of War: Judgment (PC) - single-file launcher.
//
// GearsOfWarJudgment.exe is this program with the real game and its runtime files appended to
// it (see pack.cpp). On the first run it unpacks them into
//   %LOCALAPPDATA%\GearsOfWarJudgmentPC\bin\<id>\
// and starts the game from there; later runs find the files already unpacked and start at once.
// The game's settings, saves and logs stay next to THIS file (GOWJ_HOME points the game at it).
//
// Only Windows components are used (kernel32, user32, cabinet.dll for decompression), and the C
// runtime is linked statically, so nothing has to be installed first.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <compressapi.h>

namespace {

constexpr char kMagic[8] = {'G', 'O', 'W', 'J', 'P', 'A', 'K', '1'};

#pragma pack(push, 1)
struct Footer {
  char magic[8];
  uint64_t payload_offset;  // start of the payload inside this file
  uint64_t payload_size;
  uint64_t id;  // hash of the payload; names the unpack folder
};
struct EntryHeader {
  uint16_t name_len;  // bytes, UTF-8, follows the header
  uint64_t raw_size;
  uint32_t block_count;
};
struct BlockHeader {
  uint32_t raw_size;
  uint32_t comp_size;
};
#pragma pack(pop)

void Fail(const wchar_t* text) {
  MessageBoxW(nullptr, text, L"Gears of War: Judgment", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

std::wstring Widen(const char* s, size_t n) {
  if (!n) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, s, int(n), nullptr, 0);
  std::wstring w(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, int(n), w.data(), len);
  return w;
}

std::wstring Dir(const std::wstring& path) {
  size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"." : path.substr(0, i);
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void MakeDirs(const std::wstring& path) {
  for (size_t i = 3; i < path.size(); ++i) {
    if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
  }
  CreateDirectoryW(path.c_str(), nullptr);
}

void RemoveTree(const std::wstring& dir) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
      const std::wstring p = dir + L"\\" + fd.cFileName;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
      else DeleteFileW(p.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

// A small window shown only while unpacking, so the first start does not look frozen.
HWND g_splash = nullptr;
LRESULT CALLBACK SplashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(16, 18, 22));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(232, 234, 237));
    RECT a{24, 22, rc.right - 24, 56};
    DrawTextW(dc, L"GEARS OF WAR: JUDGMENT", -1, &a, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, old);
    DeleteObject(font);
    HFONT small_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, 0, L"Segoe UI");
    old = SelectObject(dc, small_font);
    SetTextColor(dc, RGB(139, 147, 161));
    RECT b{24, 62, rc.right - 24, 100};
    DrawTextW(dc, L"Preparing the game files. This only happens the first time.", -1, &b,
              DT_LEFT | DT_WORDBREAK);
    SelectObject(dc, old);
    DeleteObject(small_font);
    EndPaint(h, &ps);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void ShowSplash() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = SplashProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"gowj_splash";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  RegisterClassW(&wc);
  const int w = 420, h = 120;
  g_splash = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Gears of War: Judgment",
                             WS_POPUP | WS_BORDER, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                             (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr,
                             wc.hInstance, nullptr);
  ShowWindow(g_splash, SW_SHOW);
  UpdateWindow(g_splash);
}

void Pump() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

bool ReadExact(HANDLE f, void* dst, DWORD n) {
  DWORD got = 0;
  return ReadFile(f, dst, n, &got, nullptr) && got == n;
}

bool WriteExact(HANDLE f, const void* src, DWORD n) {
  DWORD put = 0;
  return WriteFile(f, src, n, &put, nullptr) && put == n;
}

// Unpacks every file of the payload into `dest`.
bool Unpack(HANDLE self, const Footer& ft, const std::wstring& dest) {
  LARGE_INTEGER pos;
  pos.QuadPart = LONGLONG(ft.payload_offset);
  SetFilePointerEx(self, pos, nullptr, FILE_BEGIN);
  DECOMPRESSOR_HANDLE dec = nullptr;
  if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &dec)) return false;
  uint64_t remaining = ft.payload_size;
  std::vector<uint8_t> comp, raw;
  bool ok = true;
  while (ok && remaining >= sizeof(EntryHeader)) {
    EntryHeader eh;
    if (!ReadExact(self, &eh, sizeof(eh))) { ok = false; break; }
    std::string name(eh.name_len, '\0');
    if (eh.name_len && !ReadExact(self, name.data(), eh.name_len)) { ok = false; break; }
    remaining -= sizeof(eh) + eh.name_len;
    std::wstring out_path = dest + L"\\" + Widen(name.data(), name.size());
    for (auto& ch : out_path) {
      if (ch == L'/') ch = L'\\';
    }
    MakeDirs(Dir(out_path));
    HANDLE out = CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) { ok = false; break; }
    for (uint32_t b = 0; b < eh.block_count && ok; ++b) {
      BlockHeader bh;
      if (!ReadExact(self, &bh, sizeof(bh))) { ok = false; break; }
      comp.resize(bh.comp_size);
      raw.resize(bh.raw_size);
      if (!ReadExact(self, comp.data(), bh.comp_size)) { ok = false; break; }
      SIZE_T produced = 0;
      if (!Decompress(dec, comp.data(), bh.comp_size, raw.data(), bh.raw_size, &produced) ||
          produced != bh.raw_size || !WriteExact(out, raw.data(), bh.raw_size)) {
        ok = false;
        break;
      }
      remaining -= sizeof(bh) + bh.comp_size;
      Pump();
    }
    CloseHandle(out);
  }
  CloseDecompressor(dec);
  return ok;
}

std::wstring Timestamp() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t buf[40];
  wsprintfW(buf, L"%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  return buf;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t selfbuf[MAX_PATH * 2];
  GetModuleFileNameW(nullptr, selfbuf, MAX_PATH * 2);
  const std::wstring self = selfbuf;
  const std::wstring home = Dir(self);

  HANDLE f = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) Fail(L"Could not read the program file.");
  LARGE_INTEGER size;
  GetFileSizeEx(f, &size);
  Footer ft{};
  LARGE_INTEGER pos;
  pos.QuadPart = size.QuadPart - LONGLONG(sizeof(Footer));
  SetFilePointerEx(f, pos, nullptr, FILE_BEGIN);
  if (size.QuadPart < LONGLONG(sizeof(Footer)) || !ReadExact(f, &ft, sizeof(ft)) ||
      memcmp(ft.magic, kMagic, 8) != 0) {
    Fail(L"This file does not contain the game (it is the bare launcher stub).");
  }

  wchar_t local[MAX_PATH];
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) Fail(L"LOCALAPPDATA is not set.");
  wchar_t idbuf[24];
  wsprintfW(idbuf, L"%08x%08x", unsigned(ft.id >> 32), unsigned(ft.id & 0xFFFFFFFF));
  const std::wstring bin = std::wstring(local) + L"\\GearsOfWarJudgmentPC\\bin";
  const std::wstring dest = bin + L"\\" + idbuf;
  const std::wstring exe = dest + L"\\GearsOfWarJudgment.exe";

  if (!Exists(dest + L"\\.complete")) {
    ShowSplash();
    Pump();
    const std::wstring tmp = dest + L".part";
    RemoveTree(tmp);
    MakeDirs(tmp);
    if (!Unpack(f, ft, tmp)) {
      RemoveTree(tmp);
      if (g_splash) DestroyWindow(g_splash);
      Fail(L"Unpacking the game files failed. Check that there is free disk space and that your "
           L"antivirus is not blocking this program, then try again.");
    }
    HANDLE m = CreateFileW((tmp + L"\\.complete").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m != INVALID_HANDLE_VALUE) CloseHandle(m);
    RemoveTree(dest);
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), 0)) {
      if (g_splash) DestroyWindow(g_splash);
      Fail(L"Could not finish unpacking the game files.");
    }
    // Older unpacked versions are no longer needed.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((bin + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !wcscmp(fd.cFileName, idbuf)) continue;
        RemoveTree(bin + L"\\" + fd.cFileName);
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
    if (g_splash) DestroyWindow(g_splash);
  }
  CloseHandle(f);

  // Command line: the game's arguments are this program's arguments. A log file next to this
  // program is added unless the caller chose one.
  std::wstring cmd = L"\"" + exe + L"\"";
  const wchar_t* args = GetCommandLineW();
  if (*args == L'"') { ++args; while (*args && *args != L'"') ++args; if (*args) ++args; }
  else { while (*args && *args != L' ') ++args; }
  const std::wstring rest = args;
  cmd += rest;
  if (rest.find(L"--log_file") == std::wstring::npos) {
    CreateDirectoryW((home + L"\\logs").c_str(), nullptr);
    cmd += L" \"--log_file=" + home + L"\\logs\\gowj_" + Timestamp() + L".log\"";
  }

  SetEnvironmentVariableW(L"GOWJ_HOME", home.c_str());
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
  cmdbuf.push_back(L'\0');
  if (!CreateProcessW(exe.c_str(), cmdbuf.data(), nullptr, nullptr, FALSE, 0, nullptr, dest.c_str(), &si, &pi)) {
    Fail(L"Could not start the game.");
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}
