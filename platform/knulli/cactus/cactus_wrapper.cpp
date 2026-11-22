// Cactus Galaxy Wrapper: la interfaz de la consola (RG35XX H, KNULLI) que convierte tu disco de Super Mario Galaxy
// (ISO, RVZ, WIA, WBFS, GCZ o CISO) en el game.pak del port. Dibuja a 640x480 en memoria (un desierto de noche bajo
// una galaxia, con un saguaro que crece con el avance) y lo muestra con SDL2; la conversion la hace nebula-pak
// (platform/knulli/pak), que corre como proceso aparte y cuenta su avance por la salida.
//
//   cactus-galaxy-wrapper [--smg <carpeta smg>] [--auto]      la interfaz
//   cactus-galaxy-wrapper --demo <carpeta>                     capturas PNG de cada pantalla (sin SDL)
//
// --auto (lo usa el lanzador del juego): si hay un solo disco valido lo convierte sin preguntar y, al terminar, sale
// para que arranque el juego. Salida: 0 si hay game.pak al final, 1 si no, 3 si no se pudo abrir la pantalla.
// GPL-3.0-or-later, como Nebula. Fuente 8x8: font8x8 de Daniel Hepper (dominio publico).
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CACTUS_NO_SDL
#include <SDL.h>
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

// font8x8 declares its tables as `char` with values above 127.
#define char unsigned char
#include "font8x8/font8x8_basic.h"
#include "font8x8/font8x8_ext_latin.h"
#undef char

#include "strings.h"
#include "logo_senor_cactus.h"

namespace {

using u32 = uint32_t;
constexpr int W = 640, H = 480;

// ---------------------------------------------------------------------------------------------------------------
// Drawing: 0xRRGGBB pixels in memory.

struct Canvas {
  std::vector<u32> px = std::vector<u32>(W * H, 0);

  void blend(int x, int y, u32 c, float a) {
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0.0f) return;
    u32& d = px[y * W + x];
    if (a >= 1.0f) { d = c; return; }
    const int ia = int(a * 256.0f);
    const int r = int(d >> 16 & 255) + (((int(c >> 16 & 255) - int(d >> 16 & 255)) * ia) >> 8);
    const int g = int(d >> 8 & 255) + (((int(c >> 8 & 255) - int(d >> 8 & 255)) * ia) >> 8);
    const int b = int(d & 255) + (((int(c & 255) - int(d & 255)) * ia) >> 8);
    d = u32(r) << 16 | u32(g) << 8 | u32(b);
  }
  void add(int x, int y, float r, float g, float b) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    u32& d = px[y * W + x];
    const int nr = std::min(255, int(d >> 16 & 255) + int(r));
    const int ng = std::min(255, int(d >> 8 & 255) + int(g));
    const int nb = std::min(255, int(d & 255) + int(b));
    d = u32(nr) << 16 | u32(ng) << 8 | u32(nb);
  }
  void rect(int x, int y, int w, int h, u32 c, float a = 1.0f) {
    for (int j = std::max(0, y); j < std::min(H, y + h); ++j)
      for (int i = std::max(0, x); i < std::min(W, x + w); ++i) blend(i, j, c, a);
  }
  void rrect(int x, int y, int w, int h, int r, u32 c, float a = 1.0f) {
    for (int j = 0; j < h; ++j) {
      for (int i = 0; i < w; ++i) {
        const int dx = i < r ? r - i - 1 : (i >= w - r ? i - (w - r) : -1);
        const int dy = j < r ? r - j - 1 : (j >= h - r ? j - (h - r) : -1);
        float cover = 1.0f;
        if (dx >= 0 && dy >= 0) {
          const float d = std::sqrt(float(dx * dx + dy * dy)) - float(r) + 1.0f;
          cover = std::clamp(1.0f - d, 0.0f, 1.0f);
        }
        blend(x + i, y + j, c, a * cover);
      }
    }
  }
  void rrect_outline(int x, int y, int w, int h, int r, int t, u32 c, float a = 1.0f) {
    for (int j = 0; j < h; ++j) {
      for (int i = 0; i < w; ++i) {
        const float cx = std::clamp(float(i) + 0.5f, float(r), float(w - r));
        const float cy = std::clamp(float(j) + 0.5f, float(r), float(h - r));
        const float d = std::hypot(float(i) + 0.5f - cx, float(j) + 0.5f - cy);
        const float edge = float(r) - d;  // distance inside the rounded shape
        if (edge < 0.0f || edge > float(t) + 1.0f) continue;
        const float cover = std::clamp(edge + 0.5f, 0.0f, 1.0f) * std::clamp(float(t) + 0.5f - edge, 0.0f, 1.0f);
        if (r == 0 && i >= t && j >= t && i < w - t && j < h - t) continue;
        blend(x + i, y + j, c, a * cover);
      }
    }
  }
  void circle(float cx, float cy, float r, u32 c, float a = 1.0f) {
    for (int j = int(cy - r - 1); j <= int(cy + r + 1); ++j)
      for (int i = int(cx - r - 1); i <= int(cx + r + 1); ++i) {
        const float d = std::hypot(float(i) + 0.5f - cx, float(j) + 0.5f - cy);
        blend(i, j, c, a * std::clamp(r - d + 0.5f, 0.0f, 1.0f));
      }
  }
  void ellipse(float cx, float cy, float rx, float ry, u32 c, float a = 1.0f) {
    for (int j = int(cy - ry - 1); j <= int(cy + ry + 1); ++j)
      for (int i = int(cx - rx - 1); i <= int(cx + rx + 1); ++i) {
        const float u = (float(i) + 0.5f - cx) / rx, v = (float(j) + 0.5f - cy) / ry;
        const float d = (std::sqrt(u * u + v * v) - 1.0f) * std::min(rx, ry);
        blend(i, j, c, a * std::clamp(0.5f - d, 0.0f, 1.0f));
      }
  }
  void glow(float cx, float cy, float r, float red, float green, float blue) {
    for (int j = int(cy - r); j <= int(cy + r); ++j)
      for (int i = int(cx - r); i <= int(cx + r); ++i) {
        const float d2 = ((float(i) - cx) * (float(i) - cx) + (float(j) - cy) * (float(j) - cy)) / (r * r);
        if (d2 >= 1.0f) continue;
        const float k = (1.0f - d2) * (1.0f - d2);
        add(i, j, red * k, green * k, blue * k);
      }
  }
  void star(float cx, float cy, float r, u32 c, float a, float spin) {  // five points
    float xs[10], ys[10];
    for (int k = 0; k < 10; ++k) {
      const float ang = spin + float(k) * 3.14159265f / 5.0f - 3.14159265f / 2.0f;
      const float rr = k % 2 == 0 ? r : r * 0.45f;
      xs[k] = cx + rr * std::cos(ang);
      ys[k] = cy + rr * std::sin(ang);
    }
    for (int j = int(cy - r - 1); j <= int(cy + r + 1); ++j)
      for (int i = int(cx - r - 1); i <= int(cx + r + 1); ++i) {
        int inside = 0;
        for (int s = 0; s < 4; ++s) {  // 2x2 samples
          const float px_ = float(i) + 0.25f + 0.5f * float(s & 1), py_ = float(j) + 0.25f + 0.5f * float(s >> 1);
          bool in = false;
          for (int k = 0, l = 9; k < 10; l = k++) {
            if ((ys[k] > py_) != (ys[l] > py_) && px_ < (xs[l] - xs[k]) * (py_ - ys[k]) / (ys[l] - ys[k]) + xs[k]) in = !in;
          }
          inside += in;
        }
        blend(i, j, c, a * float(inside) / 4.0f);
      }
  }
};

u32 mix(u32 a, u32 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto ch = [&](int s) { return u32(float(a >> s & 255) + (float(b >> s & 255) - float(a >> s & 255)) * t) << s; };
  return ch(16) | ch(8) | ch(0);
}

// ---------------------------------------------------------------------------------------------------------------
// Text: font8x8 (U+0000-007F, U+00A0-00FF) and a few glyphs of our own.

struct Glyph {
  u32 code;
  const char* rows[8];
};
const Glyph kExtra[] = {
    {0x2713, {"........", ".......#", "......##", ".....##.", "#...##..", "##.##...", ".###....", "..#....."}},  // check
    {0x2717, {"........", "##....##", ".##..##.", "..####..", "...##...", "..####..", ".##..##.", "##....##"}},  // cross
    {0x2192, {"........", "....#...", "....##..", "#######.", "#######.", "....##..", "....#...", "........"}},  // ->
    {0x2190, {"........", "...#....", "..##....", ".#######", ".#######", "..##....", "...#....", "........"}},  // <-
    {0x2191, {"...##...", "..####..", ".######.", "...##...", "...##...", "...##...", "...##...", "........"}},  // up
    {0x2193, {"...##...", "...##...", "...##...", "...##...", ".######.", "..####..", "...##...", "........"}},  // down
    {0x25C4, {"........", ".....##.", "...####.", ".######.", ".######.", "...####.", ".....##.", "........"}},  // <
    {0x25BA, {"........", ".##.....", ".####...", ".######.", ".######.", ".####...", ".##.....", "........"}},  // >
    {0x2026, {"........", "........", "........", "........", "........", "........", "##.##.##", "##.##.##"}},  // ...
};

// Cyrillic of our own (Russian: the interface's and the game's), in font8x8's style; letters shaped like Latin ones
// are those (А = A, ё = ë...).
struct CyrillicGlyph {
  u32 code, latin;
  const char* rows[8];
};
const CyrillicGlyph kCyrillic[] = {
    {0x0410, 0x41, {}},  // А
    {0x0411, 0, {"######..", "##......", "##......", "#####...", "##..##..", "##..##..", "#####...", "........"}},  // Б
    {0x0412, 0x42, {}},  // В
    {0x0413, 0, {"######..", "##......", "##......", "##......", "##......", "##......", "##......", "........"}},  // Г
    {0x0414, 0, {"..####..", ".##.##..", ".##.##..", ".##.##..", ".##.##..", ".##.##..", "#######.", "##...##."}},  // Д
    {0x0415, 0x45, {}},  // Е
    {0x0401, 0xCB, {}},  // Ё
    {0x0416, 0, {"##.##.##", "##.##.##", ".######.", "..####..", ".######.", "##.##.##", "##.##.##", "........"}},  // Ж
    {0x0417, 0, {".####...", "##..##..", "....##..", "..###...", "....##..", "##..##..", ".####...", "........"}},  // З
    {0x0418, 0, {"##...##.", "##..###.", "##.####.", "#######.", "####.##.", "###..##.", "##...##.", "........"}},  // И
    {0x0419, 0, {"..###...", "##...##.", "##..###.", "##.####.", "####.##.", "###..##.", "##...##.", "........"}},  // Й
    {0x041A, 0x4B, {}},  // К
    {0x041B, 0, {"..####..", ".##.##..", ".##.##..", ".##.##..", ".##.##..", ".##.##..", "##..##..", "........"}},  // Л
    {0x041C, 0x4D, {}},  // М
    {0x041D, 0x48, {}},  // Н
    {0x041E, 0x4F, {}},  // О
    {0x041F, 0, {"#######.", "##...##.", "##...##.", "##...##.", "##...##.", "##...##.", "##...##.", "........"}},  // П
    {0x0420, 0x50, {}},  // Р
    {0x0421, 0x43, {}},  // С
    {0x0422, 0x54, {}},  // Т
    {0x0423, 0, {"##...##.", "##...##.", "##...##.", ".######.", ".....##.", "##...##.", ".#####..", "........"}},  // У
    {0x0424, 0, {"...##...", ".######.", "##.##.##", "##.##.##", ".######.", "...##...", "...##...", "........"}},  // Ф
    {0x0425, 0x58, {}},  // Х
    {0x0426, 0, {"##..##..", "##..##..", "##..##..", "##..##..", "##..##..", "#######.", ".....##.", "........"}},  // Ц
    {0x0427, 0, {"##...##.", "##...##.", "##...##.", ".######.", ".....##.", ".....##.", ".....##.", "........"}},  // Ч
    {0x0428, 0, {"##.##.##", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "########", "........"}},  // Ш
    {0x0429, 0, {"##.##.##", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "########", ".......#", "........"}},  // Щ
    {0x042A, 0, {"###.....", ".##.....", ".##.....", ".####...", ".##.##..", ".##.##..", ".####...", "........"}},  // Ъ
    {0x042B, 0, {"##....##", "##....##", "##....##", "####..##", "##.##.##", "##.##.##", "####..##", "........"}},  // Ы
    {0x042C, 0, {"##......", "##......", "##......", "#####...", "##..##..", "##..##..", "#####...", "........"}},  // Ь
    {0x042D, 0, {".####...", "##..##..", "....##..", "..####..", "....##..", "##..##..", ".####...", "........"}},  // Э
    {0x042E, 0, {"##..###.", "##.##.##", "####..##", "####..##", "####..##", "##.##.##", "##..###.", "........"}},  // Ю
    {0x042F, 0, {".######.", "##...##.", "##...##.", ".######.", "..##.##.", ".##..##.", "##...##.", "........"}},  // Я
    {0x0430, 0x61, {}},  // а
    {0x0431, 0, {"..####..", ".##.....", "##......", "#####...", "##..##..", "##..##..", ".####...", "........"}},  // б
    {0x0432, 0, {"........", "........", "#####...", "##..##..", "#####...", "##..##..", "#####...", "........"}},  // в
    {0x0433, 0, {"........", "........", "######..", "##......", "##......", "##......", "##......", "........"}},  // г
    {0x0434, 0, {"........", "........", "..####..", ".##.##..", ".##.##..", ".##.##..", "#######.", "##...##."}},  // д
    {0x0435, 0x65, {}},  // е
    {0x0451, 0xEB, {}},  // ё
    {0x0436, 0, {"........", "........", "##.##.##", ".######.", "..####..", ".######.", "##.##.##", "........"}},  // ж
    {0x0437, 0, {"........", "........", ".####...", "....##..", "..###...", "....##..", ".####...", "........"}},  // з
    {0x0438, 0, {"........", "........", "##...##.", "##..###.", "##.####.", "####.##.", "##...##.", "........"}},  // и
    {0x0439, 0, {"........", "..###...", "##...##.", "##..###.", "##.####.", "####.##.", "##...##.", "........"}},  // й
    {0x043A, 0, {"........", "........", "##..##..", "##.##...", "####....", "##.##...", "##..##..", "........"}},  // к
    {0x043B, 0, {"........", "........", "..####..", ".##.##..", ".##.##..", ".##.##..", "##..##..", "........"}},  // л
    {0x043C, 0, {"........", "........", "##...##.", "###.###.", "#######.", "##.#.##.", "##...##.", "........"}},  // м
    {0x043D, 0, {"........", "........", "##..##..", "##..##..", "######..", "##..##..", "##..##..", "........"}},  // н
    {0x043E, 0x6F, {}},  // о
    {0x043F, 0, {"........", "........", "######..", "##..##..", "##..##..", "##..##..", "##..##..", "........"}},  // п
    {0x0440, 0x70, {}},  // р
    {0x0441, 0x63, {}},  // с
    {0x0442, 0, {"........", "........", "######..", "..##....", "..##....", "..##....", "..##....", "........"}},  // т
    {0x0443, 0x79, {}},  // у
    {0x0444, 0, {"...##...", "...##...", ".######.", "##.##.##", "##.##.##", ".######.", "...##...", "...##..."}},  // ф
    {0x0445, 0x78, {}},  // х
    {0x0446, 0, {"........", "........", "##..##..", "##..##..", "##..##..", "##..##..", "#######.", ".....##."}},  // ц
    {0x0447, 0, {"........", "........", "##..##..", "##..##..", ".#####..", "....##..", "....##..", "........"}},  // ч
    {0x0448, 0, {"........", "........", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "########", "........"}},  // ш
    {0x0449, 0, {"........", "........", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "########", ".......#"}},  // щ
    {0x044A, 0, {"........", "........", "###.....", ".##.....", ".####...", ".##.##..", ".####...", "........"}},  // ъ
    {0x044B, 0, {"........", "........", "##....##", "##....##", "####..##", "##.##.##", "####..##", "........"}},  // ы
    {0x044C, 0, {"........", "........", "##......", "##......", "#####...", "##..##..", "#####...", "........"}},  // ь
    {0x044D, 0, {"........", "........", ".####...", "....##..", "..####..", "....##..", ".####...", "........"}},  // э
    {0x044E, 0, {"........", "........", "##..###.", "##.##.##", "####..##", "##.##.##", "##..###.", "........"}},  // ю
    {0x044F, 0, {"........", "........", ".#####..", "##..##..", ".#####..", "..#.##..", "##..##..", "........"}},  // я
};

