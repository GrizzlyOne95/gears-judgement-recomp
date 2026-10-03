// Gears of War: Judgment (PC) - settings launcher.
//
// A small native window for the settings people change most: quality level, internal
// resolution, texture filtering, anti-aliasing and upscaling, frame limit, mouse sensitivity.
// It edits config\gowj.toml next to this program (the same file the game and its F4 menu use),
// keeps every other line of that file as it is, and can start the game.
//
// Only components that ship with Windows are used (user32, gdi32, GDI+, dwmapi, shell32) and the
// C runtime is linked statically, so there is nothing to install.
//
//   Graphics Settings.exe --apply key=value [key=value ...]    writes settings without a window

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "default_config.h"

using namespace Gdiplus;

namespace {

// ------------------------------------------------------------------------------------------
// config file model: a list of lines, edited in place
// ------------------------------------------------------------------------------------------
constexpr const char* kBlockBegin = "# >>> gowj auto quality";
constexpr const char* kBlockEnd = "# <<< gowj auto quality";

std::wstring g_dir;          // folder of this program (and of the game exe)
std::wstring g_cfg_path;     // <dir>\config\gowj.toml
std::vector<std::string> g_lines;

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos) j = text.size();
    std::string l = text.substr(i, j - i);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    out.push_back(l);
    i = j + 1;
  }
  if (!out.empty() && out.back().empty()) out.pop_back();
  return out;
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  GetFileSizeEx(f, &sz);
  out.resize(size_t(sz.QuadPart));
  DWORD got = 0;
  const bool ok = out.empty() || (ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size());
  CloseHandle(f);
  return ok;
}

bool WriteTextFile(const std::wstring& path, const std::string& text) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(f);
  return ok;
}

void LoadConfig() {
  std::string text;
  if (!ReadTextFile(g_cfg_path, text)) text = kDefaultConfig;
  g_lines = SplitLines(text);
}

bool SaveConfig() {
  CreateDirectoryW((g_dir + L"\\config").c_str(), nullptr);
  std::string text;
  for (const auto& l : g_lines) {
    text += l;
    text += '\n';
  }
  return WriteTextFile(g_cfg_path, text);
}

// index of the active "key = value" line outside the managed block, or -1
int FindKey(const std::string& key) {
  bool in_block = false;
  for (size_t i = 0; i < g_lines.size(); ++i) {
    const std::string& l = g_lines[i];
    if (l.rfind(kBlockBegin, 0) == 0) { in_block = true; continue; }
    if (in_block) { if (l.rfind(kBlockEnd, 0) == 0) in_block = false; continue; }
    size_t b = l.find_first_not_of(" \t");
    if (b == std::string::npos || l[b] == '#') continue;
    if (l.compare(b, key.size(), key) == 0) {
      size_t e = b + key.size();
      while (e < l.size() && (l[e] == ' ' || l[e] == '\t')) ++e;
      if (e < l.size() && l[e] == '=') return int(i);
    }
  }
  return -1;
}

bool GetValue(const std::string& key, std::string* value) {
  const int i = FindKey(key);
  if (i < 0) return false;
  std::string v = g_lines[i].substr(g_lines[i].find('=') + 1);
  size_t c = v.find('#');
  if (c != std::string::npos) v = v.substr(0, c);
  size_t b = v.find_first_not_of(" \t\"");
  size_t e = v.find_last_not_of(" \t\"");
  *value = b == std::string::npos ? "" : v.substr(b, e - b + 1);
  return true;
}

void SetValue(const std::string& key, const std::string& value) {
  const std::string line = key + " = " + value;
  const int i = FindKey(key);
  if (i >= 0) { g_lines[i] = line; return; }
  for (size_t k = 0; k < g_lines.size(); ++k) {
    if (g_lines[k].rfind(kBlockBegin, 0) == 0) { g_lines.insert(g_lines.begin() + k, line); return; }
  }
  g_lines.push_back(line);
}

void RemoveKey(const std::string& key) {
  const int i = FindKey(key);
  if (i >= 0) g_lines.erase(g_lines.begin() + i);
}

std::string Num(double v, int decimals) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.push_back('0');
  }
  return s;
}

// ------------------------------------------------------------------------------------------
// the settings
// ------------------------------------------------------------------------------------------
enum Id {
  kQuality, kResolution, kFiltering, kFullscreen,
  kSmaa, kFsr, kSharpness, kGrain, kBlur,
  kLimit, kVrr,
  kSens, kInvert,
  kCount
};
enum class Kind { Segmented, Toggle, Slider };

struct Ctl {
  Id id;
  Kind kind;
  const wchar_t* label;
  const wchar_t* hint;
  std::vector<std::wstring> items;
  int sel = -1;
  bool on = false;
  double val = 0, vmin = 0, vmax = 1, step = 0.1;
  const wchar_t* unit = L"";
  std::wstring last;  // what the game actually applied on its last start (from cvars.txt)
  bool dirty = false;
  RectF row{}, ctl{};
};

