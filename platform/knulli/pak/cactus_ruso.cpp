// cactus-ruso: the Russian translation of Super Mario Galaxy made for the Switch release (СаХаР and N69's
// adaptation of reWiision's Wii translation), brought to the player's own Wii game (USA, RMGE01) as a Nebula mod.
//
//   cactus-ruso <traducción.zip o su carpeta> <game.pak> <carpeta del mod>
//
// The Switch files are little-endian and their fonts are the Switch's high-resolution ones (some 55 MB unpacked;
// the game keeps its fonts in a 20 MB heap), so nothing is copied as is:
// - text: the game's own Message.arc (from game.pak) with each message the translation has, by its name, in Russian
//   (its tags turned big-endian); the messages it lacks stay in English, and the flow and attributes are the disc's;
// - fonts: the game's own Font.arc with the characters the Russian text needs and the font lacks (Cyrillic, «», —...)
//   added from the translation's font of the same name, scaled down to the game's cell size, in new sheets.
// The result is a mod folder (files/<the disc's paths>), written beside and renamed into place when complete.
// Nothing of the translation is shipped with the port: the player brings it. This file: GPL-3.0-or-later, as Nebula.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zlib.h"

namespace {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using Bytes = std::vector<u8>;

[[noreturn]] void fail(const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  throw std::runtime_error(text);
}

std::string lower(std::string s) {
  for (char& c : s) c = char(tolower((unsigned char)c));
  return s;
}
bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

u16 rd16(const u8* p, bool be) { return be ? u16(p[0] << 8 | p[1]) : u16(p[1] << 8 | p[0]); }
u32 rd32(const u8* p, bool be) {
  return be ? u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]
            : u32(p[3]) << 24 | u32(p[2]) << 16 | u32(p[1]) << 8 | p[0];
}
void wr16(u8* p, u16 v) { p[0] = u8(v >> 8); p[1] = u8(v); }
void wr32(u8* p, u32 v) { p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v); }
void put16(Bytes& b, u16 v) { b.push_back(u8(v >> 8)); b.push_back(u8(v)); }
void put32(Bytes& b, u32 v) { put16(b, u16(v >> 16)); put16(b, u16(v)); }
void pad_to(Bytes& b, size_t align) { while (b.size() % align) b.push_back(0); }

Bytes read_file(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) fail("no se puede abrir %s: %s", path.c_str(), strerror(errno));
  Bytes out;
  u8 buf[1 << 16];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
  fclose(f);
  return out;
}

void make_dirs(const std::string& path) {
  for (size_t at = 1; at <= path.size(); ++at) {
    if (at == path.size() || path[at] == '/') {
      const std::string part = path.substr(0, at);
      if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) fail("no se puede crear %s: %s", part.c_str(), strerror(errno));
    }
  }
}

void write_file(const std::string& path, const Bytes& data) {
  make_dirs(path.substr(0, path.rfind('/')));
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) fail("no se puede escribir %s: %s", path.c_str(), strerror(errno));
  const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  if (fclose(f) != 0 || !ok) fail("no se pudo escribir %s (¿falta espacio?)", path.c_str());
}

void remove_tree(const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return;
  if (S_ISDIR(st.st_mode)) {
    if (DIR* d = opendir(path.c_str())) {
      while (dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) remove_tree(path + "/" + e->d_name);
      }
      closedir(d);
    }
    rmdir(path.c_str());
  } else {
    unlink(path.c_str());
  }
}

// ---------------------------------------------------------------------------------------------------------------
// The translation: a .zip as downloaded (stored or deflated entries) or its extracted folder.

struct Translation {
  std::string root;
  bool zip = false;
  Bytes archive;
  struct Entry { std::string name; u32 method, csize, usize, local; };
  std::vector<Entry> entries;  // zip entries, or files under the folder (name relative)

  explicit Translation(const std::string& path) : root(path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) fail("no se encuentra %s", path.c_str());
    if (S_ISDIR(st.st_mode)) {
      walk("");
      return;
    }
    zip = true;
    archive = read_file(path);
    const Bytes& z = archive;
    if (z.size() < 22) fail("%s no es un .zip", path.c_str());
    size_t eocd = std::string::npos;
    for (size_t at = z.size() - 22 + 1; at-- > 0 && z.size() - at < 0x10000 + 22;) {
      if (rd32(&z[at], false) == 0x06054b50) { eocd = at; break; }
    }
    if (eocd == std::string::npos) fail("%s no es un .zip (o está incompleto)", path.c_str());
    const u32 count = rd16(&z[eocd + 10], false), cd = rd32(&z[eocd + 16], false);
    size_t at = cd;
    for (u32 i = 0; i < count; ++i) {
      if (at + 46 > z.size() || rd32(&z[at], false) != 0x02014b50) fail("el .zip está dañado");
      Entry e;
      e.method = rd16(&z[at + 10], false);
      e.csize = rd32(&z[at + 20], false);
      e.usize = rd32(&z[at + 24], false);
      const u32 n = rd16(&z[at + 28], false), x = rd16(&z[at + 30], false), c = rd16(&z[at + 32], false);
      e.local = rd32(&z[at + 42], false);
      e.name.assign(reinterpret_cast<const char*>(&z[at + 46]), n);
      entries.push_back(e);
      at += 46 + n + x + c;
    }
  }
  void walk(const std::string& relative) {
    DIR* d = opendir((root + "/" + relative).c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      const std::string name = relative.empty() ? std::string(e->d_name) : relative + "/" + e->d_name;
      struct stat st;
      if (stat((root + "/" + name).c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) walk(name);
      else entries.push_back({name, 0, 0, u32(st.st_size), 0});
    }
    closedir(d);
  }
  // The file whose path ends in `suffix` (lowercase, '/' separated); a language folder in `prefer` first.
  bool find(const std::string& suffix, const std::vector<std::string>& prefer, Bytes& out, std::string* found) const {
    const Entry* best = nullptr;
    size_t best_rank = ~size_t(0);
    for (const Entry& e : entries) {
      const std::string l = lower(e.name);
      if (!ends_with(l, suffix) || ends_with(l, "/")) continue;
      size_t rank = prefer.size();
      for (size_t i = 0; i < prefer.size(); ++i) if (l.find(prefer[i]) != std::string::npos) { rank = i; break; }
      if (rank < best_rank) { best = &e; best_rank = rank; }
    }
    if (!best) return false;
    if (found) *found = best->name;
    if (!zip) { out = read_file(root + "/" + best->name); return true; }
    const Bytes& z = archive;
    if (best->local + 30 > z.size() || rd32(&z[best->local], false) != 0x04034b50) fail("el .zip está dañado");
    const size_t data = best->local + 30 + rd16(&z[best->local + 26], false) + rd16(&z[best->local + 28], false);
    if (data + best->csize > z.size()) fail("el .zip está incompleto");
    if (best->method == 0) { out.assign(z.begin() + data, z.begin() + data + best->csize); return true; }
    if (best->method != 8) fail("%s usa una compresión del .zip que no se puede leer (%u)", best->name.c_str(), best->method);
    out.assign(best->usize, 0);
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) fail("zlib");
    s.next_in = const_cast<u8*>(&z[data]);
    s.avail_in = best->csize;
    s.next_out = out.data();
    s.avail_out = u32(out.size());
    const int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END || s.total_out != out.size()) fail("no se pudo descomprimir %s del .zip", best->name.c_str());
    return true;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// Yaz0.