void glyph_bits(u32 code, unsigned char out[8]) {
  if (code < 128) { memcpy(out, font8x8_basic[code], 8); return; }
  if (code >= 0xA0 && code <= 0xFF) { memcpy(out, font8x8_ext_latin[code - 0xA0], 8); return; }
  if (code >= 0x401 && code <= 0x451) {
    for (const CyrillicGlyph& g : kCyrillic) {
      if (g.code != code) continue;
      if (g.latin) { glyph_bits(g.latin, out); return; }
      for (int r = 0; r < 8; ++r) {
        out[r] = 0;
        for (int c = 0; c < 8; ++c) if (g.rows[r][c] == '#') out[r] |= u32(1) << c;
      }
      return;
    }
  }
  for (const Glyph& g : kExtra) {
    if (g.code != code) continue;
    for (int r = 0; r < 8; ++r) {
      out[r] = 0;
      for (int c = 0; c < 8; ++c) if (g.rows[r][c] == '#') out[r] |= u32(1) << c;
    }
    return;
  }
  memcpy(out, font8x8_basic[int('?')], 8);
}

std::vector<u32> utf8(const std::string& s) {
  std::vector<u32> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = (unsigned char)s[i];
    u32 code = c;
    int extra = 0;
    if (c >= 0xF0) { code = c & 7; extra = 3; }
    else if (c >= 0xE0) { code = c & 15; extra = 2; }
    else if (c >= 0xC0) { code = c & 31; extra = 1; }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) code = code << 6 | ((unsigned char)s[i] & 63);
    out.push_back(code);
  }
  return out;
}

int text_width(const std::string& s, int scale) { return int(utf8(s).size()) * 8 * scale; }

int text(Canvas& cv, int x, int y, const std::string& s, int scale, u32 color, float alpha = 1.0f, bool shadow = true) {
  const std::vector<u32> codes = utf8(s);
  for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
    const int ox = pass == 0 ? std::max(1, scale / 2) : 0;
    const u32 c = pass == 0 ? 0x0a0614 : color;
    const float a = pass == 0 ? alpha * 0.7f : alpha;
    int cx = x;
    for (u32 code : codes) {
      unsigned char bits[8];
      glyph_bits(code, bits);
      for (int r = 0; r < 8; ++r)
        for (int col = 0; col < 8; ++col)
          if (bits[r] >> col & 1) cv.rect(cx + col * scale + ox, y + r * scale + ox, scale, scale, c, a);
      cx += 8 * scale;
    }
  }
  return int(codes.size()) * 8 * scale;
}

void text_center(Canvas& cv, int cx, int y, const std::string& s, int scale, u32 color, float alpha = 1.0f) {
  text(cv, cx - text_width(s, scale) / 2, y, s, scale, color, alpha);
}

// At most `chars` characters, with an ellipsis.
std::string fit(const std::string& s, size_t chars) {
  const std::vector<u32> codes = utf8(s);
  if (codes.size() <= chars) return s;
  std::string out;
  size_t count = 0;
  for (size_t i = 0; i < s.size() && count < chars - 1;) {
    const unsigned char c = (unsigned char)s[i];
    const size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    out += s.substr(i, len);
    i += len;
    ++count;
  }
  return out + "\xE2\x80\xA6";
}

std::vector<std::string> wrap(const std::string& s, size_t chars) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush_word = [&] {
    if (word.empty()) return;
    if (!line.empty() && utf8(line).size() + 1 + utf8(word).size() > chars) { lines.push_back(line); line.clear(); }
    while (utf8(word).size() > chars) { lines.push_back(word.substr(0, chars)); word = word.substr(chars); }
    line += (line.empty() ? "" : " ") + word;
    word.clear();
  };
  for (char c : s) {
    if (c == ' ' || c == '\n') { flush_word(); if (c == '\n') { lines.push_back(line); line.clear(); } }
    else word += c;
  }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// ---------------------------------------------------------------------------------------------------------------
// The scene: night desert, a galaxy, a ringed planet, twinkling stars and the saguaro.

constexpr u32 kGreen = 0x8ef0a0, kGold = 0xffd36e, kPink = 0xff7fb8, kRed = 0xff6b5e, kWhite = 0xf4eefc,
              kMuted = 0xb9a9d6, kPanel = 0x140c2a;

float dune_back(int x) { return 352.0f + 16.0f * std::sin(float(x) * 0.011f + 1.0f) + 9.0f * std::sin(float(x) * 0.027f); }
float dune_front(int x) { return 404.0f + 12.0f * std::sin(float(x) * 0.008f + 2.0f) + 6.0f * std::sin(float(x) * 0.031f); }

void panel(Canvas& cv);
constexpr int kPanelX = 244, kPanelY = 14, kPanelW = 384, kPanelH = 452;

struct Star {
  float x, y, size, phase, speed;
  u32 color;
};

class Scene {
 public:
  Scene() {
    unsigned seed = 7;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return float(seed >> 8 & 0xFFFF) / 65535.0f; };
    // Sky.
    for (int y = 0; y < H; ++y) {
      const float t = float(y) / 360.0f;
      u32 c = t < 0.55f ? mix(0x07061f, 0x24124a, t / 0.55f) : mix(0x24124a, 0x9c3f5a, (t - 0.55f) / 0.35f);
      if (t > 0.9f) c = mix(0x9c3f5a, 0xe9824e, (t - 0.9f) / 0.1f);
      for (int x = 0; x < W; ++x) bg_.px[y * W + x] = c;
    }
    // The galaxy: two spiral arms of soft points around a warm core, tilted.
    const float gx = 150.0f, gy = 92.0f, tilt = -0.35f;
    for (int i = 0; i < 2600; ++i) {
      const float t = rnd();
      const float arm = float(i % 2) * 3.14159265f;
      const float ang = t * 4.4f * 3.14159265f + arm + (rnd() - 0.5f) * 0.55f;
      const float r = 5.0f + t * 88.0f + (rnd() - 0.5f) * 9.0f;
      float x = r * std::cos(ang), y = r * std::sin(ang) * 0.42f;
      const float xr = x * std::cos(tilt) - y * std::sin(tilt), yr = x * std::sin(tilt) + y * std::cos(tilt);
      const float warm = std::max(0.0f, 1.0f - t * 2.2f);
      const float k = 22.0f * (1.0f - t * 0.6f);
      bg_.glow(gx + xr, gy + yr, 2.0f + rnd() * 2.5f, k * (0.55f + warm), k * (0.45f + 0.4f * warm), k * (1.0f - 0.3f * warm));
    }
    bg_.glow(gx, gy, 26.0f, 120.0f, 95.0f, 70.0f);
    bg_.glow(gx, gy, 9.0f, 160.0f, 150.0f, 120.0f);
    // A ringed planet.
    bg_.ellipse(52.0f, 196.0f, 30.0f, 7.0f, 0xd9a16b, 0.5f);
    bg_.circle(52.0f, 196.0f, 15.0f, 0x6c4fa8);
    for (int j = 181; j < 211; ++j)  // shading
      for (int i = 37; i < 67; ++i) {
        const float d = std::hypot(float(i) - 46.0f, float(j) - 190.0f);
        if (std::hypot(float(i) - 52.0f, float(j) - 196.0f) < 15.0f) bg_.blend(i, j, 0xa58ad8, std::clamp(0.5f - d / 30.0f, 0.0f, 0.5f));
      }
    for (int i = 22; i < 83; ++i) {  // the ring's front half
      const float u = (float(i) - 52.0f) / 30.0f;
      const float y = 196.0f + 7.0f * std::sqrt(std::max(0.0f, 1.0f - u * u));
      bg_.blend(i, int(y), 0xf0c48c, 0.85f);
      bg_.blend(i, int(y) - 1, 0xd9a16b, 0.5f);
    }
    // Stars (only where the sky shows).
    for (int i = 0; i < 150; ++i) {
      Star s{rnd() * W, rnd() * 340.0f, 0.6f + rnd() * 1.4f, rnd() * 6.28f, 0.8f + rnd() * 2.5f,
             mix(0xffffff, rnd() < 0.5f ? 0xffd9a0 : 0xa8c8ff, rnd())};
      if (s.y < dune_back(int(s.x)) - 8.0f && std::hypot(s.x - gx, (s.y - gy) * 2.0f) > 60.0f) stars_.push_back(s);
    }
    // Dunes, far cacti, near ground.
    for (int x = 0; x < W; ++x) {
      const int yb = int(dune_back(x)), yf = int(dune_front(x));
      for (int y = yb; y < H; ++y) bg_.px[y * W + x] = mix(0x4a2850, 0x2d1838, float(y - yb) / 60.0f);
      for (int y = yf; y < H; ++y) bg_.px[y * W + x] = mix(0x2a1531, 0x170b1d, float(y - yf) / 70.0f);
      bg_.blend(x, yb, 0x8a4f6e, 0.7f);
      bg_.blend(x, yf, 0x6b3a5a, 0.8f);
    }
    far_cactus(214, 0.55f);
    far_cactus(18, 0.42f);
    far_cactus(600, 0.5f);
    // The panel's frame and title never change: drawn once into the background (what moves stays left of it).
    panel(bg_);
    stars_.erase(std::remove_if(stars_.begin(), stars_.end(), [](const Star& st) {
      return st.x > kPanelX - 6 && st.y > kPanelY - 6;
    }), stars_.end());
  }

  void draw(Canvas& cv, float time) const {
    cv.px = bg_.px;
    for (const Star& s : stars_) {
      const float tw = 0.55f + 0.45f * std::sin(time * s.speed + s.phase);
      if (s.size > 1.6f) {
        cv.glow(s.x, s.y, 3.0f * s.size, 70.0f * tw, 70.0f * tw, 80.0f * tw);
      }
      cv.circle(s.x, s.y, s.size * 0.6f, s.color, tw);
    }
    // A shooting star every few seconds.
    const float cycle = std::fmod(time, 7.0f);
    if (cycle < 0.9f) {
      const float t = cycle / 0.9f;
      const float x = 30.0f + t * 220.0f, y = 40.0f + t * 70.0f;
      for (int k = 0; k < 26; ++k) {
        const float f = float(k) / 26.0f;
        cv.circle(x - f * 40.0f, y - f * 12.7f, 1.4f * (1.0f - f), 0xfff2d0, (1.0f - f) * (1.0f - t * 0.6f));
      }
    }
  }

 private:
  void far_cactus(int x, float s) {
    const int ground = int(dune_back(x)) + 2;
    const u32 c = 0x2a1636;
    const int h = int(70 * s), w = std::max(4, int(12 * s));
    bg_.rrect(x - w / 2, ground - h, w, h + 2, w / 2, c);
    bg_.rrect(x - w / 2 - int(14 * s), ground - int(h * 0.6f), int(16 * s), w - 1, 2, c);
    bg_.rrect(x - w / 2 - int(14 * s), ground - int(h * 0.85f), w - 2, int(h * 0.3f), (w - 2) / 2, c);
    bg_.rrect(x + w / 2 - 1, ground - int(h * 0.45f), int(13 * s), w - 2, 2, c);
    bg_.rrect(x + w / 2 + int(9 * s), ground - int(h * 0.68f), w - 2, int(h * 0.25f), (w - 2) / 2, c);
  }
  Canvas bg_;
  std::vector<Star> stars_;
};