struct Card {
  const wchar_t* title;
  std::vector<int> ctls;
  int column;
  const wchar_t* note = nullptr;  // short hint drawn at the bottom of the card
  RectF rect{};
};

Ctl g_c[kCount];
std::vector<Card> g_cards;

void InitControls() {
  g_c[kQuality] = {kQuality, Kind::Segmented, L"Quality level",
                   L"Picking a level sets resolution, texture filtering and SMAA below to match it. Auto picks from your graphics card and lowers itself if 60 fps is not held. Low: 720p, no SMAA, 4x. Medium: 720p, SMAA, 8x. High: 1440p, SMAA, 16x."};
  g_c[kQuality].items = {L"Auto", L"Low", L"Medium", L"High"};
  g_c[kResolution] = {kResolution, Kind::Segmented, L"Internal resolution",
                      L"The resolution the game renders at before it is scaled to your screen. Auto follows the quality level. 2x is sharper and needs a much faster graphics card."};
  g_c[kResolution].items = {L"Auto", L"1x  720p", L"2x  1440p"};
  g_c[kFiltering] = {kFiltering, Kind::Segmented, L"Texture filtering",
                     L"Keeps ground and walls sharp at a slant. Auto follows the quality level."};
  g_c[kFiltering].items = {L"Auto", L"4x", L"8x", L"16x"};
  g_c[kFullscreen] = {kFullscreen, Kind::Toggle, L"Fullscreen", L"Borderless fullscreen. Turn off to play in a window."};
  g_c[kSmaa] = {kSmaa, Kind::Segmented, L"Anti-aliasing (SMAA)",
                L"Smooths jagged edges in the final image. Auto turns it on except on the Low level."};
  g_c[kSmaa].items = {L"Auto", L"On", L"Off"};
  g_c[kFsr] = {kFsr, Kind::Toggle, L"FSR upscaling", L"AMD FidelityFX Super Resolution 1.0 scales the image to your screen and sharpens it. Off uses a plain stretch."};
  g_c[kSharpness] = {kSharpness, Kind::Slider, L"FSR sharpness", L"How much FSR sharpens the picture. Lower is sharper, higher is softer."};
  g_c[kSharpness].vmin = 0; g_c[kSharpness].vmax = 2; g_c[kSharpness].step = 0.1;
  g_c[kGrain] = {kGrain, Kind::Toggle, L"Film grain", L"The game's animated film grain. Off gives a calmer, sharper image."};
  g_c[kBlur] = {kBlur, Kind::Toggle, L"Motion blur", L"The game's motion blur. Off is the default."};
  g_c[kLimit] = {kLimit, Kind::Segmented, L"Frame limit",
                 L"A steady limit looks smoother than a fast but uneven frame rate. Unlocked also turns vsync off, so frames are not held to the display refresh. In the game, F8 switches between this limit and unlocked."};
  g_c[kLimit].items = {L"30", L"60", L"90", L"120", L"Unlocked"};
  g_c[kVrr] = {kVrr, Kind::Toggle, L"Variable refresh rate", L"For G-Sync and FreeSync monitors. Turn on when the frame limit is Unlocked."};
  g_c[kSens] = {kSens, Kind::Slider, L"Mouse sensitivity", L"How far the camera turns per mouse movement. In the game, F11 and F12 lower and raise it."};
  g_c[kSens].vmin = 25; g_c[kSens].vmax = 300; g_c[kSens].step = 5; g_c[kSens].unit = L"%";
  g_c[kInvert] = {kInvert, Kind::Toggle, L"Invert vertical look", L"Moving the mouse up looks down."};

  g_cards = {
      {L"QUALITY", {kQuality, kResolution, kFiltering, kFullscreen}, 0},
      {L"IMAGE", {kSmaa, kFsr, kSharpness, kGrain, kBlur}, 1},
      {L"FRAME RATE", {kLimit, kVrr}, 0, L"In the game, F8 lifts and restores this limit, and F9 shows an FPS counter. Choose Unlocked here to also turn vsync off."},
      {L"MOUSE", {kSens, kInvert}, 1, L"In the game, F11 and F12 lower and raise the sensitivity, and Insert releases the mouse."},
  };
}