bool is_yaz0(const Bytes& d) { return d.size() >= 16 && memcmp(d.data(), "Yaz0", 4) == 0; }

Bytes yaz0_decode(const Bytes& d) {
  const u32 size = rd32(&d[4], true);
  Bytes out;
  out.reserve(size);
  size_t i = 16;
  while (out.size() < size) {
    if (i >= d.size()) fail("un archivo Yaz0 está incompleto");
    const u8 code = d[i++];
    for (int b = 0; b < 8 && out.size() < size; ++b) {
      if (code & (0x80 >> b)) {
        if (i >= d.size()) fail("un archivo Yaz0 está incompleto");
        out.push_back(d[i++]);
      } else {
        if (i + 1 >= d.size()) fail("un archivo Yaz0 está incompleto");
        const u8 b1 = d[i], b2 = d[i + 1];
        i += 2;
        const size_t dist = (size_t(b1 & 0xf) << 8 | b2) + 1;
        size_t n = b1 >> 4;
        if (n == 0) { if (i >= d.size()) fail("un archivo Yaz0 está incompleto"); n = size_t(d[i++]) + 0x12; } else n += 2;
        if (dist > out.size()) fail("un archivo Yaz0 está dañado");
        const size_t from = out.size() - dist;
        for (size_t k = 0; k < n && out.size() < size; ++k) out.push_back(out[from + k]);
      }
    }
  }
  return out;
}