// The saguaro: grows with `growth` (0..1), flowers with `bloom` (0..1), browns and droops with `wilt` (0..1).
void draw_cactus(Canvas& cv, float base_x, float growth, float bloom, float wilt, float time) {
  const float ground = dune_front(int(base_x)) + 6.0f;
  const float trunk_h = 70.0f + 185.0f * std::clamp(growth, 0.0f, 1.0f);
  const float w = 34.0f;
  const float top = ground - trunk_h;
  auto shade = [&](float u) {  // u: -1 left edge .. 1 right edge
    u32 c = u < -0.35f ? 0x67c872 : (u < 0.35f ? 0x43a152 : 0x2d7339);
    if (std::fabs(std::fmod(std::fabs(u) * 3.0f + 0.15f, 1.0f) - 0.5f) < 0.09f) c = mix(c, 0x1f5a2b, 0.7f);  // ribs
    return mix(c, mix(c, 0x7d6a3a, 0.8f), wilt);
  };
  auto column = [&](float cx, float ytop, float ybottom, float width, float lean) {
    // A vertical capsule with ribs and spines; `lean` tilts its top (the wilted cactus droops).
    const float r = width / 2.0f;
    for (int y = int(ytop - r); y <= int(ybottom); ++y) {
      const float along = std::clamp((ybottom - float(y)) / std::max(1.0f, ybottom - ytop + r), 0.0f, 1.0f);
      const float off = lean * along * along;
      float half = r;
      if (float(y) < ytop) half = std::sqrt(std::max(0.0f, r * r - (ytop - float(y)) * (ytop - float(y))));
      for (int x = int(cx + off - half - 1); x <= int(cx + off + half + 1); ++x) {
        const float u = (float(x) + 0.5f - cx - off) / std::max(0.5f, half);
        const float cover = std::clamp(half - std::fabs(float(x) + 0.5f - cx - off) + 0.5f, 0.0f, 1.0f);
        if (cover > 0.0f) cv.blend(x, y, shade(u), cover);
      }
      if (int(y) % 9 == 0 && float(y) > ytop - r * 0.5f) {  // spines on the ribs
        for (float rib : {-0.62f, 0.0f, 0.62f}) {
          const float sx = cx + off + rib * half;
          cv.blend(int(sx), y, mix(0xeaf7cc, 0x9a8a60, wilt), 0.95f);
          cv.blend(int(sx) + (rib < 0 ? -1 : 1), y - 1, mix(0xeaf7cc, 0x9a8a60, wilt), 0.6f);
        }
      }
    }
  };
  auto elbow = [&](float x0, float x1, float y, float thick) {
    const float lo = std::min(x0, x1), hi = std::max(x0, x1);
    for (int yy = int(y - thick / 2); yy <= int(y + thick / 2); ++yy) {
      const float v = (float(yy) - y) / (thick / 2);
      const u32 c = shade(v * 0.9f);
      for (int x = int(lo); x <= int(hi); ++x) cv.blend(x, yy, c, 1.0f);
    }
  };
  cv.ellipse(base_x + 6.0f, ground + 2.0f, 40.0f, 6.0f, 0x0a0410, 0.55f);
  const float lean = wilt * 16.0f;
  // Arms first (the trunk covers their joints).
  const float a1 = std::clamp((growth - 0.28f) / 0.35f, 0.0f, 1.0f);
  if (a1 > 0.0f) {
    const float y = ground - trunk_h * 0.42f, x = base_x - w / 2.0f - 20.0f;
    elbow(x, base_x, y, 22.0f);
    column(x, y - 18.0f - 56.0f * a1, y + 4.0f, 22.0f, -lean * 0.6f);
  }
  const float a2 = std::clamp((growth - 0.55f) / 0.35f, 0.0f, 1.0f);
  if (a2 > 0.0f) {
    const float y = ground - trunk_h * 0.62f, x = base_x + w / 2.0f + 17.0f;
    elbow(base_x, x, y, 20.0f);
    column(x, y - 14.0f - 46.0f * a2, y + 4.0f, 20.0f, lean * 0.6f);
  }
  column(base_x, top, ground, w, lean);
  if (bloom > 0.0f) {  // flowers: the crown and the arm tips
    auto flower = [&](float fx, float fy, float s) {
      for (int k = 0; k < 6; ++k) {
        const float ang = float(k) * 1.047f + time * 0.4f;
        cv.circle(fx + std::cos(ang) * 6.0f * s, fy + std::sin(ang) * 6.0f * s, 5.0f * s, kPink, 0.95f);
      }
      cv.circle(fx, fy, 4.0f * s, 0xffe066);
      cv.glow(fx, fy, 22.0f * s, 60.0f * s, 30.0f * s, 45.0f * s);
    };
    flower(base_x, top - w / 2.0f + 2.0f, bloom);
    if (a1 > 0.0f) flower(base_x - w / 2.0f - 20.0f, ground - trunk_h * 0.42f - 18.0f - 56.0f * a1 - 9.0f, bloom * 0.7f);
    if (a2 > 0.0f) flower(base_x + w / 2.0f + 17.0f, ground - trunk_h * 0.62f - 14.0f - 46.0f * a2 - 8.0f, bloom * 0.6f);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// The panel and its pieces.

constexpr int kLeft = kPanelX + 16;
constexpr int kCenter = kPanelX + kPanelW / 2;

void panel(Canvas& cv) {
  cv.rrect(kPanelX + 4, kPanelY + 6, kPanelW, kPanelH, 16, 0x05020c, 0.45f);
  cv.rrect(kPanelX, kPanelY, kPanelW, kPanelH, 16, kPanel, 0.84f);
  cv.rrect_outline(kPanelX, kPanelY, kPanelW, kPanelH, 16, 2, 0x5fc46a, 0.75f);
  text_center(cv, kCenter, kPanelY + 18, "CACTUS GALAXY", 3, kGreen);
  text_center(cv, kCenter, kPanelY + 46, "WRAPPER", 3, kGold);
  // A spiny divider.
  for (int x = kLeft + 10; x < kPanelX + kPanelW - 26; ++x) cv.blend(x, kPanelY + 80, 0x43a152, 0.9f);
  for (int x = kLeft + 14; x < kPanelX + kPanelW - 26; x += 12) {
    cv.blend(x, kPanelY + 79, 0xeaf7cc, 0.9f);
    cv.blend(x + 6, kPanelY + 81, 0xeaf7cc, 0.9f);
  }
  text_center(cv, kCenter, kPanelY + 90, "ISO · RVZ \xE2\x86\x92 game.pak", 2, kMuted);
}

int key_width(const char* label) { return utf8(label).size() > 1 ? text_width(label, 2) + 12 : 22; }

void button(Canvas& cv, int x, int y, const char* label, u32 color) {
  if (utf8(label).size() > 1) {  // START, SELECT: a pill
    const int w = key_width(label);
    cv.rrect(x + 1, y - 3, w, 22, 11, 0x05020c, 0.5f);
    cv.rrect(x, y - 4, w, 22, 11, color);
    text(cv, x + w / 2 - text_width(label, 2) / 2, y, label, 2, 0x140c2a, 1.0f, false);
    return;
  }
  cv.circle(float(x) + 11.0f, float(y) + 8.0f, 11.5f, 0x05020c, 0.5f);
  cv.circle(float(x) + 10.0f, float(y) + 7.0f, 11.0f, color);
  text(cv, x + 10 - text_width(label, 2) / 2, y, label, 2, 0x140c2a, 1.0f, false);
}

// "(A) Convertir   (B) Salir" centered in a row.
void hints(Canvas& cv, int y, const std::vector<std::pair<std::string, std::string>>& items) {
  int total = 0;
  for (const auto& [key, label] : items) total += key_width(key.c_str()) + 6 + text_width(label, 2) + 18;
  total -= 18;
  int x = kCenter - total / 2;
  for (const auto& [key, label] : items) {
    const u32 color = key == "A" ? 0xff6b6b : key == "B" ? 0xffcf5a : key == "X" ? 0x6fb8ff : 0xc9c0dc;
    button(cv, x, y, key.c_str(), color);
    x += key_width(key.c_str()) + 6;
    x += text(cv, x, y, label, 2, kWhite) + 18;
  }
}

void progress_bar(Canvas& cv, int x, int y, int w, int h, float p, float time) {
  cv.rrect(x, y, w, h, h / 2, 0x0b0618, 0.9f);
  cv.rrect_outline(x, y, w, h, h / 2, 2, 0x3a2a5c, 1.0f);
  const int fill = int(float(w - 6) * std::clamp(p, 0.0f, 1.0f));
  if (fill > 0) {
    const int fx = x + 3, fy = y + 3, fh = h - 6;
    cv.rrect(fx, fy, std::max(fh, fill), fh, fh / 2, 0x43a152);
    for (int i = 0; i < std::max(fh, fill); ++i) {  // a lying cactus: light top, dark bottom, ribs, spines
      cv.blend(fx + i, fy + 2, 0x7fd88a, 0.8f);
      cv.blend(fx + i, fy + 3, 0x67c872, 0.6f);
      cv.blend(fx + i, fy + fh - 3, 0x2d7339, 0.9f);
      cv.blend(fx + i, fy + fh / 2, 0x2f7d3e, 0.55f);
      if (i % 10 == 4 && i > fh / 2 && i < fill - fh / 2) {
        cv.blend(fx + i, fy - 1, 0xeaf7cc, 0.9f);
        cv.blend(fx + i, fy + fh, 0xeaf7cc, 0.9f);
        cv.blend(fx + i + 1, fy + fh / 2 - 4, 0xeaf7cc, 0.7f);
      }
    }
    // A little star riding the tip.
    cv.star(float(fx + std::max(fh, fill)) - float(fh) / 2.0f, float(fy + fh / 2), float(fh) * 0.62f, 0xffe066, 1.0f, time * 2.0f);
  }
}

// The interface's language: KNULLI's (system.language), CACTUS_LANG to force one.
int g_ui = UI_ES;
const char* T(Str s) { return kText[g_ui][s]; }

std::string format(const char* pattern, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* pattern, ...) {
  char b[512];
  va_list args;
  va_start(args, pattern);
  vsnprintf(b, sizeof(b), pattern, args);
  va_end(args);
  return b;
}

int ui_from_code(std::string code) {
  for (char& c : code) c = char(tolower((unsigned char)c));
  for (int i = 0; i < UI_COUNT; ++i) if (code.compare(0, 2, kUiCodes[i]) == 0) return i;
  return -1;
}

// "es_MX" from `key=value` files (batocera.conf / knulli.conf) or ES's es_settings.cfg.
std::string read_setting(const char* path, const char* key) {
  FILE* f = fopen(path, "r");
  if (f == nullptr) return "";
  char line[512];
  std::string found;
  const size_t n = strlen(key);
  while (fgets(line, sizeof(line), f)) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, key, n) == 0 && p[n] == '=') {
      found = p + n + 1;
    } else if (const char* tag = strstr(p, "name=\"Language\"")) {
      if (const char* v = strstr(tag, "value=\"")) found = std::string(v + 7, strcspn(v + 7, "\""));
    }
  }
  fclose(f);
  while (!found.empty() && (found.back() == '\n' || found.back() == '\r' || found.back() == ' ')) found.pop_back();
  return found;
}

std::string detect_ui_language() {
  if (const char* forced = getenv("CACTUS_LANG"); forced && ui_from_code(forced) >= 0) { g_ui = ui_from_code(forced); return std::string("CACTUS_LANG=") + forced; }
  for (const char* path : {"/userdata/system/batocera.conf", "/userdata/system/knulli.conf", "/userdata/system/system.conf"}) {
    const std::string v = read_setting(path, "system.language");
    if (ui_from_code(v) >= 0) { g_ui = ui_from_code(v); return std::string(path) + ": " + v; }
  }
  for (const char* path : {"/userdata/system/configs/emulationstation/es_settings.cfg", "/userdata/system/.emulationstation/es_settings.cfg"}) {
    const std::string v = read_setting(path, "Language");
    if (ui_from_code(v) >= 0) { g_ui = ui_from_code(v); return std::string(path) + ": " + v; }
  }
  for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
    const char* v = getenv(var);
    if (v && *v && strcmp(v, "C") != 0 && strcmp(v, "POSIX") != 0 && ui_from_code(v) >= 0) {
      g_ui = ui_from_code(v);
      return std::string(var) + "=" + v;
    }
  }
  g_ui = UI_ES;
  return "sin idioma del sistema: español";
}

std::string mib_text(double mib) {
  char b[64];
  if (mib >= 1024.0) snprintf(b, sizeof(b), "%.1f %s", mib / 1024.0, g_ui == UI_FR ? "Go" : g_ui == UI_RU ? "ГБ" : "GB");
  else snprintf(b, sizeof(b), "%.0f %s", mib, g_ui == UI_FR ? "Mo" : g_ui == UI_RU ? "МБ" : "MB");
  std::string s = b;
  if (g_ui != UI_EN) std::replace(s.begin(), s.end(), '.', ',');
  return s;
}

// nebula-pak's (Spanish) errors, in the interface's language where they are the usual ones.
std::string translate_error(const std::string& e) {
  unsigned long long need = 0, have = 0;
  const size_t at = e.find("hacen falta");
  if (at != std::string::npos && sscanf(e.c_str() + at, "hacen falta %llu MiB libres y hay %llu MiB", &need, &have) == 2) {
    return format(T(S_ERR_SPACE), mib_text(double(need)).c_str(), mib_text(double(have)).c_str());
  }
  if (e.find("el parche no corresponde") != std::string::npos) return T(S_ERR_PATCH);
  if (e.find("no queda espacio") != std::string::npos) return T(S_ERR_SPACE_SHORT);
  if (e.find("FAT32") != std::string::npos) return T(S_ERR_FAT32);
  if (e.find("main.dol") != std::string::npos) return T(S_ERR_DOL);
  if (e.find("cancelada") != std::string::npos) return T(S_ERR_CANCELLED);
  if (e.find("incompleta") != std::string::npos || e.find("danad") != std::string::npos ||
      e.find("antes de tiempo") != std::string::npos) {
    return T(S_ERR_INCOMPLETE);
  }
  return e;
}

std::string duration_text(double seconds) {
  char b[64];
  const long s = long(seconds + 0.5);
  const bool ru = g_ui == UI_RU;
  if (s >= 3600) snprintf(b, sizeof(b), ru ? "%ld ч %02ld мин" : "%ld h %02ld min", s / 3600, s / 60 % 60);
  else if (s >= 60) snprintf(b, sizeof(b), ru ? "%ld мин %02ld с" : "%ld min %02ld s", s / 60, s % 60);
  else snprintf(b, sizeof(b), ru ? "%ld с" : "%ld s", s);
  return b;
}

// ---------------------------------------------------------------------------------------------------------------
// Disc images and the conversion (nebula-pak as a child process).

struct Image {
  std::string path, name, format, id, title;
  double mib = 0;
  int status = -1;  // -1 checking, 0 Super Mario Galaxy USA, 1 another game, 3 not a disc
};

std::string lower(std::string s) {
  for (char& c : s) c = char(tolower((unsigned char)c));
  return s;
}

bool is_image(const std::string& name) {
  const std::string l = lower(name);
  for (const char* ext : {".iso", ".rvz", ".wia", ".wbfs", ".gcz", ".ciso"}) {
    const size_t n = strlen(ext);
    if (l.size() > n && l.compare(l.size() - n, n, ext) == 0) return true;
  }
  return false;
}

std::vector<Image> find_images(const std::vector<std::string>& dirs) {
  std::vector<Image> out;
  std::vector<std::string> seen;
  for (const std::string& dir : dirs) {
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) continue;
    while (dirent* e = readdir(d)) {
      if (e->d_name[0] == '.' || !is_image(e->d_name)) continue;
      const std::string path = dir + "/" + e->d_name;
      char real[PATH_MAX];
      const std::string key = realpath(path.c_str(), real) ? real : path;
      if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
      struct stat st;
      if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
      seen.push_back(key);
      Image im;
      im.path = path;
      im.name = e->d_name;
      im.mib = double(st.st_size) / 1048576.0;
      out.push_back(im);
    }
    closedir(d);
  }
  // .wbf1 halves of split images are not separate discs; Galaxy-looking names first.
  std::stable_sort(out.begin(), out.end(), [](const Image& a, const Image& b) {
    auto score = [](const Image& i) { const std::string l = lower(i.name); return l.find("galaxy") != std::string::npos || l.find("rmge01") != std::string::npos || l.find("smg") != std::string::npos; };
    return score(a) > score(b);
  });
  return out;
}

// Runs `argv`, calling `line` for each line (or \r-terminated update) it prints; returns its exit status.
pid_t spawn(const std::vector<std::string>& argv, int* out_fd) {
  int fds[2];
  if (pipe(fds) != 0) return -1;
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[0]);
    close(fds[1]);
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execv(args[0], args.data());
    _exit(127);
  }
  close(fds[1]);
  if (pid < 0) { close(fds[0]); return -1; }
  *out_fd = fds[0];
  return pid;
}

template <typename F>
int read_lines(int fd, pid_t pid, F&& line) {
  std::string pending;
  char buf[4096];
  while (true) {
    const ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; ++i) {
      if (buf[i] == '\n' || buf[i] == '\r') { if (!pending.empty()) line(pending); pending.clear(); }
      else pending += buf[i];
    }
  }
  if (!pending.empty()) line(pending);
  close(fd);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

struct Job {
  std::mutex mutex;
  std::thread thread;
  pid_t pid = -1;
  std::atomic<bool> finished{false};
  int exit_code = -1;
  double percent = 0, done_mib = 0, total_mib = 0;
  std::string error, last_line;
  double started = 0;
  int language = 0, patch_slot = -1;
  bool mod = false;  // making a translation's mod (nebula-pak --patch-mod or cactus-ruso), not game.pak
};

double now_seconds() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
}

// ---------------------------------------------------------------------------------------------------------------
// The app.

enum class Screen { NeedDisc, Menu, Language, Converting, Done, Viva, Failed, Settings, Controls, Mods };

// The custom controls profile: the game's Wii buttons that can be given another button of the handheld (the sticks
// stay: Mario and the pointer), and the handheld's buttons as the game's bindings name them (Nintendo and Xbox
// schemes: NEBULA_PAD_SCHEME picks one in the game, both are written). Positions as the game reads SDL's buttons.
struct WiiButton { const char* id; const char* label; Str text; };  // label, or text when it has words
const WiiButton kWiiButtons[] = {
    {"a", "Wii A", S_COUNT}, {"b", "Wii B", S_COUNT}, {"z", "Wii Z", S_COUNT}, {"c", "Wii C", S_COUNT},
    {"shake", nullptr, S_SHAKE}, {"plus", "Wii +", S_COUNT}, {"minus", "Wii -", S_COUNT},
    {"one", "Wii 1", S_COUNT}, {"two", "Wii 2", S_COUNT},
    {"dpad_up", "Wii \xE2\x86\x91", S_COUNT}, {"dpad_down", "Wii \xE2\x86\x93", S_COUNT},
    {"dpad_left", "Wii \xE2\x86\x90", S_COUNT}, {"dpad_right", "Wii \xE2\x86\x92", S_COUNT},
    {"pointer_recenter", nullptr, S_RECENTER},
};
constexpr int kWiiButtonCount = int(sizeof(kWiiButtons) / sizeof(kWiiButtons[0]));
enum Phys { kSouth, kEast, kWest, kNorth, kL1, kR1, kL2, kR2, kL3, kR3, kSelectBtn, kDUp, kDDown, kDLeft, kDRight, kPhysCount };
const char* const kPhysLabel[kPhysCount] = {"B", "A", "Y", "X", "L1", "R1", "L2", "R2", "L3", "R3", "SELECT",
    "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x90", "\xE2\x86\x92"};
const char* const kPhysSwitch[kPhysCount] = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS", "Minus",
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight"};
const char* const kPhysXbox[kPhysCount] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "View",
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight"};

// The languages of the USA disc, as GALAXY_LANGUAGE names them (the launcher reads smg/idioma.txt), and Brazilian
// Portuguese and Russian: the disc patched on the way with a translation's xdelta (smg/patches/), into one of those
// three slots (Russian in the interface's own Cyrillic).
struct Language {
  const char* key;
  const char* name;
  const char* note;
  const char* tag;  // for the patched ones: shown while converting and in the log
};
const Language kLanguages[] = {
    {"spanish", "Español", "textos y menús", ""},
    {"english", "English", "texts and menus", ""},
    {"french", "Français", "textes et menus", ""},
    {"ptbr", "Português", "Brasil · tradução", "PT-BR"},
    {"ru", "Русский", "русский перевод", "RU"},
};
constexpr int kPtBr = 3, kRu = 4;
constexpr int kPatchedFirst = kPtBr, kPatchedCount = 2;
inline bool is_patched(int language) { return language >= kPatchedFirst && language < kPatchedFirst + kPatchedCount; }
int patched_of(const std::string& key) {
  for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i) if (key == kLanguages[i].key) return i;
  return -1;
}