// config -> controls
void LoadControls() {
  std::string v;
  auto& q = g_c[kQuality];
  q.sel = 0;
  if (GetValue("gowj_quality", &v)) q.sel = v == "low" ? 1 : v == "medium" ? 2 : v == "high" ? 3 : 0;

  auto& r = g_c[kResolution];
  r.sel = 0;
  if (GetValue("draw_resolution_scale_x", &v)) r.sel = v == "1" ? 1 : v == "2" ? 2 : -1;

  auto& f = g_c[kFiltering];
  f.sel = 0;
  if (GetValue("anisotropic_override", &v)) f.sel = v == "3" ? 1 : v == "4" ? 2 : v == "5" ? 3 : -1;

  g_c[kFullscreen].on = !GetValue("fullscreen", &v) || v != "false";

  auto& s = g_c[kSmaa];
  s.sel = 0;
  if (GetValue("gowj_smaa", &v)) s.sel = v == "true" ? 1 : v == "false" ? 2 : 0;

  g_c[kFsr].on = !GetValue("gowj_fsr", &v) || v != "false";
  g_c[kSharpness].val = GetValue("gowj_fsr_sharpness", &v) ? atof(v.c_str()) : 0.5;
  g_c[kGrain].on = GetValue("gowj_film_grain", &v) && v == "true";
  g_c[kBlur].on = GetValue("gowj_motion_blur", &v) && v == "true";

  auto& l = g_c[kLimit];
  int hz = 60;
  if (GetValue("gowj_pace_hz", &v)) hz = atoi(v.c_str());
  l.sel = hz == 30 ? 0 : hz == 60 ? 1 : hz == 90 ? 2 : hz == 120 ? 3 : hz == 0 ? 4 : -1;
  g_c[kVrr].on = GetValue("d3d12_allow_variable_refresh_rate_and_tearing", &v) && v == "true";

  const double sens = GetValue("mnk_raw_look_scale", &v) ? atof(v.c_str()) : 0.003;
  g_c[kSens].val = std::clamp(std::round(sens / 0.003 * 100.0 / 5.0) * 5.0, 25.0, 300.0);
  g_c[kInvert].on = GetValue("mnk_invert_y", &v) && v == "true";
  for (auto& c : g_c) c.dirty = false;
}

// controls -> config (only what was changed)
void ApplyControls() {
  auto b = [](bool on) { return std::string(on ? "true" : "false"); };
  for (auto& c : g_c) {
    if (!c.dirty) continue;
    switch (c.id) {
      case kQuality: {
        static const char* n[] = {"auto", "low", "medium", "high"};
        if (c.sel >= 0) SetValue("gowj_quality", std::string("\"") + n[c.sel] + "\"");
      } break;
      case kResolution:
        if (c.sel == 0) { RemoveKey("draw_resolution_scale_x"); RemoveKey("draw_resolution_scale_y"); }
        else if (c.sel > 0) {
          SetValue("draw_resolution_scale_x", std::to_string(c.sel));
          SetValue("draw_resolution_scale_y", std::to_string(c.sel));
        }
        break;
      case kFiltering:
        if (c.sel == 0) RemoveKey("anisotropic_override");
        else if (c.sel > 0) SetValue("anisotropic_override", std::to_string(c.sel + 2));
        break;
      case kFullscreen: SetValue("fullscreen", b(c.on)); break;
      case kSmaa:
        if (c.sel == 0) RemoveKey("gowj_smaa");
        else if (c.sel > 0) SetValue("gowj_smaa", b(c.sel == 1));
        break;
      case kFsr: SetValue("gowj_fsr", b(c.on)); break;
      case kSharpness: SetValue("gowj_fsr_sharpness", Num(c.val, 1)); break;
      case kGrain: SetValue("gowj_film_grain", b(c.on)); break;
      case kBlur: SetValue("gowj_motion_blur", b(c.on)); break;
      case kLimit: {
        static const int hz[] = {30, 60, 90, 120, 0};
        if (c.sel >= 0) SetValue("gowj_pace_hz", std::to_string(hz[c.sel]));
        // Unlimited also has to leave vsync, which otherwise gates presents at half the refresh rate.
        if (c.sel >= 0) SetValue("vsync", hz[c.sel] == 0 ? "false" : "true");
      } break;
      case kVrr: SetValue("d3d12_allow_variable_refresh_rate_and_tearing", b(c.on)); break;
      case kSens: SetValue("mnk_raw_look_scale", Num(0.003 * c.val / 100.0, 5)); break;
      case kInvert: SetValue("mnk_invert_y", b(c.on)); break;
      default: break;
    }
    c.dirty = false;
  }
}

// A quality level is a bundle: picking one also sets resolution, texture filtering and SMAA so the
// window shows exactly what will be applied (Auto clears them and lets the game choose).
void ApplyPreset(int level) {
  struct Preset { int res, filt, smaa; };
  static const Preset kPresets[4] = {{0, 0, 0}, {1, 1, 2}, {1, 2, 1}, {2, 3, 1}};
  if (level < 0 || level > 3) return;
  g_c[kQuality].sel = level;
  g_c[kQuality].dirty = true;
  const Preset& p = kPresets[level];
  g_c[kResolution].sel = p.res;
  g_c[kResolution].dirty = true;
  g_c[kFiltering].sel = p.filt;
  g_c[kFiltering].dirty = true;
  g_c[kSmaa].sel = p.smaa;
  g_c[kSmaa].dirty = true;
}