// Greedy, with hash chains over the 4 KiB window (what the game reads; not as tight as Nintendo's).
Bytes yaz0_encode(const Bytes& src) {
  Bytes out = {'Y', 'a', 'z', '0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  wr32(&out[4], u32(src.size()));
  const size_t n = src.size();
  std::vector<int32_t> head(1 << 15, -1), prev(n, -1);
  auto hash = [&](size_t p) { return ((src[p] << 10) ^ (src[p + 1] << 5) ^ src[p + 2]) & 0x7fff; };
  size_t p = 0;
  while (p < n) {
    const size_t code_at = out.size();
    out.push_back(0);
    for (int b = 0; b < 8 && p < n; ++b) {
      size_t best_len = 0, best_dist = 0;
      if (p + 3 <= n) {
        int chain = 48;
        for (int32_t c = head[hash(p)]; c >= 0 && p - size_t(c) <= 0x1000 && chain-- > 0; c = prev[c]) {
          const size_t max = std::min<size_t>(0x111, n - p);
          size_t len = 0;
          while (len < max && src[size_t(c) + len] == src[p + len]) ++len;
          if (len > best_len) { best_len = len; best_dist = p - size_t(c); if (len == max) break; }
        }
      }
      const size_t step = best_len >= 3 ? best_len : 1;
      if (best_len >= 3) {
        const size_t d = best_dist - 1;
        if (best_len >= 0x12) {
          out.push_back(u8(d >> 8));
          out.push_back(u8(d));
          out.push_back(u8(best_len - 0x12));
        } else {
          out.push_back(u8((best_len - 2) << 4 | d >> 8));
          out.push_back(u8(d));
        }
      } else {
        out[code_at] |= u8(0x80 >> b);
        out.push_back(src[p]);
      }
      for (size_t k = 0; k < step; ++k, ++p) {
        if (p + 3 <= n) { const int h = hash(p); prev[p] = head[h]; head[h] = int32_t(p); }
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// RARC (JKRArchive), big-endian (Wii) or little-endian (the Switch's "CRAR").

struct Rarc {
  Bytes d;
  bool be = true;
  u32 data_start = 0;
  struct File { std::string path; size_t entry; u32 offset, size; };
  std::vector<File> files;

  explicit Rarc(Bytes bytes) : d(std::move(bytes)) {
    if (d.size() < 0x40) fail("archivo RARC demasiado corto");
    if (!memcmp(d.data(), "RARC", 4)) be = true;
    else if (!memcmp(d.data(), "CRAR", 4)) be = false;
    else fail("no es un archivo RARC");
    data_start = rd32(&d[12], be) + 0x20;
    const u32 node_off = rd32(&d[0x24], be) + 0x20;
    walk(0, "", node_off, 0);
  }
  u32 entry_off() const { return rd32(&d[0x2C], be) + 0x20; }
  u32 str_off() const { return rd32(&d[0x34], be) + 0x20; }
  std::string name_at(u32 o) const {
    const size_t at = size_t(str_off()) + o;
    if (at >= d.size()) fail("RARC dañado");
    return std::string(reinterpret_cast<const char*>(&d[at]), strnlen(reinterpret_cast<const char*>(&d[at]), d.size() - at));
  }
  void walk(u32 node, const std::string& path, u32 node_off, int depth) {
    if (depth > 32) fail("RARC dañado");
    const u8* n = &d[node_off + node * 16];
    const u16 count = rd16(n + 10, be);
    const u32 first = rd32(n + 12, be);
    for (u32 k = 0; k < count; ++k) {
      const size_t eo = entry_off() + (first + k) * 20;
      if (eo + 20 > d.size()) fail("RARC dañado");
      const u32 w = rd32(&d[eo + 4], be);
      const u32 flags = w >> 24;
      const u32 data = rd32(&d[eo + 8], be), size = rd32(&d[eo + 12], be);
      const std::string nm = name_at(w & 0xffff);
      if (nm == "." || nm == "..") continue;
      if (flags & 2) walk(data, path + nm + "/", node_off, depth + 1);
      else files.push_back({path + nm, eo, data, size});
    }
  }
  const File* file(const std::string& name) const {
    const std::string l = lower(name);
    for (const File& f : files) {
      const std::string p = lower(f.path);
      if (p == l || ends_with(p, "/" + l)) return &f;
    }
    return nullptr;
  }
  Bytes contents(const File& f) const {
    if (size_t(data_start) + f.offset + f.size > d.size()) fail("RARC dañado");
    return Bytes(d.begin() + data_start + f.offset, d.begin() + data_start + f.offset + f.size);
  }
  // A big-endian archive (this one's tables) with some files' contents replaced; the data laid out again.
  Bytes rebuilt(const std::map<std::string, Bytes>& replace) const {
    if (!be) fail("solo se reescriben archivos RARC de Wii");
    Bytes out(d.begin(), d.begin() + data_start);
    std::vector<const File*> order;
    for (const File& f : files) order.push_back(&f);
    std::sort(order.begin(), order.end(), [](const File* a, const File* b) { return a->offset < b->offset; });
    u32 total = 0;
    for (const File* f : order) {
      auto it = replace.find(f->path);
      const Bytes data = it != replace.end() ? it->second : contents(*f);
      const u32 offset = u32(out.size() - data_start);
      out.insert(out.end(), data.begin(), data.end());
      pad_to(out, 0x20);
      wr32(&out[f->entry + 8], offset);
      wr32(&out[f->entry + 12], u32(data.size()));
      total = u32(out.size() - data_start);
    }
    const u32 old_total = rd32(&d[0x10], true), old_mram = rd32(&d[0x14], true);
    wr32(&out[4], u32(out.size()));
    wr32(&out[0x10], total);
    wr32(&out[0x14], old_mram + (total - old_total));
    return out;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// JMap tables (BCSV): messageid.tbl, a message's name (a string field) and its index (an int field).

std::map<std::string, u32> message_ids(const Bytes& t, bool be) {
  if (t.size() < 16) fail("messageid.tbl dañado");
  const u32 count = rd32(&t[0], be), fields = rd32(&t[4], be), entries = rd32(&t[8], be), size = rd32(&t[12], be);
  int name_field = -1, index_field = -1;
  for (u32 f = 0; f < fields; ++f) {
    const u8* p = &t[16 + f * 12];
    const u8 type = p[11];
    if (type == 6 && name_field < 0) name_field = int(f);
    if (type == 0 && index_field < 0) index_field = int(f);
  }
  if (name_field < 0 || index_field < 0) fail("messageid.tbl no tiene nombres e índices");
  const size_t strings = size_t(entries) + size_t(count) * size;
  if (strings > t.size()) fail("messageid.tbl dañado");
  auto field = [&](u32 entry, int f) {
    const u8* p = &t[16 + f * 12];
    const u32 mask = rd32(p + 4, be);
    const u16 off = rd16(p + 8, be);
    const u8 shift = p[10];
    return (rd32(&t[entries + entry * size + off], be) & mask) >> shift;
  };
  std::map<std::string, u32> out;
  for (u32 e = 0; e < count; ++e) {
    const size_t at = strings + field(e, name_field);
    if (at >= t.size()) fail("messageid.tbl dañado");
    out[std::string(reinterpret_cast<const char*>(&t[at]), strnlen(reinterpret_cast<const char*>(&t[at]), t.size() - at))] =
        field(e, index_field);
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------
// BMG (message.bmg): UTF-16 text with tags (0x1A, then the tag's size and group bytes, its type and parameters).

struct Bmg {
  Bytes d;
  bool be = true;
  struct Section { char magic[5]; size_t at, size; };
  std::vector<Section> sections;
  size_t inf = 0, dat = 0;
  u16 count = 0, esize = 0;

  explicit Bmg(Bytes bytes) : d(std::move(bytes)) {
    if (d.size() < 0x20) fail("message.bmg dañado");
    if (!memcmp(d.data(), "MESGbmg1", 8)) be = true;
    else if (!memcmp(d.data(), "GSEM1gmb", 8)) be = false;
    else fail("message.bmg no es un BMG");
    if (d[0x10] != 2) fail("message.bmg no está en UTF-16");
    const u32 n = rd32(&d[0xC], be);
    size_t at = 0x20;
    for (u32 i = 0; i < n && at + 8 <= d.size(); ++i) {
      Section s{};
      if (be) memcpy(s.magic, &d[at], 4);
      else for (int k = 0; k < 4; ++k) s.magic[k] = char(d[at + 3 - k]);
      s.at = at;
      s.size = rd32(&d[at + 4], be);
      // The Switch file's last section says a little past its end (padding the file lacks).
      if (s.size >= 8 && at + s.size > d.size() && at + s.size - d.size() <= 0x20) s.size = d.size() - at;
      if (s.size < 8 || at + s.size > d.size()) fail("message.bmg dañado (sección %s)", s.magic);
      if (!strcmp(s.magic, "INF1")) inf = at;
      if (!strcmp(s.magic, "DAT1")) dat = at;
      sections.push_back(s);
      at += s.size;
    }
    if (!inf || !dat) fail("message.bmg sin INF1 o DAT1");
    count = rd16(&d[inf + 8], be);
    esize = rd16(&d[inf + 10], be);
    if (esize < 4 || inf + 16 + size_t(count) * esize > d.size()) fail("message.bmg dañado");
  }
  size_t section_size(size_t at) const { return rd32(&d[at + 4], be); }
  // Message i's text as raw bytes in the file's byte order, up to (not including) its terminating zero.
  Bytes raw(u16 i) const {
    const u32 off = rd32(&d[inf + 16 + size_t(i) * esize], be);
    size_t p = dat + 8 + off;
    const size_t end = dat + section_size(dat);
    const size_t start = p;
    while (p + 1 < end) {
      const u16 c = rd16(&d[p], be);
      if (c == 0) break;
      if (c == 0x1A) {
        const u8 len = d[p + 2];
        if (len < 6 || p + len > end) fail("message.bmg: etiqueta dañada");
        p += len;
      } else {
        p += 2;
      }
    }
    return Bytes(d.begin() + start, d.begin() + p);
  }
};

struct Tag { u8 group; u16 type; Bytes params; };

std::vector<Tag> tags_of(const Bytes& raw, bool be) {
  std::vector<Tag> out;
  for (size_t p = 0; p + 1 < raw.size();) {
    if (rd16(&raw[p], be) == 0x1A) {
      const u8 len = raw[p + 2];
      out.push_back({raw[p + 3], rd16(&raw[p + 4], be), Bytes(raw.begin() + p + 6, raw.begin() + p + len)});
      p += len;
    } else {
      p += 2;
    }
  }
  return out;
}

struct TextStats { u32 tags = 0, matched = 0, guessed = 0; };

// A Switch (little-endian) message turned big-endian. A tag's parameters are taken in the layout under which they
// equal a tag of the same group and type in the disc's own message of that name (as they are, as 16-bit units or
// as 32-bit values); with none, by its kind: names and strings (group 2) as they are, longer ones as 32-bit values,
// the rest as 16-bit units.
Bytes switch_text_to_wii(const Bytes& raw, const std::vector<Tag>& wii_tags, std::set<u16>& codes, TextStats& stats) {
  Bytes out;
  for (size_t p = 0; p + 1 < raw.size();) {
    const u16 c = rd16(&raw[p], false);
    if (c != 0x1A) {
      put16(out, c);
      codes.insert(c);
      p += 2;
      continue;
    }
    const u8 len = raw[p + 2], group = raw[p + 3];
    const u16 type = rd16(&raw[p + 4], false);
    const Bytes params(raw.begin() + p + 6, raw.begin() + p + len);
    p += len;
    Bytes as_is = params, units = params, words = params;
    for (size_t k = 0; k + 1 < units.size(); k += 2) std::swap(units[k], units[k + 1]);
    const bool can_words = params.size() % 4 == 0 && !params.empty();
    if (can_words) for (size_t k = 0; k + 3 < words.size(); k += 4) { std::swap(words[k], words[k + 3]); std::swap(words[k + 1], words[k + 2]); }
    const Bytes* chosen = nullptr;
    for (const Tag& t : wii_tags) {
      if (t.group != group || t.type != type || t.params.size() != params.size()) continue;
      if (t.params == units) { chosen = &units; break; }
      if (can_words && t.params == words) { chosen = &words; break; }
      if (t.params == as_is) { chosen = &as_is; break; }
    }
    ++stats.tags;
    if (chosen) ++stats.matched;
    else {
      ++stats.guessed;
      chosen = group == 2 ? &as_is : (params.size() >= 8 && can_words) ? &words : &units;
    }
    put16(out, 0x1A);
    out.push_back(len);
    out.push_back(group);
    put16(out, type);
    out.insert(out.end(), chosen->begin(), chosen->end());
  }
  return out;
}

struct MessageResult { Bytes arc; std::set<u16> codes; u32 russian = 0, english = 0, only_translation = 0; TextStats tags; };

MessageResult convert_messages(const Rarc& wii_arc, const Rarc& ru_arc) {
  const Rarc::File* wb = wii_arc.file("message.bmg");
  const Rarc::File* wt = wii_arc.file("messageid.tbl");
  const Rarc::File* rb = ru_arc.file("message.bmg");
  const Rarc::File* rt = ru_arc.file("messageid.tbl");
  if (!wb || !wt) fail("el Message.arc del juego no tiene message.bmg o messageid.tbl");
  if (!rb || !rt) fail("el Message.arc de la traducción no tiene message.bmg o messageid.tbl");
  const Bmg wii(wii_arc.contents(*wb)), ru(ru_arc.contents(*rb));
  if (!wii.be) fail("el message.bmg del juego no es de Wii");
  const std::map<std::string, u32> wii_ids = message_ids(wii_arc.contents(*wt), true);
  const std::map<std::string, u32> ru_ids = message_ids(ru_arc.contents(*rt), ru_arc.be);
  std::vector<const std::string*> name_of(wii.count, nullptr);
  for (const auto& [name, index] : wii_ids) if (index < wii.count) name_of[index] = &name;

  MessageResult r;
  std::vector<Bytes> texts(wii.count);
  for (u16 i = 0; i < wii.count; ++i) {
    texts[i] = wii.raw(i);
    if (!name_of[i]) continue;
    auto it = ru_ids.find(*name_of[i]);
    if (it == ru_ids.end() || it->second >= ru.count) { ++r.english; continue; }
    const Bytes ru_text = ru.raw(u16(it->second));
    if (ru_text.empty() && !texts[i].empty()) { ++r.english; continue; }  // untranslated there: the disc's
    texts[i] = switch_text_to_wii(ru_text, tags_of(texts[i], true), r.codes, r.tags);
    ++r.russian;
  }
  for (const auto& [name, index] : ru_ids) if (!wii_ids.count(name)) ++r.only_translation;

  // The disc's file with new INF1 offsets and DAT1 text; the other sections (flow) as they are.
  Bytes dat = {'D', 'A', 'T', '1', 0, 0, 0, 0, 0, 0};  // offset 0: an empty text
  std::vector<u32> offsets(wii.count, 0);
  for (u16 i = 0; i < wii.count; ++i) {
    if (texts[i].empty()) continue;
    offsets[i] = u32(dat.size() - 8);
    dat.insert(dat.end(), texts[i].begin(), texts[i].end());
    put16(dat, 0);
  }
  pad_to(dat, 0x20);
  wr32(&dat[4], u32(dat.size()));
  Bytes bmg(wii.d.begin(), wii.d.begin() + 0x20);
  for (const Bmg::Section& s : wii.sections) {
    if (s.at == wii.dat) {
      bmg.insert(bmg.end(), dat.begin(), dat.end());
      continue;
    }
    const size_t at = bmg.size();
    bmg.insert(bmg.end(), wii.d.begin() + s.at, wii.d.begin() + s.at + s.size);
    if (s.at == wii.inf) for (u16 i = 0; i < wii.count; ++i) wr32(&bmg[at + 16 + size_t(i) * wii.esize], offsets[i]);
  }
  wr32(&bmg[8], u32(bmg.size()));
  r.arc = wii_arc.rebuilt({{wb->path, bmg}});
  return r;
}

// ---------------------------------------------------------------------------------------------------------------
// Fonts (RFNT): the game's font with glyphs added from the translation's font of the same name.

struct Pixel { float i, a; };

struct Font {
  Bytes d;
  bool be = true;
  struct Block { char magic[5]; size_t at, size; };
  std::vector<Block> blocks;
  size_t finf = 0, tglp = 0;
  // TGLP
  u8 cell_w = 0, cell_h = 0, max_w = 0;
  int baseline = 0;
  u32 sheet_size = 0, image = 0;
  u16 sheets = 0, format = 0, cols = 0, rows = 0, sheet_w = 0, sheet_h = 0;
  int ascent = 0, height = 0;

  explicit Font(Bytes bytes) : d(std::move(bytes)) {
    if (d.size() < 0x10) fail("fuente dañada");
    if (!memcmp(d.data(), "RFNT", 4)) be = true;
    else if (!memcmp(d.data(), "TNFR", 4)) be = false;
    else fail("no es una fuente RFNT");
    size_t at = rd16(&d[12], be);
    const u16 n = rd16(&d[14], be);
    for (u16 i = 0; i < n && at + 8 <= d.size(); ++i) {
      Block b{};
      if (be) memcpy(b.magic, &d[at], 4);
      else for (int k = 0; k < 4; ++k) b.magic[k] = char(d[at + 3 - k]);
      b.at = at;
      b.size = rd32(&d[at + 4], be);
      if (b.size < 8 || at + b.size > d.size()) fail("fuente dañada");
      if (!strcmp(b.magic, "FINF")) finf = at;
      if (!strcmp(b.magic, "TGLP")) tglp = at;
      blocks.push_back(b);
      at += b.size;
    }
    if (!finf || !tglp) fail("fuente sin FINF o TGLP");
    const u8* t = &d[tglp + 8];
    cell_w = t[0]; cell_h = t[1]; baseline = int8_t(t[2]); max_w = t[3];
    sheet_size = rd32(t + 4, be); sheets = rd16(t + 8, be); format = rd16(t + 10, be) & 0x7fff;
    cols = rd16(t + 12, be); rows = rd16(t + 14, be); sheet_w = rd16(t + 16, be); sheet_h = rd16(t + 18, be);
    image = rd32(t + 20, be);
    const u8* f = &d[finf + 8];
    height = f[20]; ascent = f[22];
    if (!cols || !rows || u64(image) + u64(sheet_size) * sheets > d.size()) fail("fuente dañada");
  }
  u32 capacity() const { return u32(cols) * rows * sheets; }
  // The glyph index of a character code (0xFFFF if none), as nw4r's ResFontBase looks it up: the first map whose
  // range holds the code decides.
  u16 glyph(u16 c) const {
    u32 map = rd32(&d[finf + 8 + 16], be);
    for (int guard = 0; map && map + 12 <= d.size() && guard < 4096; ++guard) {
      const u8* m = &d[map];
      const u16 lo = rd16(m, be), hi = rd16(m + 2, be), method = rd16(m + 4, be);
      if (lo <= c && c <= hi) {
        if (method == 0) return u16(rd16(m + 12, be) + (c - lo));
        if (method == 1) return rd16(m + 12 + 2 * (c - lo), be);
        const u16 n = rd16(m + 12, be);
        for (u16 k = 0; k < n; ++k) if (rd16(m + 14 + 4 * k, be) == c) return rd16(m + 16 + 4 * k, be);
        return 0xFFFF;
      }
      map = rd32(m + 8, be);
    }
    return 0xFFFF;
  }
  // Glyph widths: left, glyph width, advance.
  bool widths(u16 index, int& left, int& glyph_w, int& advance) const {
    u32 w = rd32(&d[finf + 8 + 12], be);
    for (int guard = 0; w && w + 8 <= d.size() && guard < 4096; ++guard) {
      const u8* p = &d[w];
      const u16 lo = rd16(p, be), hi = rd16(p + 2, be);
      if (lo <= index && index <= hi) {
        const u8* e = p + 8 + 3 * (index - lo);
        left = int8_t(e[0]); glyph_w = e[1]; advance = int8_t(e[2]);
        return true;
      }
      w = rd32(p + 4, be);
    }
    const u8* f = &d[finf + 8];
    left = int8_t(f[4]); glyph_w = f[5]; advance = int8_t(f[6]);
    return false;
  }
  // A pixel of a glyph's cell (intensity, alpha in 0..1).
  Pixel pixel(u16 index, int x, int y) const {
    const u32 per = u32(cols) * rows;
    const u32 sheet = index / per, k = index % per;
    const int X = int(k % cols) * (cell_w + 1) + x, Y = int(k / cols) * (cell_h + 1) + y;
    const u8* s = &d[image + size_t(sheet) * sheet_size];
    return decode(s, format, sheet_w, X, Y);
  }
  static Pixel decode(const u8* s, u16 fmt, int width, int X, int Y) {
    switch (fmt) {
      case 0: {  // I4: 8x8 tiles
        const int t = (Y / 8) * (width / 8) + X / 8;
        const u8 b = s[t * 32 + (Y % 8) * 4 + (X % 8) / 2];
        const float v = float((X & 1) ? (b & 15) : (b >> 4)) / 15.0f;
        return {v, v};
      }
      case 1: {  // I8: 8x4
        const int t = (Y / 4) * (width / 8) + X / 8;
        const float v = s[t * 32 + (Y % 4) * 8 + X % 8] / 255.0f;
        return {v, v};
      }
      case 2: {  // IA4: 8x4, alpha high
        const int t = (Y / 4) * (width / 8) + X / 8;
        const u8 b = s[t * 32 + (Y % 4) * 8 + X % 8];
        return {float(b & 15) / 15.0f, float(b >> 4) / 15.0f};
      }
      case 3: {  // IA8: 4x4, alpha then intensity
        const int t = (Y / 4) * (width / 4) + X / 4;
        const u8* p = &s[t * 32 + ((Y % 4) * 4 + X % 4) * 2];
        return {p[1] / 255.0f, p[0] / 255.0f};
      }
    }
    return {0, 0};
  }
  static bool supported(u16 fmt) { return fmt <= 3; }
  static void encode(u8* s, u16 fmt, int width, int X, int Y, Pixel px) {
    auto q = [](float v, int max) { return std::clamp(int(std::lround(v * max)), 0, max); };
    switch (fmt) {
      case 0: {
        const int t = (Y / 8) * (width / 8) + X / 8;
        u8& b = s[t * 32 + (Y % 8) * 4 + (X % 8) / 2];
        const int v = q(px.a, 15);
        b = (X & 1) ? u8((b & 0xf0) | v) : u8((b & 0x0f) | v << 4);
        break;
      }
      case 1: {
        const int t = (Y / 4) * (width / 8) + X / 8;
        s[t * 32 + (Y % 4) * 8 + X % 8] = u8(q(px.a, 255));
        break;
      }
      case 2: {
        const int t = (Y / 4) * (width / 8) + X / 8;
        s[t * 32 + (Y % 4) * 8 + X % 8] = u8(q(px.a, 15) << 4 | q(px.i, 15));
        break;
      }
      case 3: {
        const int t = (Y / 4) * (width / 4) + X / 4;
        u8* p = &s[t * 32 + ((Y % 4) * 4 + X % 4) * 2];
        p[0] = u8(q(px.a, 255));
        p[1] = u8(q(px.i, 255));
        break;
      }
    }
  }
};

struct FontResult { Bytes data; u32 added = 0; float scale = 0; std::string note; };

// The game's font with `codes` it lacks added from `source` (scaled to the game's cell); new glyph indices after
// the last sheet's capacity, a width block and a scan map for them, the map first in the list.
FontResult add_glyphs(const Font& game, const Font& source, const std::set<u16>& codes) {
  FontResult r;
  if (!game.be) fail("la fuente del juego no es de Wii");
  std::vector<std::pair<u16, u16>> add;  // code, the source's glyph
  for (u16 c : codes) {
    if (c < 0x20 || game.glyph(c) != 0xFFFF) continue;
    const u16 g = source.glyph(c);
    if (g == 0xFFFF || g >= source.capacity()) continue;
    add.push_back({c, g});
  }
  if (add.empty()) { r.data = game.d; return r; }
  if (!Font::supported(game.format)) { r.data = game.d; r.note = "formato de la fuente sin soporte"; return r; }
  if (!Font::supported(source.format)) { r.data = game.d; r.note = "formato de la fuente rusa sin soporte"; return r; }
  const float scale = game.baseline > 0 && source.baseline > 0 ? float(game.baseline) / float(source.baseline)
                      : source.height > 0 && game.height > 0 ? float(game.height) / float(source.height)
                                                             : float(game.cell_h) / float(source.cell_h);
  r.scale = scale;
  const u32 per = u32(game.cols) * game.rows;
  const u32 first = game.capacity();
  const u32 new_sheets = u32((add.size() + per - 1) / per);
  if (first + add.size() > 0xFFF0) { r.data = game.d; r.note = "la fuente ya no admite más letras"; return r; }

  // New sheets.
  Bytes sheets(size_t(new_sheets) * game.sheet_size, 0);
  struct Width { int left, glyph_w, advance; };
  std::vector<Width> widths;
  for (size_t n = 0; n < add.size(); ++n) {
    const u16 g = add[n].second;
    const u32 k = u32(n) % per, sheet = u32(n) / per;
    const int cx = int(k % game.cols) * (game.cell_w + 1), cy = int(k / game.cols) * (game.cell_h + 1);
    u8* s = &sheets[size_t(sheet) * game.sheet_size];
    for (int y = 0; y < game.cell_h; ++y) {
      for (int x = 0; x < game.cell_w; ++x) {
        // The source area this pixel covers, averaged.
        const float x0 = x / scale, x1 = (x + 1) / scale, y0 = y / scale, y1 = (y + 1) / scale;
        float si = 0, sa = 0, wsum = 0;
        for (int sy = int(y0); sy < int(std::ceil(y1)) && sy < source.cell_h; ++sy) {
          const float wy = std::min(y1, float(sy + 1)) - std::max(y0, float(sy));
          for (int sx = int(x0); sx < int(std::ceil(x1)) && sx < source.cell_w; ++sx) {
            const float w = (std::min(x1, float(sx + 1)) - std::max(x0, float(sx))) * wy;
            if (w <= 0) continue;
            const Pixel p = source.pixel(g, sx, sy);
            si += p.i * p.a * w;  // intensity weighted by coverage
            sa += p.a * w;
            wsum += w;
          }
        }
        Pixel out{0, 0};
        if (wsum > 0) out = {sa > 0 ? si / sa : 0.0f, sa / wsum};
        if (source.format <= 1 && game.format >= 2) out.i = 1.0f;  // an intensity font into one with alpha: white
        Font::encode(s, game.format, game.sheet_w, cx + x, cy + y, out);
      }
    }
    int left, gw, adv;
    source.widths(g, left, gw, adv);
    Width w{int(std::lround(left * scale)), std::clamp(int(std::lround(gw * scale)), 1, int(game.cell_w)),
            std::max(1, int(std::lround(adv * scale)))};
    widths.push_back(w);
  }

  // The font laid out again: header, FINF, TGLP with the new sheets, the other blocks, the new width and map blocks.
  // Pointers are file offsets of a block's body (8 bytes past its start); the old ones are moved with their blocks.
  Bytes out(game.d.begin(), game.d.begin() + rd16(&game.d[12], true));
  std::map<u32, u32> moved;  // old body offset -> new body offset (block starts + 8)
  size_t new_tglp = 0, new_finf = 0;
  std::vector<std::pair<size_t, const Font::Block*>> placed;
  for (const Font::Block& b : game.blocks) {
    pad_to(out, 4);
    const size_t at = out.size();
    moved[u32(b.at + 8)] = u32(at + 8);
    if (b.at == game.tglp) {
      new_tglp = at;
      out.insert(out.end(), game.d.begin() + b.at, game.d.begin() + game.tglp + 8 + 24);
      pad_to(out, 0x20);
      const u32 image = u32(out.size());
      out.insert(out.end(), game.d.begin() + game.image, game.d.begin() + game.image + size_t(game.sheet_size) * game.sheets);
      out.insert(out.end(), sheets.begin(), sheets.end());
      wr32(&out[at + 8 + 20], image);
      wr16(&out[at + 8 + 8], u16(game.sheets + new_sheets));
      wr32(&out[at + 4], u32(out.size() - at));
    } else {
      if (b.at == game.finf) new_finf = at;
      out.insert(out.end(), game.d.begin() + b.at, game.d.begin() + b.at + b.size);
    }
    placed.push_back({at, &b});
  }
  auto relocate = [&](size_t at) {
    const u32 old = rd32(&out[at], true);
    if (!old) return;
    auto it = moved.find(old);
    if (it == moved.end()) fail("fuente: un puntero no apunta a un bloque");
    wr32(&out[at], it->second);
  };
  relocate(new_finf + 8 + 8);   // glyphs
  relocate(new_finf + 8 + 12);  // widths
  relocate(new_finf + 8 + 16);  // maps
  for (const auto& [at, b] : placed) {
    if (!strcmp(b->magic, "CWDH") || !strcmp(b->magic, "CMAP")) relocate(at + 8 + (b->magic[1] == 'W' ? 4 : 8));
  }
  // New widths, linked at the end of the width list.
  pad_to(out, 4);
  const size_t cwdh = out.size();
  out.insert(out.end(), {'C', 'W', 'D', 'H', 0, 0, 0, 0});
  put16(out, u16(first));
  put16(out, u16(first + add.size() - 1));
  put32(out, 0);
  for (const Width& w : widths) { out.push_back(u8(int8_t(w.left))); out.push_back(u8(w.glyph_w)); out.push_back(u8(int8_t(w.advance))); }
  pad_to(out, 4);
  wr32(&out[cwdh + 4], u32(out.size() - cwdh));
  {
    u32 p = rd32(&out[new_finf + 8 + 12], true);
    if (!p) wr32(&out[new_finf + 8 + 12], u32(cwdh + 8));
    else {
      for (int guard = 0; guard < 4096; ++guard) {
        const u32 next = rd32(&out[p + 4], true);
        if (!next) { wr32(&out[p + 4], u32(cwdh + 8)); break; }
        p = next;
      }
    }
  }
  // New maps, first in the list (the first map whose range holds a code decides, and the game's may cover every
  // code): one direct map per run of consecutive codes, so that each range holds only codes added here and never
  // hides one of the game's (an é between « and Ё).
  size_t new_blocks = 1;
  u32 next = rd32(&out[new_finf + 8 + 16], true);
  for (size_t end = add.size(); end > 0;) {
    size_t begin = end - 1;
    while (begin > 0 && add[begin - 1].first + 1 == add[begin].first) --begin;
    const size_t cmap = out.size();
    out.insert(out.end(), {'C', 'M', 'A', 'P', 0, 0, 0, 0});
    put16(out, add[begin].first);
    put16(out, add[end - 1].first);
    put16(out, 0);
    put16(out, 0);
    put32(out, next);
    put16(out, u16(first + begin));
    pad_to(out, 4);
    wr32(&out[cmap + 4], u32(out.size() - cmap));
    next = u32(cmap + 8);
    ++new_blocks;
    end = begin;
  }
  wr32(&out[new_finf + 8 + 16], next);
  // Header: size and block count.
  wr32(&out[8], u32(out.size()));
  wr16(&out[14], u16(game.blocks.size() + new_blocks));
  (void)new_tglp;
  r.data = std::move(out);
  r.added = u32(add.size());
  return r;
}

// ---------------------------------------------------------------------------------------------------------------
// game.pak: the game partition's decrypted data at its own offsets (boot.bin at 0, the file table where it says).

struct GamePak {
  FILE* f = nullptr;
  struct Entry { std::string path; u32 offset, size; };
  std::vector<Entry> files;

  explicit GamePak(const std::string& path) {
    f = fopen(path.c_str(), "rb");
    if (!f) fail("no se puede abrir %s", path.c_str());
    const Bytes boot = read(0, 0x440);
    if (memcmp(boot.data(), "RMGE01", 6) != 0) fail("game.pak no es el de Super Mario Galaxy USA (RMGE01)");
    const u32 fst = rd32(&boot[0x424], true) << 2, size = rd32(&boot[0x428], true) << 2;
    if (!fst || size < 12 || size > (64u << 20)) fail("game.pak: la tabla de archivos no cuadra");
    const Bytes t = read(fst, size);
    const u32 n = rd32(&t[8], true);
    if (size_t(n) * 12 > t.size()) fail("game.pak: la tabla de archivos no cuadra");
    const size_t names = size_t(n) * 12;
    std::vector<std::pair<u32, std::string>> dirs = {{n, ""}};  // end index, path
    for (u32 i = 1; i < n; ++i) {
      while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
      const u8* e = &t[i * 12];
      const u32 name = rd32(e, true) & 0xffffff;
      if (names + name >= t.size()) fail("game.pak: la tabla de archivos no cuadra");
      const std::string nm(reinterpret_cast<const char*>(&t[names + name]));
      const std::string path = dirs.back().second + "/" + nm;
      if (e[0] == 1) dirs.push_back({rd32(e + 8, true), path});
      else files.push_back({path, rd32(e + 4, true) << 2, rd32(e + 8, true)});
    }
  }
  ~GamePak() { if (f) fclose(f); }
  Bytes read(uint64_t offset, size_t size) const {
    Bytes out(size);
    if (fseeko(f, off_t(offset), SEEK_SET) != 0 || fread(out.data(), 1, size, f) != size) fail("game.pak está incompleto");
    return out;
  }
  // The first of `paths` (lowercase) the disc has.
  const Entry* find(const std::vector<std::string>& paths) const {
    for (const std::string& p : paths) for (const Entry& e : files) if (lower(e.path) == p) return &e;
    return nullptr;
  }
};

Bytes unpacked(const Bytes& d) { return is_yaz0(d) ? yaz0_decode(d) : d; }

// Progress as nebula-pak prints it (Cactus Galaxy Wrapper reads these lines): the stages over the translation's size.
void progress(unsigned percent, unsigned total_mib) {
  printf(" %u%% (%u / %u MiB)\n", percent, total_mib * percent / 100, total_mib);
}

int run(const std::string& translation_path, const std::string& pak_path, const std::string& out_dir) {
  const GamePak pak(pak_path);
  const Translation tr(translation_path);
  printf("Traducción: %s\n", translation_path.c_str());
  struct stat st;
  const unsigned total = stat(translation_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)
                             ? std::max<unsigned>(1, unsigned((st.st_size + (1 << 20) - 1) >> 20)) : 16;
  progress(5, total);

  const GamePak::Entry* wii_msg = pak.find({"/usenglish/messagedata/message.arc"});
  const GamePak::Entry* wii_font = pak.find({"/usenglish/layoutdata/font.arc", "/layoutdata/font.arc"});
  if (!wii_msg) fail("game.pak no tiene UsEnglish/MessageData/Message.arc");
  if (!wii_font) fail("game.pak no tiene LayoutData/Font.arc");
  const std::vector<std::string> prefer = {"/euenglish/", "/usenglish/", "english"};
  Bytes ru_msg_raw, ru_font_raw;
  std::string ru_msg_name, ru_font_name;
  if (!tr.find("/messagedata/message.arc", prefer, ru_msg_raw, &ru_msg_name))
    fail("la traducción no tiene MessageData/Message.arc: ¿es el .zip de la traducción rusa?");
  const bool have_font = tr.find("/layoutdata/font.arc", prefer, ru_font_raw, &ru_font_name);

  const std::string temporary = out_dir + ".tmp";
  remove_tree(temporary);

  // Text.
  const Bytes wii_msg_file = pak.read(wii_msg->offset, wii_msg->size);
  const Rarc wii_msg_arc(unpacked(wii_msg_file)), ru_msg_arc(unpacked(ru_msg_raw));
  MessageResult m = convert_messages(wii_msg_arc, ru_msg_arc);
  printf("Textos: %u mensajes en ruso, %u siguen en inglés (no están en la traducción), %u solo de la traducción\n",
         m.russian, m.english, m.only_translation);
  printf("Etiquetas: %u (%u iguales a las del juego, %u por su tipo)\n", m.tags.tags, m.tags.matched, m.tags.guessed);
  if (m.russian == 0) fail("ningún mensaje de la traducción coincide con los del juego");
  write_file(temporary + "/files" + wii_msg->path, is_yaz0(wii_msg_file) ? yaz0_encode(m.arc) : m.arc);
  progress(30, total);

  // Fonts: what the text uses, and all of Russian's alphabet (layouts may spell what the messages do not).
  std::set<u16> codes = m.codes;
  for (u16 c = 0x410; c <= 0x44F; ++c) codes.insert(c);
  codes.insert(0x401);
  codes.insert(0x451);
  if (!have_font) {
    printf("La traducción no trae Font.arc: las letras rusas no se verán\n");
  } else {
    const Bytes wii_font_file = pak.read(wii_font->offset, wii_font->size);
    const Rarc wii_font_arc(unpacked(wii_font_file));
    const Rarc ru_font_arc(unpacked(ru_font_raw));
    std::map<std::string, Bytes> fonts;
    for (const Rarc::File& f : wii_font_arc.files) {
      const std::string name = lower(f.path.substr(f.path.rfind('/') + 1));
      if (!ends_with(name, ".brfnt")) continue;
      const Rarc::File* source = ru_font_arc.file(name);
      if (!source) { printf("  %s: la traducción no la trae\n", f.path.c_str()); continue; }
      const Font game(wii_font_arc.contents(f)), ru(ru_font_arc.contents(*source));
      FontResult fr = add_glyphs(game, ru, codes);
      if (!fr.note.empty()) printf("  %s: %s\n", f.path.c_str(), fr.note.c_str());
      if (fr.added == 0) {
        if (fr.note.empty()) printf("  %s: no le faltan letras\n", f.path.c_str());
        continue;
      }
      printf("  %s: %u letras nuevas (celda %ux%u, escala %.2f, %u KiB)\n", f.path.c_str(), fr.added, game.cell_w,
             game.cell_h, fr.scale, unsigned(fr.data.size() >> 10));
      fonts[f.path] = std::move(fr.data);
    }
    if (!fonts.empty()) {
      const Bytes arc = wii_font_arc.rebuilt(fonts);
      printf("Font.arc: %u KiB (el del juego: %u KiB)\n", unsigned(arc.size() >> 10),
             unsigned(wii_font_arc.d.size() >> 10));
      write_file(temporary + "/files" + wii_font->path, is_yaz0(wii_font_file) ? yaz0_encode(arc) : arc);
    }
  }

  progress(100, total);
  remove_tree(out_dir);
  make_dirs(out_dir.substr(0, out_dir.rfind('/')));
  if (rename(temporary.c_str(), out_dir.c_str()) != 0) fail("no se puede crear %s: %s", out_dir.c_str(), strerror(errno));
  printf("Mod listo: %s\n", out_dir.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc != 4) {
    fprintf(stderr,
            "uso: cactus-ruso <traducción.zip o carpeta> <game.pak> <carpeta del mod>\n"
            "  La traducción rusa de Super Mario Galaxy para Switch (СаХаР, sobre la de reWiision) llevada al juego\n"
            "  de Wii (USA, RMGE01) como mod: textos y letras.\n");
    return 2;
  }
  try {
    return run(argv[1], argv[2], argv[3]);
  } catch (const std::exception& e) {
    printf("Error: %s\n", e.what());
    return 1;
  }
}