// The send-off before the game's very first start, in the game's language (Spanish, English, French, Brazilian
// Portuguese, Russian); "Cabrones" only in Spanish, the other languages say the two vivas.
const char* const kViva[5][3] = {
    {"¡Viva México, Viva Sonora, Cabrones!", "En breve iniciará el juego.", "Jugar ya"},
    {"Long live Mexico, long live Sonora!", "The game will start shortly.", "Play now"},
    {"Vive le Mexique, vive Sonora !", "Le jeu va bientôt commencer.", "Jouer"},
    {"Viva o México, viva Sonora!", "O jogo vai começar em breve.", "Jogar já"},
    {"Да здравствуют Мексика и Сонора!", "Игра скоро начнётся.", "Играть"},
};

// Cool sunglasses and a grin on the full-grown saguaro (draw_cactus at growth 1).
// Senor Cactus's logo (the port's author) on the send-off screen, above "Jugar ya": a square tile with a soft shadow
// under it and a thin warm frame, so it sits on the dark panel like a sticker.
void draw_seal(Canvas& cv, int x0, int y0) {
  const int n = kLogoSize;
  for (int y = -5; y < n + 8; ++y) {  // shadow, a little down and to the right, soft at its edges
    for (int x = -5; x < n + 8; ++x) {
      const float dx = std::max({0.0f, float(2 - x), float(x - (n + 2))});
      const float dy = std::max({0.0f, float(3 - y), float(y - (n + 3))});
      cv.blend(x0 + x, y0 + y, 0x0b0612, std::clamp(1.0f - std::hypot(dx, dy) / 5.0f, 0.0f, 1.0f) * 0.5f);
    }
  }
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const unsigned char* p = kLogoRgba + (y * n + x) * 4;
      cv.blend(x0 + x, y0 + y, u32(p[0]) << 16 | u32(p[1]) << 8 | u32(p[2]), p[3] / 255.0f);
    }
  }
  cv.rrect_outline(x0 - 2, y0 - 2, n + 4, n + 4, 0, 1, 0xffd9a0, 0.55f);  // the frame
}

void draw_shades(Canvas& cv, float base_x, float time) {
  const float ground = dune_front(int(base_x)) + 6.0f;
  const float top = ground - (70.0f + 185.0f * 1.0f);
  const int x = int(base_x), y = int(top + 14.0f);
  cv.rect(x - 19, y + 2, 4, 2, 0x0d0b14);  // temples
  cv.rect(x + 15, y + 2, 4, 2, 0x0d0b14);
  cv.rrect(x - 16, y, 14, 10, 3, 0x0d0b14);  // lenses
  cv.rrect(x + 2, y, 14, 10, 3, 0x0d0b14);
  cv.rect(x - 2, y + 2, 4, 2, 0x0d0b14);  // bridge
  for (int k = 0; k < 4; ++k) {  // glints
    cv.blend(x - 13 + k, y + 2 + k, 0xffffff, 0.75f);
    cv.blend(x + 5 + k, y + 2 + k, 0xffffff, 0.75f);
  }
  cv.blend(x - 12, y + 7, 0x6fb8ff, 0.5f);
  cv.blend(x + 6, y + 7, 0x6fb8ff, 0.5f);
  for (int dx = -7; dx <= 7; ++dx) {  // the grin (corners up)
    const int gy = y + 21 - dx * dx / 10;
    cv.blend(x + dx, gy, 0x173f20, 1.0f);
    cv.blend(x + dx, gy + 1, 0x173f20, 0.6f);
  }
  cv.blend(x - 8, y + 15, 0x173f20, 0.8f);  // dimples
  cv.blend(x + 8, y + 15, 0x173f20, 0.8f);
  const float pulse = 0.5f + 0.5f * std::sin(time * 4.0f);  // a shine on the lens
  cv.star(float(x + 14), float(y), 3.5f + pulse * 1.5f, 0xffffff, 0.4f + 0.6f * pulse, time * 2.0f);
}

struct App {
  std::string smg, gamedata, tool, output;
  bool auto_mode = false, delete_after = false, cancelling = false, deleted = false;
  std::vector<Image> images;
  std::mutex images_mutex;
  std::thread identify_thread;
  std::atomic<bool> identified{false};
  int selected = 0, scroll = 0;
  Screen screen = Screen::Menu;
  std::unique_ptr<Job> job;
  std::string error, converted_name;
  double done_at = 0, took = 0, speed = 0, speed_mib = 0, speed_at = 0;
  double free_mib = -1;
  bool have_pak = false;
  bool quit = false;
  int result = 1;
  int language = 0, language_choice = 0;
  std::string language_file, pak_language_file;
  std::string patch_files[kPatchedCount];  // the translations' xdelta found (PT-BR, RU), empty if not there
  // Russian from the Switch release's .zip or its .rusmg (smg/patches/): cactus-ruso makes its mod from game.pak.
  std::string ru_zip, ruso_tool;
  // Translations are mods over the disc's own game.pak (smg/patches/idioma-<key>, the files they change; the
  // launcher puts the chosen one in smg/mods and takes the others out), so the game's languages are always there and
  // a translation is turned on or off at once. An xdelta's mod is made once from the disc (nebula-pak --patch-mod),
  // Russian's from game.pak (cactus-ruso). A game.pak made before with a patch inside (game.pak.idioma) still works:
  // its language replaced there needs the disc converted again.
  int pak_patch = -1;         // that older game.pak's translation (kPtBr, kRu)...
  int pak_patched_slot = -1;  // ...its text in this slot (0-2)
  Image pending;  // the disc waiting for its language
  // Settings (X on the language screen): the game's internal resolution and vibration, in smg/opciones.cfg (the
  // launcher reads it before ajustes.cfg and ultra.cfg, which still win).
  std::string options_file;
  int opt_resolution = 2;  // kResolutions: auto, 50, 75 (the medium profile's), 100
  bool opt_resolution_set = false;  // chosen (or in the file): only then written (the profiles keep their own)
  bool opt_vibration = true;
  int settings_row = 0;
  bool failed_mod = false;  // the failure shown is a translation's mod's (game.pak is fine)
  // Controls (SELECT, or the Settings row): the default profile (the game's own) or the custom one, smg/mandos.cfg.
  std::string controls_file;
  // Mods (Ajustes > Mods): los de smg/mods/ (activos) y smg/mods-desactivados/ (apagados), sin los de idioma.
  struct Mod { std::string name; bool on; };
  std::vector<Mod> mods;
  int mods_row = 0;
  std::string mods_on_dir() const { return smg + "/mods"; }
  std::string mods_off_dir() const { return smg + "/mods-desactivados"; }
  void scan_mods() {
    mods.clear();
    for (const bool on : {true, false}) {
      if (DIR* d = opendir((on ? mods_on_dir() : mods_off_dir()).c_str())) {
        while (dirent* e = readdir(d)) {
          const std::string n = e->d_name;
          if (n.empty() || n[0] == '.' || n.rfind("idioma-", 0) == 0) continue;
          struct stat st{};
          if (stat(((on ? mods_on_dir() : mods_off_dir()) + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) mods.push_back({n, on});
        }
        closedir(d);
      }
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return lower(a.name) < lower(b.name); });
    mods_row = std::clamp(mods_row, 0, std::max(0, int(mods.size()) - 1));
  }
  int mods_active() const { int n = 0; for (const Mod& m : mods) n += m.on; return n; }
  // Apagar = mover la carpeta a smg/mods-desactivados/ (Nebula solo carga smg/mods/); encender, al reves.
  // Los archivos de un mod por su nombre en minusculas (sin notas): dos mods que comparten uno cambian lo mismo del
  // disco (los sueltos van al archivo de su nombre) y no deben estar encendidos a la vez.
  static void mod_files(const std::string& dir, std::vector<std::string>& out) {
    if (DIR* d = opendir(dir.c_str())) {
      while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        const std::string path = dir + "/" + n;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { mod_files(path, out); continue; }
        const std::string l = lower(n);
        const size_t dot = l.rfind('.');
        const std::string ext = dot == std::string::npos ? "" : l.substr(dot);
        if (ext == ".txt" || ext == ".md" || ext == ".png" || ext == ".jpg" || ext == ".pdf" || ext == ".xml") continue;
        out.push_back(l);
      }
      closedir(d);
    }
  }
  std::string mod_path(const Mod& m) const { return (m.on ? mods_on_dir() : mods_off_dir()) + "/" + m.name; }
  bool move_mod(Mod& m, bool on) {
    if (m.on == on) return true;
    const std::string dir = on ? mods_on_dir() : mods_off_dir();
    mkdir(dir.c_str(), 0755);
    if (rename(mod_path(m).c_str(), (dir + "/" + m.name).c_str()) != 0) return false;
    m.on = on;
    printf("[cactus] mod %s: %s\n", m.name.c_str(), on ? "activo" : "apagado");
    fflush(stdout);
    return true;
  }
  // Encender uno apaga antes los encendidos que cambian sus mismos archivos (p. ej. dos mods de personaje).
  void toggle_mod(int i) {
    if (i < 0 || i >= int(mods.size())) return;
    Mod& m = mods[i];
    if (m.on) { move_mod(m, false); return; }
    std::vector<std::string> mine;
    mod_files(mod_path(m), mine);
    std::sort(mine.begin(), mine.end());
    for (Mod& other : mods) {
      if (&other == &m || !other.on) continue;
      std::vector<std::string> theirs;
      mod_files(mod_path(other), theirs);
      for (const std::string& f : theirs) {
        if (std::binary_search(mine.begin(), mine.end(), f)) { move_mod(other, false); break; }
      }
    }
    move_mod(m, true);
  }
  bool opt_custom_controls = false;
  int controls_map[kWiiButtonCount];  // the handheld's button for each Wii button (Phys), -1: the game's own
  int controls_saved[kWiiButtonCount];
  bool controls_saved_custom = false;
  int controls_row = 0, controls_scroll = 0;
  bool capturing = false;
  Screen controls_from = Screen::Language;
  double started_at = now_seconds();
  std::string chain_image;    // the disc whose translation's mod follows its conversion
  bool chain_delete = false;  // ...deleted after that