// config\cvars.txt is rewritten by the game on every start with the values it really applied
// (columns: category, name, type, source, default, current, ...). It is what the "last start"
// labels are read from, so the window never has to guess what Auto turned into.
void LoadLastStart() {
  std::string text;
  if (!ReadTextFile(g_dir + L"\\config\\cvars.txt", text)) return;
  auto field = [&](const std::string& line, int idx) {
    size_t start = 0;
    for (int i = 0; i < idx; ++i) {
      start = line.find(char(9), start);
      if (start == std::string::npos) return std::string();
      ++start;
    }
    size_t end = line.find(char(9), start);
    return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
  };
  std::string scale, aniso, smaa, hz;
  for (const auto& l : SplitLines(text)) {
    const std::string name = field(l, 1);
    if (name == "draw_resolution_scale_x") scale = field(l, 5);
    else if (name == "anisotropic_override") aniso = field(l, 5);
    else if (name == "gowj_smaa") smaa = field(l, 5);
    else if (name == "gowj_pace_hz") hz = field(l, 5);
  }
  if (scale == "1") g_c[kResolution].last = L"last start: 1x 720p";
  else if (scale == "2") g_c[kResolution].last = L"last start: 2x 1440p";
  else if (!scale.empty()) g_c[kResolution].last = L"last start: " + Widen(scale) + L"x";
  if (aniso == "3") g_c[kFiltering].last = L"last start: 4x";
  else if (aniso == "4") g_c[kFiltering].last = L"last start: 8x";
  else if (aniso == "5") g_c[kFiltering].last = L"last start: 16x";
  else if (aniso == "-1" || aniso == "0") g_c[kFiltering].last = L"last start: game default";
  if (smaa == "true") g_c[kSmaa].last = L"last start: on";
  else if (smaa == "false") g_c[kSmaa].last = L"last start: off";
  if (hz == "0") g_c[kLimit].last = L"last start: unlocked";
  else if (!hz.empty()) g_c[kLimit].last = L"last start: " + Widen(hz) + L" fps";
}

// ------------------------------------------------------------------------------------------
// look
// ------------------------------------------------------------------------------------------
const Color kBg(255, 16, 18, 22), kCard(255, 24, 27, 33), kCardLine(255, 38, 42, 51),
    kText(255, 232, 234, 237), kMuted(255, 139, 147, 161), kTrack(255, 33, 37, 45),
    kTrackHover(255, 45, 50, 60), kAccent(255, 224, 64, 38), kAccentHover(255, 240, 84, 58),
    kOff(255, 63, 69, 82), kWhite(255, 245, 246, 248), kOk(255, 90, 200, 120), kWarn(255, 240, 170, 60);

constexpr float kBaseW = 900.f, kMargin = 24.f, kGap = 18.f, kHeaderH = 92.f, kFooterH = 78.f;
constexpr float kPad = 22.f, kTitleH = 44.f;

HWND g_hwnd = nullptr;
float g_scale = 1.f;
int g_hover_ctl = -1, g_hover_seg = -1, g_hover_btn = -1, g_drag = -1, g_press_btn = -1;
bool g_changed = false, g_game_found = false;
std::wstring g_status;
Color g_status_color = kMuted;
RectF g_btn[3];  // reset, save, play
const wchar_t* kBtnText[3] = {L"Reset to defaults", L"Save", L"Play"};
float g_content_h = 0;

float RowHeight(const Ctl& c) { return c.kind == Kind::Toggle ? 46.f : 70.f; }

void Layout() {
  const float colw = (kBaseW - 2 * kMargin - kGap) / 2.f;
  float y[2] = {kHeaderH, kHeaderH};
  for (auto& card : g_cards) {
    float h = kTitleH + kPad * 0.4f;
    for (int i : card.ctls) h += RowHeight(g_c[i]);
    h += kPad * 0.6f;
    if (card.note) h += 46.f;
    card.rect = RectF(kMargin + card.column * (colw + kGap), y[card.column], colw, h);
    float cy = y[card.column] + kTitleH + kPad * 0.4f;
    for (int i : card.ctls) {
      Ctl& c = g_c[i];
      const float rh = RowHeight(c);
      c.row = RectF(card.rect.X + kPad, cy, colw - 2 * kPad, rh);
      if (c.kind == Kind::Toggle) {
        c.ctl = RectF(c.row.X + c.row.Width - 46.f, cy + 10.f, 46.f, 26.f);
      } else if (c.kind == Kind::Segmented) {
        c.ctl = RectF(c.row.X, cy + 26.f, c.row.Width, 34.f);
      } else {
        c.ctl = RectF(c.row.X, cy + 26.f, c.row.Width - 64.f, 34.f);
      }
      cy += rh;
    }
    y[card.column] += h + kGap;
  }
  // Both columns end at the same height: the last card of the shorter column takes the difference.
  const float bottom = (std::max)(y[0], y[1]) - kGap;
  for (int col = 0; col < 2; ++col) {
    for (int k = int(g_cards.size()) - 1; k >= 0; --k) {
      if (g_cards[k].column != col) continue;
      g_cards[k].rect.Height += bottom - (y[col] - kGap);
      break;
    }
  }
  const float content = bottom + kGap;
  g_content_h = content;
  const float bar_y = content + 2.f;
  const float bw[3] = {160.f, 110.f, 130.f};
  float x = kBaseW - kMargin;
  for (int i = 2; i >= 0; --i) {
    x -= bw[i];
    g_btn[i] = RectF(x, bar_y + 14.f, bw[i], 44.f);
    x -= 12.f;
  }
}