  const std::string& patch_file(int which) const { return patch_files[which - kPatchedFirst]; }
  bool have_xdelta(int which) const { return is_patched(which) && !patch_file(which).empty(); }
  bool ru_mod() const { return !ru_zip.empty(); }  // Russian from the .rusmg/.zip (no disc needed)
  bool have_patch(int which) const { return have_xdelta(which) || (which == kRu && ru_mod()); }
  // A translation's mod made from the disc (an xdelta), not from game.pak.
  bool mod_needs_disc(int which) const { return is_patched(which) && !(which == kRu && ru_mod()); }
  std::string mod_dir(int which) const { return smg + "/patches/idioma-" + kLanguages[which].key; }
  std::string mod_active(int which) const { return smg + "/mods/idioma-" + kLanguages[which].key; }
  bool mod_made(int which) const {
    struct stat st;
    return is_patched(which) && (stat(mod_dir(which).c_str(), &st) == 0 || stat(mod_active(which).c_str(), &st) == 0);
  }
  // The game language whose files a translation's mod changes (english unless its patch said otherwise).
  int mod_slot(int which) const {
    const std::vector<std::string> w = words(mod_dir(which) + ".ranura");
    const int slot = w.empty() ? -1 : slot_of(w[0]);
    return slot >= 0 ? slot : 1;
  }
  void save_mod_slot(int which, int slot) {
    if (FILE* f = fopen((mod_dir(which) + ".ranura").c_str(), "w")) {
      fprintf(f, "%s\n", kLanguages[slot >= 0 ? slot : 1].key);
      fclose(f);
    }
  }
  bool any_translation() const {
    for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i) if (have_patch(i) || mod_made(i)) return true;
    return pak_patch >= 0;
  }

  static std::vector<std::string> words(const std::string& path) {
    std::vector<std::string> out;
    if (FILE* f = fopen(path.c_str(), "r")) {
      char word[32];
      while (out.size() < 4 && fscanf(f, "%31s", word) == 1) out.push_back(lower(word));
      fclose(f);
    }
    return out;
  }
  static int slot_of(const std::string& key) {
    for (int i = 0; i < 3; ++i) if (key == kLanguages[i].key) return i;
    return -1;
  }
  static constexpr const char* kResolutions[4] = {"auto", "50", "75", "100"};
  void load_options() {
    if (FILE* f = fopen(options_file.c_str(), "r")) {
      char line[160];
      while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos || l[0] == '#') continue;
        const std::string key = l.substr(0, eq), value = lower(l.substr(eq + 1));
        if (key == "GALAXY_GLES_RESOLUTION") {
          for (int i = 0; i < 4; ++i) if (value == kResolutions[i]) { opt_resolution = i; opt_resolution_set = true; }
        } else if (key == "NEBULA_VIBRACION") {
          opt_vibration = value != "0";
        } else if (key == "NEBULA_MANDOS") {
          opt_custom_controls = value == "personalizado";
        }
      }
      fclose(f);
    }
  }
  void save_options() {
    const std::string temporary = options_file + ".tmp";
    if (FILE* f = fopen(temporary.c_str(), "w")) {
      fprintf(f, "# Cactus Galaxy Wrapper: Ajustes (X en la pantalla de idiomas). ajustes.cfg y ultra.cfg mandan sobre esto.\n");
      if (opt_resolution_set) fprintf(f, "GALAXY_GLES_RESOLUTION=%s\n", kResolutions[opt_resolution]);
      fprintf(f, "NEBULA_VIBRACION=%d\nNEBULA_MANDOS=%s\n", opt_vibration ? 1 : 0,
              opt_custom_controls ? "personalizado" : "predeterminado");
      fclose(f);
      rename(temporary.c_str(), options_file.c_str());
    }
    printf("[cactus] ajustes: resolucion %s, vibracion %s, mandos %s\n", kResolutions[opt_resolution],
           opt_vibration ? "si" : "no", opt_custom_controls ? "personalizados" : "predeterminados");
    fflush(stdout);
  }
  void load_controls() {
    for (int& m : controls_map) m = -1;
    if (FILE* f = fopen(controls_file.c_str(), "r")) {
      char line[160];
      while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        const std::string prefix = "bind.switch_pro.";
        const size_t eq = l.find('=');
        if (l.rfind(prefix, 0) != 0 || eq == std::string::npos) continue;
        const std::string id = l.substr(prefix.size(), eq - prefix.size()), value = l.substr(eq + 1);
        for (int w = 0; w < kWiiButtonCount; ++w) {
          if (id != kWiiButtons[w].id) continue;
          for (int k = 0; k < kPhysCount; ++k) if (value == kPhysSwitch[k]) controls_map[w] = k;
        }
      }
      fclose(f);
    }
  }
  void save_controls() {
    const std::string temporary = controls_file + ".tmp";
    if (FILE* f = fopen(temporary.c_str(), "w")) {
      fprintf(f, "# Cactus Galaxy Wrapper: mandos personalizados (SELECT). bind.<esquema>.<boton del Wii>=<boton>;\n"
                 "# lo que no aparece queda como en el juego. Se usa con NEBULA_MANDOS=personalizado (smg/opciones.cfg).\n");
      for (int w = 0; w < kWiiButtonCount; ++w) {
        if (controls_map[w] < 0) continue;
        fprintf(f, "bind.switch_pro.%s=%s\nbind.xbox.%s=%s\n", kWiiButtons[w].id, kPhysSwitch[controls_map[w]],
                kWiiButtons[w].id, kPhysXbox[controls_map[w]]);
      }
      fclose(f);
      rename(temporary.c_str(), controls_file.c_str());
    }
  }
  void open_controls() {
    controls_from = screen;
    std::copy(std::begin(controls_map), std::end(controls_map), std::begin(controls_saved));
    controls_saved_custom = opt_custom_controls;
    controls_row = 0;
    controls_scroll = 0;
    capturing = false;
    screen = Screen::Controls;
  }
  void close_controls(bool keep) {
    if (keep) {
      save_controls();
      save_options();
    } else {
      std::copy(std::begin(controls_saved), std::end(controls_saved), std::begin(controls_map));
      opt_custom_controls = controls_saved_custom;
    }
    capturing = false;
    screen = controls_from;
  }
  // A button of the handheld while capturing: given to the Wii button of the row, and taken from any other.
  void capture(int phys) {
    if (!capturing || controls_row < 1) return;
    for (int& m : controls_map) if (m == phys) m = -1;
    controls_map[controls_row - 1] = phys;
    capturing = false;
  }
  std::string wii_button_name(int w) const {
    return kWiiButtons[w].label ? std::string(kWiiButtons[w].label) : std::string(T(kWiiButtons[w].text));
  }

  void load_language() {
    // Not chosen yet: the menu's language for a new install (spanish, english, french; Portuguese when the patch is
    // there), Spanish if game.pak is already there (what the launcher uses without smg/idioma.txt).
    language = have_pak ? 0 : (g_ui == UI_ES ? 0 : g_ui == UI_FR ? 2 : (g_ui == UI_PT && have_patch(kPtBr)) ? kPtBr
                                 : (g_ui == UI_RU && have_patch(kRu)) ? kRu : 1);
    const std::vector<std::string> pak = words(pak_language_file);  // an older game.pak with a patch: "ptbr english"
    pak_patch = pak.size() >= 2 && have_pak ? patched_of(pak[0]) : -1;
    pak_patched_slot = pak_patch >= 0 ? slot_of(pak[1]) : -1;
    if (pak_patched_slot < 0) pak_patch = -1;
    const std::vector<std::string> chosen = words(language_file);   // "english", "english ptbr" or "english ru"
    if (!chosen.empty() && slot_of(chosen[0]) >= 0) language = slot_of(chosen[0]);
    const int patched = chosen.size() >= 2 ? patched_of(chosen[1]) : -1;
    if (patched >= 0 && have_pak && (patched == pak_patch || mod_made(patched))) language = patched;
  }
  void save_language(int which) {
    language = which;
    const bool patched = is_patched(which);
    const int slot = !patched ? which : which == pak_patch ? std::max(0, pak_patched_slot) : mod_slot(which);
    const std::string temporary = language_file + ".tmp";
    if (FILE* f = fopen(temporary.c_str(), "w")) {
      fprintf(f, "%s%s%s\n", kLanguages[slot].key, patched ? " " : "", patched ? kLanguages[which].key : "");
      fclose(f);
      rename(temporary.c_str(), language_file.c_str());
    }
    printf("[cactus] idioma del juego: %s%s%s%s\n", kLanguages[slot].key, patched ? " (" : "",
           patched ? kLanguages[which].tag : "", patched ? ")" : " (sin parches)");
    fflush(stdout);
  }
  // The languages that need nothing made (the menu's left/right cycle through them): the game's own (but the one an
  // older game.pak's patch replaced) and the translations whose mod is made.
  std::vector<int> menu_languages() const {
    std::vector<int> out;
    if (pak_patched_slot >= 0) out.push_back(pak_patch);
    for (int i = 0; i < 3; ++i) if (i != pak_patched_slot) out.push_back(i);
    for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i)
      if (i != pak_patch && have_pak && mod_made(i)) out.push_back(i);
    return out;
  }
  // The languages a new install can have.
  std::vector<int> install_languages() const {
    std::vector<int> out = {0, 1, 2};
    for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i) if (have_patch(i) || mod_made(i)) out.push_back(i);
    return out;
  }
  static int step(const std::vector<int>& list, int current, int delta) {
    auto it = std::find(list.begin(), list.end(), current);
    const int at = it == list.end() ? 0 : int(it - list.begin());
    return list[size_t((at + delta + int(list.size())) % int(list.size()))];
  }
  void ask_language(const Image& im) {
    pending = im;
    const std::vector<int> list = install_languages();
    language_choice = std::find(list.begin(), list.end(), language) != list.end() ? language : list[0];
    screen = Screen::Language;
  }
  // With game.pak already made (no disc chosen): its languages, plus those that need it made again (a translation
  // whose patch is there, or the language a translation replaced) if a valid disc is at hand.
  bool pak_mode() const { return pending.path.empty(); }
  const Image* valid_disc() {
    for (const Image& im : images) if (im.status == 0) return &im;
    return nullptr;
  }
  // With game.pak there: the languages ready, plus what can be made: a translation's mod (Russian's from game.pak;
  // an xdelta's from the disc, if there is one) and the language an older game.pak's patch replaced (the disc
  // converted again).
  std::vector<int> language_options() {
    if (!pak_mode()) return install_languages();
    std::vector<int> out = menu_languages();
    std::lock_guard<std::mutex> lock(images_mutex);
    const bool disc = valid_disc() != nullptr;
    if (disc && pak_patched_slot >= 0) out.push_back(pak_patched_slot);
    for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i) {
      if (std::find(out.begin(), out.end(), i) != out.end()) continue;
      if ((i == kRu && ru_mod()) || (have_xdelta(i) && disc)) out.push_back(i);
    }
    return out;
  }
  bool needs_reinstall(int which) const {
    if (!pak_mode() || is_patched(which)) return false;
    const std::vector<int> have = menu_languages();
    return std::find(have.begin(), have.end(), which) == have.end();
  }
  bool needs_mod(int which) const { return pak_mode() && is_patched(which) && which != pak_patch && !mod_made(which); }
  void show_installed_languages() {
    pending = Image();
    language_choice = language;
    screen = Screen::Language;
  }
  std::vector<std::string> scan_dirs;
  double rescan_at = 0;
  // Which translation an .xdelta's name is (lowercase name): kPtBr, kRu or -1. Russian by "rus"/"russian"/"русск"
  // or "ru" as a word of its own (ru.xdelta, smg-ru.xdelta, SMG_RU_v1.xdelta), not inside another word.
  static int patch_of_name(const std::string& name) {
    const auto has = [&](const char* w) { return name.find(w) != std::string::npos; };
    if (has("pt-br") || has("ptbr") || has("pt_br") || has("portug")) return kPtBr;
    if (has("rus") || has("\xD1\x80\xD1\x83\xD1\x81") || has("\xD0\xA0\xD1\x83\xD1\x81")) return kRu;  // рус, Рус
    for (size_t at = name.find("ru"); at != std::string::npos; at = name.find("ru", at + 1)) {
      const bool before = at == 0 || !isalnum((unsigned char)name[at - 1]);
      const bool after = at + 2 >= name.size() || !isalpha((unsigned char)name[at + 2]);
      if (before && after) return kRu;
    }
    return -1;
  }
  void find_patch() {
    for (const std::string& dir : {smg + "/patches", gamedata, smg}) {
      DIR* d = opendir(dir.c_str());
      if (d == nullptr) continue;
      while (dirent* e = readdir(d)) {
        const std::string name = lower(e->d_name);
        const bool xdelta = name.size() > 7 && name.compare(name.size() - 7, 7, ".xdelta") == 0;
        const int which = xdelta ? patch_of_name(name.substr(0, name.size() - 7)) : -1;
        if (which >= 0 && patch_files[which - kPatchedFirst].empty()) patch_files[which - kPatchedFirst] = dir + "/" + e->d_name;
        // The Switch release's .zip, or what cactus-ruso --extraer keeps of it (.rusmg, what the port ships).
        const size_t dot = name.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
        if ((ext == ".zip" || ext == ".rusmg") && patch_of_name(name.substr(0, dot)) == kRu &&
            (ru_zip.empty() || ext == ".rusmg"))
          ru_zip = dir + "/" + e->d_name;
      }
      closedir(d);
    }
    for (int i = kPatchedFirst; i < kPatchedFirst + kPatchedCount; ++i)
      if (have_xdelta(i)) printf("[cactus] parche %s: %s\n", kLanguages[i].tag, patch_file(i).c_str());
    if (ru_mod()) printf("[cactus] traducción rusa de Switch (mod): %s\n", ru_zip.c_str());
  }

  void refresh_disk() {
    struct statvfs fs;
    free_mib = statvfs(gamedata.c_str(), &fs) == 0 ? double(fs.f_bavail) * double(fs.f_frsize) / 1048576.0 : -1;
    struct stat st;
    have_pak = stat(output.c_str(), &st) == 0 && st.st_size > 0;
  }

  void start_identify() {
    identify_thread = std::thread([this] {
      for (size_t i = 0;; ++i) {
        std::string path;
        {
          std::lock_guard<std::mutex> lock(images_mutex);
          if (i >= images.size()) break;
          path = images[i].path;
        }
        int fd = -1;
        std::string out;
        const pid_t pid = spawn({tool, "--identify", path}, &fd);
        int code = 3;
        if (pid > 0) code = read_lines(fd, pid, [&](const std::string& l) { if (out.empty() && l.find('\t') != std::string::npos) out = l; });
        std::lock_guard<std::mutex> lock(images_mutex);
        Image& im = images[i];
        im.status = code == 0 ? 0 : (code == 1 && !out.empty() ? 1 : 3);
        if (!out.empty()) {
          std::vector<std::string> f;
          size_t at = 0;
          while (true) { const size_t t = out.find('\t', at); f.push_back(out.substr(at, t - at)); if (t == std::string::npos) break; at = t + 1; }
          if (f.size() >= 4) { im.id = f[0]; im.format = f[1]; im.title = f[3]; }
        }
      }
      // Valid discs first.
      std::lock_guard<std::mutex> lock(images_mutex);
      std::stable_sort(images.begin(), images.end(), [](const Image& a, const Image& b) { return (a.status == 0) > (b.status == 0); });
      identified = true;
    });
  }

  // game.pak, always the disc's own (a translation is a mod over it: made next, from this disc or game.pak).
  void start_conversion(const Image& im, int chosen) {
    const bool then_mod = is_patched(chosen);
    chain_image = then_mod ? im.path : std::string();
    chain_delete = then_mod && delete_after && mod_needs_disc(chosen);
    printf("[cactus] convirtiendo %s -> %s\n", im.path.c_str(), output.c_str());
    start_job({tool, im.path, output}, chosen, im.name, false, im.path, delete_after && !chain_delete);
  }
  // A translation's mod: Russian's from game.pak (cactus-ruso), an xdelta's from the disc (nebula-pak --patch-mod).
  void start_mod(int which, const std::string& disc, bool remove_disc) {
    if (which == kRu && ru_mod()) {
      printf("[cactus] traducción rusa -> %s\n", mod_dir(kRu).c_str());
      start_job({ruso_tool, ru_zip, output, mod_dir(kRu)}, kRu, ru_zip.substr(ru_zip.rfind('/') + 1), true, "", false);
    } else {
      printf("[cactus] parche %s como mod -> %s\n", kLanguages[which].tag, mod_dir(which).c_str());
      start_job({tool, "--patch-mod", patch_file(which), disc, mod_dir(which)}, which,
                disc.substr(disc.rfind('/') + 1), true, disc, remove_disc);
    }
  }
  void start_job(const std::vector<std::string>& args, int chosen, const std::string& name, bool mod,
                 const std::string& image_path, bool remove_image) {
    job = std::make_unique<Job>();
    job->language = chosen;
    job->mod = mod;
    job->started = now_seconds();
    converted_name = name;
    speed = 0; speed_mib = 0; speed_at = job->started;
    cancelling = false;
    Job* j = job.get();
    int fd = -1;
    j->pid = spawn(args, &fd);
    if (j->pid < 0) { error = T(S_ERR_RUN); screen = Screen::Failed; job.reset(); return; }
    screen = Screen::Converting;
    fflush(stdout);
    j->thread = std::thread([this, j, fd, image_path, remove_image] {
      const int code = read_lines(fd, j->pid, [j](const std::string& l) {
        printf("%s\n", l.c_str());
        fflush(stdout);
        std::lock_guard<std::mutex> lock(j->mutex);
        j->last_line = l;
        unsigned percent = 0;
        unsigned long long done = 0, total = 0;
        if (sscanf(l.c_str(), " %u%% (%llu / %llu MiB)", &percent, &done, &total) == 3) {
          j->percent = percent; j->done_mib = double(done); j->total_mib = double(total);
        } else if (l.rfind("Error: ", 0) == 0) {
          j->error = l.substr(7);
        } else if (l.rfind("PATCH_LANGUAGE\t", 0) == 0) {
          j->patch_slot = slot_of(l.substr(15));
        }
      });
      std::lock_guard<std::mutex> lock(j->mutex);
      j->exit_code = code;
      if (code == 0 && remove_image && !image_path.empty()) deleted = unlink(image_path.c_str()) == 0;
      j->finished = true;
    });
  }

  void finish_job() {
    if (!job || !job->finished) return;
    job->thread.join();
    took = now_seconds() - job->started;
    refresh_disk();
    if (job->exit_code == 0 && have_pak) {
      if (job->mod) {
        // The translation's mod: which game language its files are (an xdelta's says it; Russian's is English).
        save_mod_slot(job->language, job->patch_slot);
      } else {
        // game.pak just made from the disc: its own languages, no older patch inside any more.
        pak_patch = -1;
        pak_patched_slot = -1;
        unlink(pak_language_file.c_str());
        if (is_patched(job->language) && have_patch(job->language)) {  // (a mod made before, with no source: kept)
          const double started = job->started;
          const int which = job->language;
          const std::string disc = chain_image;
          job.reset();
          start_mod(which, disc, chain_delete);
          if (job) job->started = started;  // the time is the whole install's
          return;
        }
      }
      screen = Screen::Done;
      result = 0;
      done_at = now_seconds();
      save_language(job->language);
    } else {
      error = job->error.empty() ? (cancelling ? std::string(T(S_ERR_CANCELLED)) : job->last_line) : translate_error(job->error);
      if (error.empty()) error = format(T(S_ERR_EXIT), job->exit_code);
      // A translation's mod that failed for a reason with no text of its own: said in the interface's language (the
      // tools' detail, in Spanish, stays in log.txt and is added in Spanish).
      failed_mod = job->mod;
      if (job->mod && !cancelling && (job->error.empty() || error == job->error)) {
        error = std::string(T(S_ERR_MOD)) + (g_ui == UI_ES && !job->error.empty() ? " " + job->error : std::string());
      }
      screen = Screen::Failed;
    }
    job.reset();
  }
  // Buttons: confirm (A/START), back (B/SELECT), toggle (X), up, down.
  enum Key { kConfirm, kBack, kToggle, kUp, kDown, kPrev, kNext, kStart, kMap };
  void press(Key key) {
    // SELECT: the controls (anywhere but while converting); START: confirm (on the controls screen, save).
    if (key == kMap) {
      if (screen == Screen::Language || screen == Screen::Menu || screen == Screen::Settings ||
          screen == Screen::NeedDisc || screen == Screen::Done) {
        if (now_seconds() - started_at > 1.0) open_controls();  // not the SELECT held to open the wrapper
      }
      return;
    }
    if (key == kStart && screen != Screen::Controls) key = kConfirm;
    switch (screen) {
      case Screen::Menu: {
        std::lock_guard<std::mutex> lock(images_mutex);
        if (key == kUp && selected > 0) --selected;
        if (key == kDown && selected + 1 < int(images.size())) ++selected;
        if (key == kToggle) delete_after = !delete_after;
        if (key == kBack) quit = true;
        if (key == kPrev || key == kNext) {  // the game's language, even with game.pak already made
          save_language(step(menu_languages(), language, key == kNext ? 1 : -1));
        }
        if (key == kConfirm && selected < int(images.size()) && images[selected].status == 0) ask_language(images[selected]);
        break;
      }
      case Screen::Language: {
        if (key == kToggle && pak_mode()) {  // X: the settings
          settings_row = 0;
          scan_mods();
          screen = Screen::Settings;
          break;
        }
        const std::vector<int> options = language_options();
        if (key == kUp || key == kPrev) language_choice = step(options, language_choice, -1);
        if (key == kDown || key == kNext) language_choice = step(options, language_choice, 1);
        if (key == kBack) {
          if (pak_mode()) quit = true;  // leave without playing
          else screen = Screen::Menu;
        }
        if (key == kConfirm) {
          if (!pak_mode()) {
            start_conversion(pending, language_choice);
          } else if (needs_mod(language_choice)) {
            // The translation's mod over this game.pak (Russian's in seconds; an xdelta's reads the disc), then the game.
            images_mutex.lock();
            const Image* disc = valid_disc();
            const std::string path = disc ? disc->path : std::string();
            images_mutex.unlock();
            if (!mod_needs_disc(language_choice) || !path.empty()) start_mod(language_choice, path, false);
          } else if (!needs_reinstall(language_choice)) {
            save_language(language_choice);  // the game starts in it
            result = 0;
            quit = true;
          } else {
            images_mutex.lock();
            const Image* disc = valid_disc();
            const Image chosen = disc ? *disc : Image();
            images_mutex.unlock();
            if (!chosen.path.empty()) start_conversion(chosen, language_choice);
          }
        }
        break;
      }
      case Screen::NeedDisc:
        if (key == kBack) quit = true;
        break;
      case Screen::Converting:
        if (key == kBack && job && !cancelling) { cancelling = true; kill(job->pid, SIGTERM); }
        break;
      case Screen::Settings:
        if (key == kUp) settings_row = (settings_row + 3) % 4;
        if (key == kDown) settings_row = (settings_row + 1) % 4;
        if ((key == kPrev || key == kNext || key == kToggle || key == kConfirm) && settings_row == 3) {  // the mods' list
          save_options();
          scan_mods();
          mods_row = 0;
          screen = Screen::Mods;
          break;
        }
        if (key == kPrev || key == kNext || (key == kToggle)) {
          const int d = key == kPrev ? -1 : 1;
          if (settings_row == 0) { opt_resolution = (opt_resolution + d + 4) % 4; opt_resolution_set = true; }
          else if (settings_row == 1) opt_vibration = !opt_vibration;
          else opt_custom_controls = !opt_custom_controls;
        }
        if (key == kConfirm && settings_row == 2) {  // the custom mapping
          save_options();
          open_controls();
          break;
        }
        if (key == kConfirm || key == kBack) {
          save_options();
          screen = Screen::Language;
        }
        break;
      case Screen::Mods: {
        const int n = int(mods.size());
        if (n > 0 && key == kUp) mods_row = (mods_row + n - 1) % n;
        if (n > 0 && key == kDown) mods_row = (mods_row + 1) % n;
        if (key == kPrev || key == kNext || key == kToggle || key == kConfirm) toggle_mod(mods_row);
        if (key == kBack) screen = Screen::Settings;
        break;
      }
      case Screen::Controls: {
        if (capturing) break;  // the next button goes to the row (main loop)
        const int rows = opt_custom_controls ? kWiiButtonCount + 1 : 1;
        if (key == kUp) controls_row = (controls_row + rows - 1) % rows;
        if (key == kDown) controls_row = (controls_row + 1) % rows;
        if ((key == kPrev || key == kNext) && controls_row == 0) opt_custom_controls = !opt_custom_controls;
        if (key == kConfirm && controls_row > 0) capturing = true;
        if (key == kToggle && controls_row > 0) controls_map[controls_row - 1] = -1;
        if (key == kStart) close_controls(true);
        if (key == kBack) close_controls(false);
        break;
      }
      case Screen::Done:  // the game starts only on A
        if (key == kConfirm && result == 0 && viva_pending()) show_viva();
        else if (key == kConfirm) quit = true;
        break;
      case Screen::Viva:
        if (key == kConfirm) quit = true;
        break;
      case Screen::Failed:
        if (key == kConfirm) {
          refresh_disk();
          error.clear();
          if (have_pak) show_installed_languages();
          else screen = images.empty() ? Screen::NeedDisc : Screen::Menu;
        }
        if (key == kBack) quit = true;
        break;
    }
  }

  void tick() {
    finish_job();
    if (screen == Screen::NeedDisc && now_seconds() > rescan_at) {  // the disc copied in while we wait
      rescan_at = now_seconds() + 2.0;
      if (identified) {
        std::vector<Image> found = find_images(scan_dirs);
        if (!found.empty()) {
          if (identify_thread.joinable()) identify_thread.join();
          {
            std::lock_guard<std::mutex> lock(images_mutex);
            images = std::move(found);
            selected = 0;
          }
          printf("[cactus] disco encontrado: %s\n", images[0].path.c_str());
          fflush(stdout);
          identified = false;
          refresh_disk();
          start_identify();
          auto_mode = true;
          screen = Screen::Menu;
        }
      }
    }
    if (auto_mode && screen == Screen::Menu && identified && !job && error.empty()) {
      std::lock_guard<std::mutex> lock(images_mutex);
      int valid = 0, which = -1;
      for (int i = 0; i < int(images.size()); ++i) if (images[i].status == 0) { ++valid; which = i; }
      auto_mode = false;  // only once
      if (valid == 1) ask_language(images[which]);  // straight to the language, then it installs
    }
    // Done hands over to the send-off; nothing starts the game but A.
    if (screen == Screen::Done && result == 0 && done_at > 0 && viva_pending() && now_seconds() - done_at > 3.5) show_viva();
  }
  bool auto_quit = false;
  std::string viva_file;  // there once the send-off was shown (only before the game's first start)
  double viva_at = 0;
  bool viva_pending() const { return auto_quit && !viva_file.empty() && access(viva_file.c_str(), F_OK) != 0; }
  void show_viva() {
    screen = Screen::Viva;
    viva_at = now_seconds();
    if (FILE* f = fopen(viva_file.c_str(), "w")) { fputs("1\n", f); fclose(f); }
  }

  // ------------------------------------------------------------------------------------------------------------
  void draw(Canvas& cv, const Scene& scene, float time) {
    scene.draw(cv, time);
    float growth = 0.08f, bloom = 0.0f, wilt = 0.0f;
    if (screen == Screen::Converting && job) {
      std::lock_guard<std::mutex> lock(job->mutex);
      growth = 0.08f + 0.92f * float(job->percent) / 100.0f;
    }
    if (screen == Screen::Done) { growth = 1.0f; bloom = std::clamp(float(now_seconds() - done_at) * 1.5f, 0.0f, 1.0f); }
    if (screen == Screen::Viva) { growth = 1.0f; bloom = 1.0f; }
    if (screen == Screen::Failed) { growth = 0.45f; wilt = 1.0f; }
    if (demo_growth >= 0) { growth = demo_growth; bloom = demo_bloom; wilt = demo_wilt; }
    draw_cactus(cv, 120.0f, growth, bloom, wilt, time);
    if (screen == Screen::Viva) {
      draw_shades(cv, 120.0f, time);
      for (int i = 0; i < 46; ++i) {  // confetti in the flag's colours
        const float fx = std::fmod(float(i) * 97.3f + 18.0f * std::sin(time * 1.3f + float(i)), float(W));
        const float fy = std::fmod(time * (45.0f + float(i % 7) * 9.0f) + float(i) * 53.0f, float(H));
        const u32 c = i % 3 == 0 ? 0x2fbf5a : (i % 3 == 1 ? 0xffffff : 0xff4d4d);
        cv.rect(int(fx), int(fy), 3 + i % 2, 5, c, 0.85f);
      }
    }
    if (screen == Screen::Converting) {  // a star circling the cactus
      const float ang = time * 1.6f;
      const float top = dune_front(120) + 6.0f - (70.0f + 185.0f * growth);
      cv.star(120.0f + std::cos(ang) * 46.0f, top + 10.0f + std::sin(ang) * 14.0f, 7.0f, 0xffe066, 0.95f, time * 3.0f);
    }
    switch (screen) {
      case Screen::NeedDisc: draw_need_disc(cv, time); break;
      case Screen::Menu: draw_menu(cv); break;
      case Screen::Language: draw_language(cv, time); break;
      case Screen::Settings: draw_settings(cv, time); break;
      case Screen::Mods: draw_mods(cv, time); break;
      case Screen::Controls: draw_controls(cv, time); break;
      case Screen::Converting: draw_converting(cv, time); break;
      case Screen::Done: draw_done(cv); break;
      case Screen::Viva: draw_viva(cv); break;
      case Screen::Failed: draw_failed(cv); break;
    }
  }

  void draw_menu(Canvas& cv) {
    std::lock_guard<std::mutex> lock(images_mutex);
    int y = 134;
    if (images.empty()) {
      int ly = y + 4;
      for (const std::string& l : wrap(T(S_NO_DISC), 22)) { text_center(cv, kCenter, ly, l, 2, kGold); ly += 22; }
      ly += 12;
      for (const std::string& l : wrap(T(S_NO_DISC_HELP), 22)) { text(cv, kLeft + 4, ly, l, 2, kWhite); ly += 22; }
    } else {
      text(cv, kLeft, y, T(S_CHOOSE), 2, kGold);
      y += 26;
      const int rows = 4;
      if (selected < scroll) scroll = selected;
      if (selected >= scroll + rows) scroll = selected - rows + 1;
      for (int i = scroll; i < std::min(int(images.size()), scroll + rows); ++i) {
        const Image& im = images[i];
        const bool sel = i == selected;
        if (sel) {
          cv.rrect(kLeft - 6, y - 4, kPanelW - 20, 42, 8, 0x2f7d3e, 0.55f);
          cv.rrect_outline(kLeft - 6, y - 4, kPanelW - 20, 42, 8, 2, 0x7fd88a, 0.9f);
        }
        const u32 name_color = im.status == 0 ? kWhite : (im.status == -1 ? kMuted : 0x8f82a8);
        text(cv, kLeft + 2, y, fit(im.name, 22), 2, name_color);
        std::string detail;
        u32 dc = kMuted;
        const std::string size = mib_text(im.mib);
        if (im.status == -1) detail = size + " · " + T(S_CHECKING);
        else if (im.status == 0) { detail = im.format + " · " + size + " · \xE2\x9C\x93 USA"; dc = kGreen; }
        else if (im.status == 1) { detail = std::string("\xE2\x9C\x97 ") + T(S_OTHER_GAME) + " (" + im.id + ")"; dc = 0xd98a8a; }
        else { detail = std::string("\xE2\x9C\x97 ") + T(S_NOT_DISC); dc = 0xd98a8a; }
        text(cv, kLeft + 2, y + 18, fit(detail, 22), 2, dc);
        y += 46;
      }
      if (int(images.size()) > rows) {  // scroll arrows
        if (scroll > 0) text(cv, kPanelX + kPanelW - 30, 134, "\xE2\x86\x91", 2, kGold);
        if (scroll + rows < int(images.size())) text(cv, kPanelX + kPanelW - 30, 340, "\xE2\x86\x93", 2, kGold);
      }
    }
    // The game's language (left/right change it) and how much room there is.
    const int sy = 350;
    for (int x = kLeft + 10; x < kPanelX + kPanelW - 26; x += 4) cv.blend(x, sy - 8, 0x43a152, 0.6f);
    {
      int x = kLeft;
      x += text(cv, x, sy, "\xE2\x97\x84 ", 2, kGold);
      x += text(cv, x, sy, T(S_LANGUAGE), 2, kMuted);
      x += text(cv, x, sy, kLanguages[language].name, 2, kWhite);
      text(cv, x, sy, " \xE2\x96\xBA", 2, kGold);
    }
    if (free_mib >= 0) {
      const bool low = free_mib < 3400.0;
      const std::string room = mib_text(free_mib) + (low ? T(S_LOW) : "");
      text(cv, kLeft, sy + 20, fit(std::string(have_pak ? T(S_HAVE_PAK) : T(S_FREE)) + room, 22), 2, low ? kRed : kMuted);
    } else if (have_pak) {
      text(cv, kLeft, sy + 20, fit(T(S_HAVE_PAK_ONLY), 22), 2, kGold);
    }
    const bool can = selected < int(images.size()) && images[selected].status == 0;
    if (can) hints(cv, 406, {{"A", T(S_CONVERT)}, {"B", T(S_EXIT)}});
    else hints(cv, 406, {{"B", T(S_EXIT)}});
    hints(cv, 436, {{"X", std::string(T(S_DELETE_DISC)) + (delete_after ? T(S_YES) : T(S_NO))}});
  }

  void draw_need_disc(Canvas& cv, float time) {
    text_center(cv, kCenter, 136, fit(T(S_NEED_DISC), 14), 3, kGold);
    int y = 180;
    for (const std::string& l : wrap(T(S_NEED_DISC_HELP), 22)) {
      const bool folder = l.find("ports/smg") != std::string::npos;
      text(cv, folder ? kCenter - text_width(l, 2) / 2 : kLeft + 4, y, l, 2, folder ? kGreen : kWhite);
      y += folder ? 26 : 22;
    }
    const int dots = int(time * 2.0f) % 4;
    std::string searching = T(S_SEARCHING);
    if (searching.size() > 3 && searching.compare(searching.size() - 3, 3, "\xE2\x80\xA6") == 0) searching.resize(searching.size() - 3);
    text_center(cv, kCenter, 384, searching + std::string(size_t(dots), '.'), 2, kMuted, 0.7f + 0.3f * std::sin(time * 3.0f));
    hints(cv, 430, {{"B", T(S_EXIT)}});
  }

  void draw_settings(Canvas& cv, float time) {
    text_center(cv, kCenter, 128, T(S_SETTINGS), 2, kGold);
    const std::string res_value = opt_resolution == 0 ? std::string(T(S_AUTO_RES)) : std::string(kResolutions[opt_resolution]) + " %";
    static const char* const kSizes[4] = {"50-100 %", "320x264", "480x396", "640x528"};
    const std::string res_note = opt_resolution == 2 ? std::string(kSizes[2]) + " · " + T(S_RECOMMENDED) : kSizes[opt_resolution];
    const std::string mods_value = mods.empty() ? std::string("-") : format("%d / %d", mods_active(), int(mods.size()));
    const struct { const char* label; std::string value, note; } rows[4] = {
        {T(S_RESOLUTION), res_value, res_note},
        {T(S_VIBRATION), T(opt_vibration ? S_YES : S_NO), ""},
        {T(S_CONTROLS), T(opt_custom_controls ? S_PROFILE_CUSTOM : S_PROFILE_DEFAULT), ""},
        {T(S_MODS), mods_value, ""},
    };
    static constexpr int kRowY[4] = {152, 230, 286, 342};
    for (int row = 0; row < 4; ++row) {
      const int y = kRowY[row];
      const bool sel = row == settings_row;
      if (sel) {
        cv.rrect(kLeft + 10, y - 6, kPanelW - 52, row == 0 ? 72 : 52, 10, 0x2f7d3e, 0.55f);
        cv.rrect_outline(kLeft + 10, y - 6, kPanelW - 52, row == 0 ? 72 : 52, 10, 2, 0x7fd88a, 0.9f);
        cv.star(float(kLeft + 30), float(y + 8), 9.0f, 0xffe066, 1.0f, time * 2.0f);
      }
      text(cv, kLeft + 50, y, rows[row].label, 2, sel ? kGreen : kMuted);
      const std::string value = "\xE2\x97\x84 " + rows[row].value + " \xE2\x96\xBA";
      const int scale = text_width(value, 3) <= kPanelW - 40 ? 3 : 2;  // "Auto (dynamique)" does not fit large
      text_center(cv, kCenter, y + 24 + (scale == 2 ? 4 : 0), fit(value, scale == 3 ? 15 : 22), scale, sel ? kWhite : kMuted);
      if (!rows[row].note.empty()) text_center(cv, kCenter, y + 52, fit(rows[row].note, 22), 2, sel ? kGold : 0x8f82a8);
    }
    text_center(cv, kCenter, 398, fit(T(S_APPLIES), 22), 2, 0x8f82a8);
    hints(cv, 418, {{"SELECT", T(S_CONTROLS)}});
    hints(cv, 440, {{"A", settings_row == 2 ? T(S_ASSIGN) : settings_row == 3 ? T(S_MODS) : T(S_BACK)}, {"B", T(S_BACK)}});
  }

  // Ajustes > Mods: cada mod con Si/No (se aplica la proxima vez que se abra el juego).
  void draw_mods(Canvas& cv, float time) {
    text_center(cv, kCenter, 128, T(S_MODS), 2, kGold);
    if (mods.empty()) {
      int y = 190;
      for (const std::string& line : wrap(T(S_NO_MODS), 22)) { text_center(cv, kCenter, y, line, 2, kMuted); y += 22; }
    } else {
      constexpr int kVisible = 5;
      const int first = std::clamp(mods_row - kVisible / 2, 0, std::max(0, int(mods.size()) - kVisible));
      for (int i = first; i < std::min(int(mods.size()), first + kVisible); ++i) {
        const int y = 160 + (i - first) * 44;
        const bool sel = i == mods_row;
        if (sel) {
          cv.rrect(kLeft + 10, y - 8, kPanelW - 52, 38, 10, 0x2f7d3e, 0.55f);
          cv.rrect_outline(kLeft + 10, y - 8, kPanelW - 52, 38, 10, 2, 0x7fd88a, 0.9f);
          cv.star(float(kLeft + 30), float(y + 10), 8.0f, 0xffe066, 1.0f, time * 2.0f);
        }
        text(cv, kLeft + 46, y + 2, fit(mods[i].name, 13), 2, sel ? kWhite : kMuted);
        const std::string value = T(mods[i].on ? S_YES : S_NO);
        text(cv, kLeft + kPanelW - 60 - text_width(value, 2), y + 2, value, 2, mods[i].on ? kGreen : 0x8f82a8);
      }
      if (int(mods.size()) > kVisible) text_center(cv, kCenter, 382, format("%d / %d", mods_row + 1, int(mods.size())), 2, 0x8f82a8);
    }
    text_center(cv, kCenter, 398, fit(T(S_APPLIES), 22), 2, 0x8f82a8);
    hints(cv, 432, {{"A", std::string(T(S_YES)) + "/" + T(S_NO)}, {"B", T(S_BACK)}});
  }

  void draw_controls(Canvas& cv, float time) {
    text_center(cv, kCenter, 128, T(S_CONTROLS), 2, kGold);
    {  // the profile
      const int y = 152;
      const bool sel = controls_row == 0;
      if (sel) {
        cv.rrect(kLeft + 10, y - 5, kPanelW - 52, 28, 10, 0x2f7d3e, 0.55f);
        cv.rrect_outline(kLeft + 10, y - 5, kPanelW - 52, 28, 10, 2, 0x7fd88a, 0.9f);
      }
      const std::string value = "\xE2\x97\x84 " + std::string(T(opt_custom_controls ? S_PROFILE_CUSTOM : S_PROFILE_DEFAULT)) +
                                " \xE2\x96\xBA";
      text_center(cv, kCenter, y + 2, fit(value, 22), 2, sel ? kWhite : kMuted);
    }
    if (!opt_custom_controls) {
      text_center(cv, kCenter, 230, fit(T(S_AS_BEFORE), 22), 2, kGreen);
    } else {
      constexpr int kVisible = 8;
      const int sel = controls_row - 1;
      if (sel >= 0 && sel < controls_scroll) controls_scroll = sel;
      if (sel >= controls_scroll + kVisible) controls_scroll = sel - kVisible + 1;
      for (int i = controls_scroll; i < std::min(kWiiButtonCount, controls_scroll + kVisible); ++i) {
        const int y = 184 + (i - controls_scroll) * 23;
        const bool on = i == sel;
        if (on) {
          cv.rrect(kLeft + 10, y - 4, kPanelW - 52, 22, 8, capturing ? 0x7d5a2f : 0x2f7d3e, 0.55f);
          cv.star(float(kLeft + 22), float(y + 7), 6.0f, 0xffe066, 1.0f, time * 2.0f);
        }
        text(cv, kLeft + 34, y, fit(wii_button_name(i), 11), 2, on ? kWhite : kMuted);  // 11 + 8 fit the row
        const std::string value = on && capturing ? std::string("\xE2\x80\xA6")
                                  : controls_map[i] >= 0 ? std::string(kPhysLabel[controls_map[i]]) : std::string(T(S_AS_BEFORE));
        const std::string shown = fit(value, 8);
        text(cv, kPanelX + kPanelW - 36 - text_width(shown, 2), y, shown, 2,
             on && capturing ? kGold : controls_map[i] >= 0 ? kGreen : 0x8f82a8);
      }
      if (controls_scroll > 0) text_center(cv, kCenter, 176, "\xE2\x86\x91", 1, kMuted);
      if (controls_scroll + kVisible < kWiiButtonCount) text_center(cv, kCenter, 368, "\xE2\x86\x93", 1, kMuted);
    }
    if (capturing) {  // the prompt in full, in the interface's language (the row in orange is the Wii button)
      text_center(cv, kCenter, 384, fit(T(S_PRESS_BUTTON), 22), 2, kGold);
    } else {
      text_center(cv, kCenter, 384, fit(T(S_STICKS_SAME), 22), 2, 0x8f82a8);
    }
    if (capturing) {
      hints(cv, 432, {{"START", T(S_CANCEL)}});
    } else {
      if (controls_row > 0) hints(cv, 410, {{"A", T(S_ASSIGN)}, {"X", T(S_CLEAR)}});
      hints(cv, 434, {{"START", T(S_SAVE)}, {"B", T(S_BACK)}});
    }
  }

  void draw_language(Canvas& cv, float time) {
    text_center(cv, kCenter, 128, T(S_GAME_LANGUAGE), 2, kGold);
    text_center(cv, kCenter, 150, T(S_GAME_LANGUAGE_SUB), 2, kMuted);
    const std::vector<int> list = language_options();
    // Up to four rows with their note under the name; five or more (both translations) only the names, closer.
    const int n = int(list.size());
    const bool compact = n > 4;
    const int spacing = compact ? std::min(36, 156 / (n - 1)) : n > 3 ? 46 : 58;
    const int box = compact ? spacing - 2 : n > 3 ? 44 : 50;
    for (int row = 0; row < n; ++row) {
      const int i = list[size_t(row)];
      const int y = (n > 3 ? (compact ? 180 : 178) : 184) + row * spacing;
      const bool sel = i == language_choice;
      if (sel) {
        cv.rrect(kLeft + 10, y - 5, kPanelW - 52, box, 10, 0x2f7d3e, 0.55f);
        cv.rrect_outline(kLeft + 10, y - 5, kPanelW - 52, box, 10, 2, 0x7fd88a, 0.9f);
        cv.star(float(kLeft + 30), float(y + (compact ? box / 2 - 5 : 16)), 9.0f, 0xffe066, 1.0f, time * 2.0f);
      }
      text(cv, kLeft + 50, y, kLanguages[i].name, 3, sel ? kWhite : kMuted);
      if (!compact) text(cv, kLeft + 50, y + 24, kLanguages[i].note, 2, sel ? kGreen : 0x8f82a8);
    }
    // A game language with translations at hand: no patch, the game's own text.
    const bool native = !is_patched(language_choice) && any_translation();
    if (!pak_mode()) {
      text_center(cv, kCenter, 364, fit(pending.name, 22), 2, kMuted);
      text_center(cv, kCenter, 384, fit(T(native ? S_NO_PATCHES : S_CHANGE_LATER), 22), 2, native ? kGold : 0x8f82a8);
      hints(cv, 430, {{"A", T(S_INSTALL)}, {"B", T(S_BACK)}});
    } else if (needs_reinstall(language_choice)) {
      std::string disc;
      {
        std::lock_guard<std::mutex> lock(images_mutex);
        if (const Image* d = valid_disc()) disc = d->name;
      }
      text_center(cv, kCenter, 364, fit(T(S_REINSTALL), 22), 2, kGold);
      text_center(cv, kCenter, 384, fit(disc, 22), 2, kMuted);
      hints(cv, 404, {{"X", T(S_SETTINGS)}});
      hints(cv, 430, {{"A", T(S_INSTALL)}, {"B", T(S_EXIT)}});
    } else {
      text_center(cv, kCenter, 364, fit(T(needs_mod(language_choice) ? S_MAKE_MOD : S_PAK_READY), 22), 2,
                  needs_mod(language_choice) ? kGold : kGreen);
      text_center(cv, kCenter, 384, fit(T(native ? S_NO_PATCHES : S_CHANGE_LATER), 22), 2, native ? kGold : 0x8f82a8);
      hints(cv, 404, {{"X", T(S_SETTINGS)}});
      hints(cv, 430, {{"A", T(S_PLAY)}, {"B", T(S_EXIT)}});
    }
  }

  void draw_converting(Canvas& cv, float time) {
    double percent = 0, done = 0, total = 0;
    if (job) {
      std::lock_guard<std::mutex> lock(job->mutex);
      percent = job->percent; done = job->done_mib; total = job->total_mib;
    }
    if (demo_percent >= 0) { percent = demo_percent; done = demo_done; total = demo_total; speed = demo_speed; }
    const double t = now_seconds();
    if (demo_percent < 0 && t - speed_at > 2.0) {  // MiB/s over the last couple of seconds, smoothed
      const double sp = (done - speed_mib) / (t - speed_at);
      speed = speed <= 0 ? sp : speed * 0.6 + sp * 0.4;
      speed_mib = done; speed_at = t;
    }
    const int patched = job && is_patched(job->language) ? job->language : demo_patched;
    const std::string tag = patched >= 0 ? std::string(" · ") + kLanguages[patched].tag : std::string();
    text_center(cv, kCenter, 134, fit(cancelling ? T(S_CANCELLING) : std::string(T(S_CONVERTING)) + tag, 22), 2, kGold);
    text_center(cv, kCenter, 156, fit(converted_name, 22), 2, kMuted);
    text_center(cv, kCenter, 188, format("%d%%", int(percent)), 6, kWhite);
    progress_bar(cv, kLeft + 4, 252, kPanelW - 40, 32, float(percent / 100.0), time);
    const char* unit = g_ui == UI_FR ? "Mo" : g_ui == UI_RU ? "МБ" : "MB";
    if (total > 0) text_center(cv, kCenter, 300, format("%.0f / %.0f %s", done, total, unit), 2, kWhite);
    else text_center(cv, kCenter, 300, fit(T(S_READING), 22), 2, kWhite);
    if (speed > 0.05 && total > 0) {
      text_center(cv, kCenter, 324, mib_text(speed) + (g_ui == UI_RU ? "/с" : "/s"), 2, kMuted);
      text_center(cv, kCenter, 348, fit(format(T(S_LEFT), duration_text((total - done) / speed).c_str()), 22), 2, kMuted);
    }
    const float pulse = 0.65f + 0.35f * std::sin(time * 3.0f);
    text_center(cv, kCenter, 386, fit(T(S_DONT_TURN_OFF), 22), 2, kPink, pulse);
    if (!cancelling) hints(cv, 430, {{"B", T(S_CANCEL)}});
  }

  void draw_done(Canvas& cv) {
    text_center(cv, kCenter, 140, T(S_DONE), 5, kGreen);
    text_center(cv, kCenter, 194, fit(T(S_PAK_CREATED), 22), 2, kWhite);
    text_center(cv, kCenter, 230, fit(converted_name, 22), 2, kMuted);
    text_center(cv, kCenter, 254, fit(format(T(S_IN), duration_text(took).c_str()), 22), 2, kMuted);
    text_center(cv, kCenter, 278, fit(std::string(T(S_LANGUAGE)) + kLanguages[language].name, 22), 2, kWhite);
    if (deleted) text_center(cv, kCenter, 298, fit(T(S_DISC_DELETED), 22), 2, kMuted);
    text_center(cv, kCenter, 330, fit(T(S_BLOOMED), 22), 2, kPink);
    if (auto_quit) {
      hints(cv, 430, {{"A", T(S_PLAY_NOW)}});
    } else {
      hints(cv, 430, {{"A", T(S_EXIT)}});
    }
  }

  void draw_viva(Canvas& cv) {
    const int g = std::clamp(language, 0, 4);
    const u32 flag[3] = {0x3fd36a, 0xffffff, 0xff5a5a};
    const std::vector<std::string> lines = wrap(kViva[g][0], 14);
    int y = 124;
    for (size_t i = 0; i < lines.size() && i < 4; ++i) { text_center(cv, kCenter, y, lines[i], 3, flag[i % 3]); y += 30; }
    y += 8;
    for (const std::string& l : wrap(kViva[g][1], 22)) { text_center(cv, kCenter, y, l, 2, kGold); y += 20; }
    // The author's logo, centred in what is left above the button, in every language.
    const int logo_top = y + std::max(4, (424 - y - kLogoSize) / 2);
    draw_seal(cv, kCenter - kLogoSize / 2, logo_top);
    hints(cv, 434, {{"A", kViva[g][2]}});  // in the game's language too; only A starts the game
  }

  void draw_failed(Canvas& cv) {
    text_center(cv, kCenter, 132, T(S_OUCH), 5, kRed);
    if (!failed_mod) {  // game.pak itself (a translation's mod failing leaves game.pak as it was)
      text_center(cv, kCenter, 184, fit(T(S_COULD_NOT), 22), 2, kWhite);
      text_center(cv, kCenter, 204, "game.pak", 2, kWhite);
    }
    int y = 240;
    std::vector<std::string> lines = wrap(error, 22);
    if (lines.size() > 7) { lines.resize(7); lines.back() = fit(lines.back() + " xx", 21); }
    for (const std::string& l : lines) { text(cv, kLeft + 4, y, l, 2, 0xffb4a8); y += 22; }
    hints(cv, 430, {{"A", T(S_BACK)}, {"B", T(S_EXIT)}});
  }

  // Demo values (--demo).
  double demo_percent = -1, demo_done = 0, demo_total = 0, demo_speed = 0;
  int demo_patched = -1;
  float demo_growth = -1, demo_bloom = 0, demo_wilt = 0;
};

std::string dir_of_exe() {
  char buf[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
  return std::string(buf).substr(0, std::string(buf).rfind('/'));
}

void save_png(const Canvas& cv, const std::string& path) {
  std::vector<unsigned char> rgb(W * H * 3);
  for (int i = 0; i < W * H; ++i) {
    rgb[i * 3] = (unsigned char)(cv.px[i] >> 16); rgb[i * 3 + 1] = (unsigned char)(cv.px[i] >> 8); rgb[i * 3 + 2] = (unsigned char)cv.px[i];
  }
  stbi_write_png(path.c_str(), W, H, 3, rgb.data(), W * 3);
}

int demo(const std::string& out) {
  mkdir(out.c_str(), 0755);
  Scene scene;
  Canvas cv;
  App app;
  app.free_mib = 12650;
  app.identified = true;
  Image a; a.name = "Super Mario Galaxy (USA).rvz"; a.format = "RVZ"; a.mib = 1693; a.status = 0; a.id = "RMGE01";
  Image b; b.name = "Mario Kart Wii (USA).rvz"; b.format = "RVZ"; b.mib = 2870; b.status = 1; b.id = "RMCE01";
  Image c; c.name = "smg-copia.iso"; c.format = "ISO"; c.mib = 4482; c.status = -1;
  Image d; d.name = "notas.iso"; d.mib = 1; d.status = 3;
  app.images = {a, b, c, d};
  app.patch_files[kPtBr - kPatchedFirst] = "patches/pt-br.xdelta";  // the demo shows the translations
  app.patch_files[kRu - kPatchedFirst] = "patches/smg-ru.xdelta";
  app.load_language();  // no smg/idioma.txt here: the game's language follows the interface's
  app.screen = Screen::NeedDisc;
  app.draw(cv, scene, 0.9f);
  save_png(cv, out + "/0-falta-disco.png");
  app.screen = Screen::Menu;
  app.draw(cv, scene, 1.3f);
  save_png(cv, out + "/1-menu.png");
  app.pending = a;
  app.screen = Screen::Language;
  app.language_choice = app.language;
  app.draw(cv, scene, 1.8f);
  save_png(cv, out + "/1b-idioma.png");
  app.language_choice = kPtBr;
  app.draw(cv, scene, 2.0f);
  save_png(cv, out + "/1c-idioma-ptbr.png");
  app.language_choice = kRu;
  app.draw(cv, scene, 2.1f);
  save_png(cv, out + "/1c-idioma-ru.png");
  const int saved_menu_language = app.language;
  app.have_pak = true;  // game.pak already made: its languages, PT-BR to reinstall with the patch
  app.show_installed_languages();
  app.draw(cv, scene, 2.2f);
  save_png(cv, out + "/1d-idiomas-con-game-pak.png");
  app.language_choice = kPtBr;
  app.draw(cv, scene, 2.4f);
  save_png(cv, out + "/1e-reinstalar-ptbr.png");
  app.screen = Screen::Settings;  // X: resolution and vibration
  app.settings_row = 0;
  app.draw(cv, scene, 2.5f);
  save_png(cv, out + "/1g-ajustes.png");
  app.opt_resolution = 0;
  app.draw(cv, scene, 2.52f);
  save_png(cv, out + "/1g2-ajustes-auto.png");
  app.opt_resolution = 3;
  app.draw(cv, scene, 2.53f);
  save_png(cv, out + "/1g3-ajustes-100.png");
  app.opt_resolution = 2;
  app.settings_row = 1; app.opt_vibration = false;
  app.draw(cv, scene, 2.55f);
  save_png(cv, out + "/1h-ajustes-vibracion.png");
  app.opt_vibration = true;
  app.settings_row = 2; app.opt_custom_controls = true;
  app.draw(cv, scene, 2.56f);
  save_png(cv, out + "/1i-ajustes-mandos.png");
  app.mods = {{"Mod de prueba con nombre largo", true}, {"Otro mod", false}, {"Tercero", true}};
  app.settings_row = 3;
  app.draw(cv, scene, 2.565f);
  save_png(cv, out + "/1i2-ajustes-mods.png");
  app.screen = Screen::Mods; app.mods_row = 1;
  app.draw(cv, scene, 2.566f);
  save_png(cv, out + "/1i3-mods.png");
  app.mods.clear(); app.mods_row = 0;
  app.draw(cv, scene, 2.567f);
  save_png(cv, out + "/1i4-mods-vacio.png");
  app.screen = Screen::Settings; app.settings_row = 2;
  for (int& m : app.controls_map) m = -1;
  app.controls_map[0] = kEast; app.controls_map[1] = kSouth; app.controls_map[4] = kR2;
  app.screen = Screen::Controls; app.controls_row = 2; app.capturing = false;
  app.draw(cv, scene, 2.57f);
  save_png(cv, out + "/1j-mandos.png");
  app.capturing = true;
  app.draw(cv, scene, 2.58f);
  save_png(cv, out + "/1k-mandos-captura.png");
  app.capturing = false; app.controls_row = 14;
  app.draw(cv, scene, 2.59f);
  save_png(cv, out + "/1l-mandos-abajo.png");
  app.opt_custom_controls = false; app.controls_row = 0;
  app.draw(cv, scene, 2.6f);
  save_png(cv, out + "/1m-mandos-predeterminado.png");
  app.screen = Screen::Language;
  app.pak_patch = kRu; app.pak_patched_slot = 1;  // a game.pak made with the Russian patch, in English's slot
  app.language = kRu;
  app.show_installed_languages();
  app.draw(cv, scene, 2.6f);
  save_png(cv, out + "/1f-idiomas-game-pak-ru.png");
  app.pak_patch = -1; app.pak_patched_slot = -1;
  app.language = saved_menu_language;
  app.have_pak = false;
  app.pending = a;
  app.screen = Screen::Converting;
  app.converted_name = a.name;
  app.demo_percent = 37; app.demo_done = 1251; app.demo_total = 3381; app.demo_speed = 14.8;
  app.demo_growth = 0.08f + 0.92f * 0.37f;
  app.draw(cv, scene, 2.1f);
  save_png(cv, out + "/2-convirtiendo-37.png");
  app.demo_percent = 84; app.demo_done = 2840; app.demo_total = 3381; app.demo_speed = 15.6;
  app.demo_growth = 0.08f + 0.92f * 0.84f;
  app.draw(cv, scene, 3.4f);
  save_png(cv, out + "/3-convirtiendo-84.png");
  app.demo_patched = kRu;
  app.draw(cv, scene, 3.5f);
  save_png(cv, out + "/3b-convirtiendo-ru.png");
  app.demo_patched = -1;
  app.screen = Screen::Done; app.took = 231; app.auto_quit = true;
  app.demo_growth = 1.0f; app.demo_bloom = 1.0f;
  app.draw(cv, scene, 4.0f);
  save_png(cv, out + "/4-listo.png");
  const int saved_language = app.language;
  app.language = kPtBr;
  app.draw(cv, scene, 4.2f);
  save_png(cv, out + "/4b-listo-ptbr.png");
  app.language = kRu;
  app.draw(cv, scene, 4.3f);
  save_png(cv, out + "/4c-listo-ru.png");
  app.screen = Screen::Viva;
  app.demo_bloom = 1.0f;
  for (int g = 0; g < 5; ++g) {
    app.language = g;
    app.draw(cv, scene, 4.6f + float(g) * 0.3f);
    save_png(cv, out + "/7-viva-" + std::string(kLanguages[g].key) + ".png");
  }
  app.screen = Screen::Done;
  app.language = saved_language;
  app.screen = Screen::Failed;
  app.error = translate_error("no hay espacio para game.pak: hacen falta 3390 MiB libres y hay 2104 MiB");
  app.demo_growth = 0.45f; app.demo_bloom = 0; app.demo_wilt = 1.0f;
  app.draw(cv, scene, 5.0f);
  save_png(cv, out + "/5-error.png");
  app.error = T(S_ERR_MOD);
  app.failed_mod = true;
  app.draw(cv, scene, 5.1f);
  app.failed_mod = false;
  save_png(cv, out + "/5b-error-parche.png");
  // The growing cactus, frame by frame (for an animation).
  app.screen = Screen::Converting; app.demo_wilt = 0;
  for (int f = 0; f <= 90; ++f) {
    const double p = std::min(100.0, f * 100.0 / 80.0);
    app.demo_percent = p; app.demo_done = 3381 * p / 100; app.demo_total = 3381; app.demo_speed = 15.2;
    app.demo_growth = float(0.08 + 0.92 * p / 100);
    if (f > 80) { app.screen = Screen::Done; app.demo_growth = 1; app.demo_bloom = std::min(1.0f, float(f - 80) / 6.0f); }
    app.draw(cv, scene, float(f) / 15.0f);
    char name[64];
    snprintf(name, sizeof(name), "/frame-%03d.png", f);
    save_png(cv, out + name);
  }
  printf("capturas en %s\n", out.c_str());
  return 0;
}

#ifndef CACTUS_NO_SDL
// PortMaster's controller database too (NEBULA_GAMECONTROLLERDB from the launcher), for firmwares whose own
// SDL_GAMECONTROLLERCONFIG lacks this pad.
void add_portmaster_mappings() {
  if (const char* db = getenv("NEBULA_GAMECONTROLLERDB"); db != nullptr && db[0] != '\0') SDL_GameControllerAddMappingsFromFile(db);
}
#endif

#ifndef CACTUS_NO_SDL
bool select_held() {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) return false;
  add_portmaster_mappings();
  std::vector<SDL_GameController*> pads;
  for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) pads.push_back(SDL_GameControllerOpen(i));
  bool held = false;
  const double until = now_seconds() + 0.35;
  while (!held && now_seconds() < until) {
    SDL_GameControllerUpdate();
    for (SDL_GameController* p : pads) {
      if (p && (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK) ||
                SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))) held = true;
    }
    SDL_Delay(15);
  }
  for (SDL_GameController* p : pads) if (p) SDL_GameControllerClose(p);
  SDL_Quit();
  return held;
}