float TotalHeight() { return g_content_h + kFooterH; }

GraphicsPath* Round(const RectF& r, float rad) {
  auto* p = new GraphicsPath();
  const float d = rad * 2;
  p->AddArc(r.X, r.Y, d, d, 180, 90);
  p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  p->CloseFigure();
  return p;
}

void FillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
  SolidBrush br(c);
  GraphicsPath* p = Round(r, rad);
  g.FillPath(&br, p);
  delete p;
}

void StrokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w = 1.f) {
  Pen pen(c, w);
  GraphicsPath* p = Round(r, rad);
  g.DrawPath(&pen, p);
  delete p;
}

void Text(Graphics& g, const std::wstring& s, const Font& f, const RectF& r, const Color& c,
          StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentCenter,
          bool wrap = false, bool ellipsis = true) {
  StringFormat fmt;
  fmt.SetAlignment(h);
  fmt.SetLineAlignment(v);
  if (!wrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
  fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
  SolidBrush br(c);
  g.DrawString(s.c_str(), -1, &f, r, &fmt, &br);
}

RectF Scaled(const RectF& r) { return RectF(r.X * g_scale, r.Y * g_scale, r.Width * g_scale, r.Height * g_scale); }

std::wstring SliderText(const Ctl& c) {
  wchar_t b[32];
  if (c.id == kSens) swprintf(b, 32, L"%d%%", int(c.val));
  else swprintf(b, 32, L"%.1f", c.val);
  return b;
}

float SegWidth(const Ctl& c) { return c.ctl.Width / float(c.items.size()); }

void Paint(HDC hdc, int w, int h) {
  Graphics g(hdc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  g.Clear(kBg);
  FontFamily fam(L"Segoe UI");
  const float S = g_scale;
  Font fTitle(&fam, 22 * S, FontStyleBold, UnitPixel), fSub(&fam, 13 * S, FontStyleRegular, UnitPixel),
      fCard(&fam, 12 * S, FontStyleBold, UnitPixel), fLabel(&fam, 14 * S, FontStyleRegular, UnitPixel),
      fSeg(&fam, 13 * S, FontStyleRegular, UnitPixel), fSegBold(&fam, 13 * S, FontStyleBold, UnitPixel),
      fHint(&fam, 12.5f * S, FontStyleRegular, UnitPixel), fBtn(&fam, 14 * S, FontStyleBold, UnitPixel);

  // header
  FillRound(g, Scaled(RectF(kMargin, 26, 8, 40)), 3 * S, kAccent);
  Text(g, L"GEARS OF WAR: JUDGMENT", fTitle, Scaled(RectF(kMargin + 22, 20, 520, 32)), kText);
  Text(g, L"PC  -  graphics and controls", fSub, Scaled(RectF(kMargin + 22, 50, 520, 22)), kMuted);
  {
    const wchar_t* chip = g_game_found ? L"Game found" : L"GearsOfWarJudgment.exe not found next to this program";
    // Sized from the measured text, so it fits at any display scale.
    StringFormat measure(StringFormat::GenericTypographic());
    measure.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF box;
    g.MeasureString(chip, -1, &fSeg, PointF(0, 0), &measure, &box);
    const float cw = box.Width / S + 52.f;  // dot + padding on both sides
    RectF cr = Scaled(RectF(kBaseW - kMargin - cw, 32, cw, 30));
    FillRound(g, cr, 15 * S, g_game_found ? Color(255, 28, 52, 38) : Color(255, 60, 40, 28));
    SolidBrush dot(g_game_found ? kOk : kWarn);
    g.FillEllipse(&dot, cr.X + 13 * S, cr.Y + 11 * S, 8 * S, 8 * S);
    Text(g, chip, fSeg, RectF(cr.X + 30 * S, cr.Y, cr.Width - 30 * S, cr.Height), g_game_found ? kOk : kWarn,
         StringAlignmentNear, StringAlignmentCenter, false, false);
  }

  // cards
  for (auto& card : g_cards) {
    RectF cr = Scaled(card.rect);
    FillRound(g, cr, 12 * S, kCard);
    StrokeRound(g, cr, 12 * S, kCardLine);
    Text(g, card.title, fCard, RectF(cr.X + kPad * S, cr.Y + 6 * S, cr.Width - 2 * kPad * S, (kTitleH - 6) * S), kMuted);
    if (card.note) {
      Text(g, card.note, fHint, RectF(cr.X + kPad * S, cr.Y + cr.Height - 54 * S, cr.Width - 2 * kPad * S, 44 * S),
           kMuted, StringAlignmentNear, StringAlignmentFar, true);
    }
    for (int i : card.ctls) {
      Ctl& c = g_c[i];
      const RectF row = Scaled(c.row), ct = Scaled(c.ctl);
      const bool hot = g_hover_ctl == i;
      if (c.kind == Kind::Toggle) {
        Text(g, c.label, fLabel, RectF(row.X, row.Y + 2 * S, row.Width - 60 * S, 40 * S), kText);
        FillRound(g, ct, ct.Height / 2, c.on ? (hot ? kAccentHover : kAccent) : (hot ? Color(255, 80, 87, 102) : kOff));
        SolidBrush knob(kWhite);
        const float d = ct.Height - 6 * S;
        g.FillEllipse(&knob, c.on ? ct.X + ct.Width - d - 3 * S : ct.X + 3 * S, ct.Y + 3 * S, d, d);
      } else {
        Text(g, c.label, fLabel, RectF(row.X, row.Y + 2 * S, row.Width, 22 * S), kText);
        if (!c.last.empty()) {
          Text(g, c.last, fHint, RectF(row.X, row.Y + 2 * S, row.Width, 22 * S), kMuted, StringAlignmentFar);
        }
        if (c.kind == Kind::Segmented) {
          FillRound(g, ct, 9 * S, kTrack);
          const float sw = ct.Width / float(c.items.size());
          for (size_t k = 0; k < c.items.size(); ++k) {
            RectF seg(ct.X + sw * k + 3 * S, ct.Y + 3 * S, sw - 6 * S, ct.Height - 6 * S);
            const bool sel = int(k) == c.sel;
            const bool over = hot && g_hover_seg == int(k);
            if (sel) FillRound(g, seg, 7 * S, over ? kAccentHover : kAccent);
            else if (over) FillRound(g, seg, 7 * S, kTrackHover);
            Text(g, c.items[k], sel ? fSegBold : fSeg, seg, sel ? kWhite : kMuted, StringAlignmentCenter);
          }
        } else {
          const float t = float((c.val - c.vmin) / (c.vmax - c.vmin));
          const float ty = ct.Y + ct.Height / 2;
          RectF track(ct.X, ty - 3 * S, ct.Width, 6 * S);
          FillRound(g, track, 3 * S, kTrack);
          FillRound(g, RectF(track.X, track.Y, track.Width * t, track.Height), 3 * S, hot || g_drag == i ? kAccentHover : kAccent);
          SolidBrush knob(kWhite);
          const float d = 18 * S;
          g.FillEllipse(&knob, ct.X + ct.Width * t - d / 2, ty - d / 2, d, d);
          Text(g, SliderText(c), fSegBold, RectF(ct.X + ct.Width + 8 * S, ct.Y, 60 * S, ct.Height), kText, StringAlignmentFar);
        }
      }
    }
  }

  // footer: hint / status, buttons
  const float fy = g_content_h * S;
  SolidBrush footer_brush(Color(255, 12, 13, 16));
  g.FillRectangle(&footer_brush, 0.f, fy, float(w), float(h) - fy);
  std::wstring msg = g_status;
  Color mc = g_status_color;
  if (g_hover_ctl >= 0) { msg = g_c[g_hover_ctl].hint; mc = kMuted; }
  else if (g_hover_btn == 0) { msg = L"Put every setting back to its default."; mc = kMuted; }
  else if (g_hover_btn == 2) { msg = L"Save and start the game."; mc = kMuted; }
  Text(g, msg, fHint, RectF(kMargin * S, fy + 8 * S, (g_btn[0].X - kMargin - 12) * S, (kFooterH - 16) * S), mc,
       StringAlignmentNear, StringAlignmentCenter, true);
  for (int i = 0; i < 3; ++i) {
    RectF br = Scaled(g_btn[i]);
    const bool over = g_hover_btn == i, down = g_press_btn == i && over;
    const bool disabled = i == 2 && !g_game_found;
    Color fill = i == 2 ? (disabled ? kOff : (down ? Color(255, 200, 52, 30) : over ? kAccentHover : kAccent))
                        : (i == 1 ? (down ? Color(255, 56, 62, 74) : over ? Color(255, 66, 73, 88) : Color(255, 50, 56, 68))
                                  : (over ? kTrackHover : kTrack));
    FillRound(g, br, 10 * S, fill);
    Text(g, kBtnText[i], fBtn, br, disabled ? kMuted : kWhite, StringAlignmentCenter);
  }
}

// ------------------------------------------------------------------------------------------
// behaviour
// ------------------------------------------------------------------------------------------
void SetStatus(const wchar_t* s, const Color& c) { g_status = s; g_status_color = c; }

void MarkChanged() {
  g_changed = true;
  SetStatus(L"You have unsaved changes.", kWarn);
}

int HitControl(float x, float y, int* seg) {
  *seg = -1;
  for (int i = 0; i < kCount; ++i) {
    Ctl& c = g_c[i];
    RectF r = Scaled(c.row);
    if (x < r.X || x > r.X + r.Width || y < r.Y || y > r.Y + r.Height) continue;
    if (c.kind == Kind::Segmented) {
      RectF ct = Scaled(c.ctl);
      if (y >= ct.Y && y <= ct.Y + ct.Height && x >= ct.X && x <= ct.X + ct.Width) {
        *seg = (std::min)(int((x - ct.X) / (ct.Width / c.items.size())), int(c.items.size()) - 1);
      }
    }
    return i;
  }
  return -1;
}

int HitButton(float x, float y) {
  for (int i = 0; i < 3; ++i) {
    RectF r = Scaled(g_btn[i]);
    if (x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height) return i;
  }
  return -1;
}

void SetSliderFromX(Ctl& c, float x) {
  RectF ct = Scaled(c.ctl);
  double t = std::clamp(double((x - ct.X) / ct.Width), 0.0, 1.0);
  double v = c.vmin + t * (c.vmax - c.vmin);
  v = std::round(v / c.step) * c.step;
  v = std::clamp(v, c.vmin, c.vmax);
  if (v != c.val) {
    c.val = v;
    c.dirty = true;
    MarkChanged();
  }
}

bool DoSave() {
  ApplyControls();
  if (!SaveConfig()) {
    SetStatus(L"Could not write config\\gowj.toml (is the folder writable?).", kWarn);
    return false;
  }
  g_changed = false;
  SetStatus(L"Saved. The game reads these settings the next time it starts.", kOk);
  return true;
}

void DoPlay() {
  if (!g_game_found) return;
  if (g_changed && !DoSave()) return;
  const std::wstring exe = g_dir + L"\\GearsOfWarJudgment.exe";
  ShellExecuteW(g_hwnd, L"open", exe.c_str(), nullptr, g_dir.c_str(), SW_SHOWNORMAL);
  PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}

void DoReset() {
  if (MessageBoxW(g_hwnd, L"Put every setting back to its default? Your current settings file is kept as gowj.toml.bak.",
                  L"Reset to defaults", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  std::string old;
  if (ReadTextFile(g_cfg_path, old)) WriteTextFile(g_cfg_path + L".bak", old);
  g_lines = SplitLines(kDefaultConfig);
  SaveConfig();
  LoadControls();
  g_changed = false;
  SetStatus(L"Defaults restored.", kOk);
}

void Redraw() { InvalidateRect(g_hwnd, nullptr, FALSE); }

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      const int cw = rc.right, ch = rc.bottom;
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, cw, ch);
      HGDIOBJ old = SelectObject(mem, bmp);
      Paint(mem, cw, ch);
      BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const float x = float(GET_X_LPARAM(l)), y = float(GET_Y_LPARAM(l));
      if (g_drag >= 0) { SetSliderFromX(g_c[g_drag], x); Redraw(); return 0; }
      int seg;
      const int c = HitControl(x, y, &seg);
      const int b = HitButton(x, y);
      if (c != g_hover_ctl || seg != g_hover_seg || b != g_hover_btn) {
        g_hover_ctl = c; g_hover_seg = seg; g_hover_btn = b;
        Redraw();
      }
      TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
      TrackMouseEvent(&t);
      return 0;
    }
    case WM_MOUSELEAVE:
      g_hover_ctl = g_hover_seg = g_hover_btn = -1;
      Redraw();
      return 0;
    case WM_LBUTTONDOWN: {
      const float x = float(GET_X_LPARAM(l)), y = float(GET_Y_LPARAM(l));
      SetCapture(h);
      const int b = HitButton(x, y);
      if (b >= 0) { g_press_btn = b; Redraw(); return 0; }
      int seg;
      const int i = HitControl(x, y, &seg);
      if (i < 0) return 0;
      Ctl& c = g_c[i];
      if (c.kind == Kind::Toggle) {
        RectF r = Scaled(c.row);
        if (y >= r.Y && y <= r.Y + r.Height) { c.on = !c.on; c.dirty = true; MarkChanged(); }
      } else if (c.kind == Kind::Segmented) {
        if (seg >= 0 && seg != c.sel) {
          if (c.id == kQuality) ApplyPreset(seg);
          else { c.sel = seg; c.dirty = true; }
          MarkChanged();
        }
      } else {
        RectF ct = Scaled(c.ctl);
        if (y >= ct.Y - 6 * g_scale && y <= ct.Y + ct.Height + 6 * g_scale) { g_drag = i; SetSliderFromX(c, x); }
      }
      Redraw();
      return 0;
    }
    case WM_LBUTTONUP: {
      ReleaseCapture();
      const float x = float(GET_X_LPARAM(l)), y = float(GET_Y_LPARAM(l));
      const int b = HitButton(x, y);
      const int pressed = g_press_btn;
      g_press_btn = -1;
      g_drag = -1;
      Redraw();
      if (pressed >= 0 && pressed == b) {
        if (b == 0) DoReset();
        else if (b == 1) DoSave();
        else DoPlay();
        Redraw();
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (w == VK_ESCAPE) PostMessageW(h, WM_CLOSE, 0, 0);
      else if (w == VK_RETURN) { if (GetKeyState(VK_CONTROL) < 0) DoPlay(); else { DoSave(); Redraw(); } }
      return 0;
    case WM_DPICHANGED: {
      g_scale = float(HIWORD(w)) / 96.f;
      const RECT* r = reinterpret_cast<RECT*>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      Redraw();
      return 0;
    }
    case WM_CLOSE:
      if (g_changed) {
        const int r = MessageBoxW(h, L"Save your changes before closing?", L"Gears of War: Judgment",
                                  MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return 0;
        if (r == IDYES && !DoSave()) return 0;
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

std::wstring SelfDir() {
  wchar_t buf[MAX_PATH * 2];
  GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
  std::wstring p = buf;
  return p.substr(0, p.find_last_of(L"\\/"));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  g_dir = SelfDir();
  g_cfg_path = g_dir + L"\\config\\gowj.toml";
  InitControls();
  LoadConfig();
  LoadControls();
  LoadLastStart();

  // Command-line mode: --apply key=value ... (used by scripts and tests; no window).
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv && argc >= 2 && std::wstring(argv[1]) == L"--apply") {
    for (int i = 2; i < argc; ++i) {
      std::string kv = Narrow(argv[i]);
      const size_t eq = kv.find('=');
      if (eq == std::string::npos) continue;
      std::string value = kv.substr(eq + 1);
      if (kv.substr(0, eq) == "preset") {
        ApplyPreset(value == "low" ? 1 : value == "medium" ? 2 : value == "high" ? 3 : 0);
        continue;
      }
      if (kv.substr(0, eq) == "limit") {
        const int hz = atoi(value.c_str());
        g_c[kLimit].sel = hz == 30 ? 0 : hz == 60 ? 1 : hz == 90 ? 2 : hz == 120 ? 3 : 4;
        g_c[kLimit].dirty = true;
        continue;
      }
      if (value == "-") { RemoveKey(kv.substr(0, eq)); continue; }
      // Text values must be quoted in the config file (a bare word makes the whole file invalid).
      const bool bare = value == "true" || value == "false" ||
                        value.find_first_not_of("0123456789.-+") == std::string::npos;
      if (!bare && value.front() != '"') value = "\"" + value + "\"";
      SetValue(kv.substr(0, eq), value);
    }
    ApplyControls();
    return SaveConfig() ? 0 : 1;
  }

  {
    DWORD a = GetFileAttributesW((g_dir + L"\\GearsOfWarJudgment.exe").c_str());
    g_game_found = a != INVALID_FILE_ATTRIBUTES;
  }
  SetStatus(g_game_found ? L"Hover a setting for details. Changes apply the next time the game starts."
                         : L"Put this program in the same folder as GearsOfWarJudgment.exe to use the Play button.",
            kMuted);

  using SetCtx = BOOL(WINAPI*)(HANDLE);
  if (auto fn = reinterpret_cast<SetCtx>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))) {
    fn(reinterpret_cast<HANDLE>(-4));  // per-monitor v2
  } else {
    SetProcessDPIAware();
  }

  GdiplusStartupInput gsi;
  ULONG_PTR token = 0;
  GdiplusStartup(&token, &gsi, nullptr);

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"gowj_settings";
  RegisterClassW(&wc);

  Layout();
  // Size from the DPI of the monitor the window will open on.
  HDC sdc = GetDC(nullptr);
  g_scale = float(GetDeviceCaps(sdc, LOGPIXELSX)) / 96.f;
  ReleaseDC(nullptr, sdc);
  RECT rc{0, 0, LONG(kBaseW * g_scale), LONG(TotalHeight() * g_scale)};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&rc, style, FALSE);
  const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Gears of War: Judgment - Settings", style,
                           (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
                           ww, wh, nullptr, nullptr, inst, nullptr);
  const UINT dpi = GetDpiForWindow(g_hwnd);
  if (dpi && dpi != UINT(g_scale * 96.f + 0.5f)) {
    g_scale = float(dpi) / 96.f;
    RECT r2{0, 0, LONG(kBaseW * g_scale), LONG(TotalHeight() * g_scale)};
    AdjustWindowRectExForDpi(&r2, style, FALSE, 0, dpi);
    SetWindowPos(g_hwnd, nullptr, 0, 0, r2.right - r2.left, r2.bottom - r2.top, SWP_NOMOVE | SWP_NOZORDER);
  }
  BOOL dark = TRUE;
  DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));  // dark title bar
  DwmSetWindowAttribute(g_hwnd, 19, &dark, sizeof(dark));
  ShowWindow(g_hwnd, show);
  UpdateWindow(g_hwnd);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  GdiplusShutdown(token);
  return 0;
}