int run_ui(App& app) {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) == 0) add_portmaster_mappings();
  if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    fprintf(stderr, "[cactus] SDL_Init: %s\n", SDL_GetError());
    return 3;
  }
  SDL_DisplayMode mode{};
  const bool have_mode = SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0;
  SDL_Window* window = SDL_CreateWindow("Cactus Galaxy Wrapper", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        have_mode ? mode.w : W, have_mode ? mode.h : H,
                                        getenv("CACTUS_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN);
  if (window == nullptr) { fprintf(stderr, "[cactus] ventana: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (renderer == nullptr) { fprintf(stderr, "[cactus] renderer: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
  SDL_ShowCursor(SDL_DISABLE);
  SDL_RenderSetLogicalSize(renderer, W, H);
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
  std::vector<SDL_GameController*> pads;
  for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) pads.push_back(SDL_GameControllerOpen(i));
  // The RG35XX H's SDL mapping is positional: its A (right) is SDL's B, its B (bottom) SDL's A.
  // ...unless the firmware maps them by label (NEBULA_FACE_LAYOUT=labels, from the launcher's profile).
  const char* face = getenv("NEBULA_FACE_LAYOUT");
  const bool swap = getenv("CACTUS_SWAP_AB") != nullptr || (face != nullptr && strcmp(face, "labels") == 0);
  const bool face_labels = face != nullptr && strcmp(face, "labels") == 0;  // as the game reads the face buttons
  // The firmware's quit chord with Start: MENU (KNULLI, muOS), SELECT (ArkOS, dArkOS) or both.
  const char* combo = getenv("NEBULA_QUIT_COMBO");
  const bool quit_menu = combo == nullptr || strcmp(combo, "select") != 0;
  const bool quit_select = combo != nullptr && (strcmp(combo, "select") == 0 || strcmp(combo, "both") == 0);
  bool select = false;
  Scene scene;
  Canvas cv;
  const double start = now_seconds();
  bool guide = false;
  // Tests: CACTUS_TEST_KEYS="next,confirm" presses those keys, one each half second once the discs are checked.
  std::vector<App::Key> test_keys;
  if (const char* keys = getenv("CACTUS_TEST_KEYS")) {
    std::string all = keys;
    for (size_t at = 0; at <= all.size();) {
      const size_t comma = std::min(all.find(',', at), all.size());
      const std::string k = all.substr(at, comma - at);
      if (k == "confirm") test_keys.push_back(App::kConfirm);
      if (k == "back") test_keys.push_back(App::kBack);
      if (k == "up") test_keys.push_back(App::kUp);
      if (k == "down") test_keys.push_back(App::kDown);
      if (k == "prev") test_keys.push_back(App::kPrev);
      if (k == "next") test_keys.push_back(App::kNext);
      if (k == "wait") test_keys.push_back(App::Key(-1));
      at = comma + 1;
    }
  }
  double next_test_key = 0;
  while (!app.quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) app.quit = true;
      if (e.type == SDL_CONTROLLERDEVICEADDED) pads.push_back(SDL_GameControllerOpen(e.cdevice.which));
      if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
        const int b = e.cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_GUIDE) guide = down;
        if (b == SDL_CONTROLLER_BUTTON_BACK) select = down;
        if (!down) continue;
        if (b == SDL_CONTROLLER_BUTTON_START && ((guide && quit_menu) || (select && quit_select))) {
          app.result = 1; app.quit = true; continue;  // the quit chord: out, no game
        }
        if (app.capturing) {  // the custom controls: this button goes to the Wii button chosen (START: never mind)
          if (b == SDL_CONTROLLER_BUTTON_START) { app.capturing = false; continue; }
          static const int kFace[4] = {kSouth, kEast, kWest, kNorth};  // SDL A, B, X, Y as the game reads them
          int phys = -1;
          if (b >= SDL_CONTROLLER_BUTTON_A && b <= SDL_CONTROLLER_BUTTON_Y) {
            phys = kFace[b - SDL_CONTROLLER_BUTTON_A];
            if (face_labels) phys = phys == kSouth ? kEast : phys == kEast ? kSouth : phys == kWest ? kNorth : kWest;
          }
          if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) phys = kL1;
          if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) phys = kR1;
          if (b == SDL_CONTROLLER_BUTTON_LEFTSTICK) phys = kL3;
          if (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK) phys = kR3;
          if (b == SDL_CONTROLLER_BUTTON_BACK) phys = kSelectBtn;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) phys = kDUp;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) phys = kDDown;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) phys = kDLeft;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) phys = kDRight;
          if (phys >= 0) app.capture(phys);
          continue;
        }
        if (b == SDL_CONTROLLER_BUTTON_START) app.press(App::kStart);
        if (b == SDL_CONTROLLER_BUTTON_BACK) app.press(App::kMap);
        if (b == (swap ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)) app.press(App::kConfirm);
        if (b == (swap ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)) app.press(App::kBack);
        if (b == (swap ? SDL_CONTROLLER_BUTTON_X : SDL_CONTROLLER_BUTTON_Y)) app.press(App::kToggle);  // X (top)
        if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) app.press(App::kUp);
        if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) app.press(App::kDown);
        if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) app.press(App::kPrev);
        if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) app.press(App::kNext);
      }
      if (e.type == SDL_CONTROLLERAXISMOTION && app.capturing && e.caxis.value > 20000 &&
          (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        app.capture(e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kL2 : kR2);  // L2/R2 as the game's ZL/ZR
      }
      if (e.type == SDL_KEYDOWN) {  // gptokeyb or a keyboard
        switch (e.key.keysym.sym) {
          case SDLK_RETURN: case SDLK_a: app.press(App::kConfirm); break;
          case SDLK_ESCAPE: case SDLK_b: case SDLK_BACKSPACE: app.press(App::kBack); break;
          case SDLK_x: app.press(App::kToggle); break;
          case SDLK_s: app.press(App::kStart); break;
          case SDLK_m: case SDLK_TAB: app.press(App::kMap); break;
          case SDLK_UP: app.press(App::kUp); break;
          case SDLK_DOWN: app.press(App::kDown); break;
          case SDLK_LEFT: app.press(App::kPrev); break;
          case SDLK_RIGHT: app.press(App::kNext); break;
          default: break;
        }
      }
    }
    if (!test_keys.empty() && app.identified && now_seconds() > next_test_key) {
      if (next_test_key > 0) {
        if (int(test_keys.front()) >= 0) app.press(test_keys.front());
        test_keys.erase(test_keys.begin());
      }
      next_test_key = now_seconds() + 0.5;
    }
    app.tick();
    app.draw(cv, scene, float(now_seconds() - start));
    SDL_UpdateTexture(texture, nullptr, cv.px.data(), W * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_Delay(app.screen == Screen::Converting ? 60 : 30);  // leave the CPU to nebula-pak while it works
  }
  if (app.job && !app.job->finished) { kill(app.job->pid, SIGTERM); }
  if (app.job) { app.job->thread.join(); }
  for (SDL_GameController* p : pads) if (p) SDL_GameControllerClose(p);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  App app;
  app.smg = dir_of_exe();
  std::string demo_dir;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--demo" && i + 1 < argc) demo_dir = argv[++i];
    else if (a == "--lang" && i + 1 < argc) setenv("CACTUS_LANG", argv[++i], 1);
    else if (a == "--smg" && i + 1 < argc) app.smg = argv[++i];
    else if (a == "--auto" || a == "--setup") { app.auto_mode = true; app.auto_quit = true; }
    else if (a == "--held") return select_held() ? 0 : 1;
    else if (a == "--delete-image") app.delete_after = true;
    else { fprintf(stderr, "uso: cactus-galaxy-wrapper [--smg <carpeta smg>] [--auto] [--delete-image] [--lang es|en|fr|pt|it|de] | --demo <carpeta>\n"); return 2; }
  }
  const std::string ui_source = detect_ui_language();
  printf("[cactus] idioma de la interfaz: %s (%s)\n", kUiCodes[g_ui], ui_source.c_str());
  if (!demo_dir.empty()) return demo(demo_dir);
  app.gamedata = app.smg + "/gamedata";
  mkdir(app.gamedata.c_str(), 0755);
  app.output = app.gamedata + "/game.pak";
  app.language_file = app.smg + "/idioma.txt";
  app.options_file = app.smg + "/opciones.cfg";
  app.load_options();
  app.controls_file = app.smg + "/mandos.cfg";
  app.load_controls();
  app.pak_language_file = app.output + ".idioma";
  app.viva_file = app.smg + "/.primer-arranque";
  {  // a game.pak made before this version: the game has started already, no send-off
    struct stat st;
    if (stat(app.output.c_str(), &st) == 0 && st.st_size > 0 && access(app.viva_file.c_str(), F_OK) != 0) {
      if (FILE* f = fopen(app.viva_file.c_str(), "w")) { fputs("1\n", f); fclose(f); }
    }
  }
  app.find_patch();
  app.refresh_disk();
  app.load_language();
  app.tool = dir_of_exe() + "/nebula-pak";
  if (access(app.tool.c_str(), X_OK) != 0) app.tool = app.smg + "/nebula-pak";
  app.ruso_tool = dir_of_exe() + "/cactus-ruso";
  if (access(app.ruso_tool.c_str(), X_OK) != 0) app.ruso_tool = app.smg + "/cactus-ruso";
  std::string ports = app.smg.substr(0, app.smg.rfind('/'));
  std::string roms = ports.substr(0, ports.rfind('/'));
  app.scan_dirs = {app.gamedata, app.smg, roms + "/wii", "/userdata/roms/wii", "/roms/wii", "/roms2/wii", "/storage/roms/wii"};
  app.images = find_images(app.scan_dirs);
  app.refresh_disk();
  app.auto_quit = true;
  // The first screen: the game's languages if game.pak is there, the discs to install from, or a request for one.
  if (app.have_pak) app.show_installed_languages();
  else if (app.images.empty()) app.screen = Screen::NeedDisc;
  else app.screen = Screen::Menu;
  printf("[cactus] Cactus Galaxy Wrapper: %zu imagen(es), destino %s\n", app.images.size(), app.output.c_str());
  app.start_identify();
#ifndef CACTUS_NO_SDL
  const int ui = run_ui(app);
#else
  const int ui = 3;
#endif
  if (app.identify_thread.joinable()) app.identify_thread.join();
  if (ui == 3) return 3;
  app.refresh_disk();
  return app.result == 0 && app.have_pak ? 0 : 1;
}
