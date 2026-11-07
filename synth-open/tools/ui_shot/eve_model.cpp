// FT812 model, see eve_model.h. Host only.
#include "eve_model.h"

#include "FT81x.h"   // CMD_* ids and the format constants of the firmware's own FT812 library

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <zlib.h>

#undef RECTS
#undef LINES
#undef BITMAPS
#undef LINE_STRIP

namespace
{
enum Prim { kBitmaps = 1, kPoints = 2, kLines = 3, kLineStrip = 4, kEdgeStripR = 5, kEdgeStripL = 6, kEdgeStripA = 7, kEdgeStripB = 8, kRects = 9 };
enum Fmt { fARGB1555 = 0, fL1 = 1, fL4 = 2, fL8 = 3, fRGB332 = 4, fARGB2 = 5, fARGB4 = 6, fRGB565 = 7, fPALETTED = 8, fL2 = 17 };
constexpr uint32_t kGradientMarker = 0x3E000000u;   // private DL word: 4 payload words follow (not a chip opcode)

inline int sx(uint32_t v, int bits)
{
    const uint32_t m = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return int((v ^ m)) - int(m);
}

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// DL command encoders (the chip's own bit layouts)
inline uint32_t dBegin(uint32_t p) { return (31u << 24) | p; }
inline uint32_t dEnd() { return 33u << 24; }
inline uint32_t dSave() { return 34u << 24; }
inline uint32_t dRestore() { return 35u << 24; }
inline uint32_t dVfmt(uint32_t f) { return (39u << 24) | f; }
inline uint32_t dHandle(uint32_t h) { return (5u << 24) | h; }
inline uint32_t dCell(uint32_t c) { return (6u << 24) | c; }
inline uint32_t dV2F(int x, int y) { return (1u << 30) | ((uint32_t(x) & 32767u) << 15) | (uint32_t(y) & 32767u); }
}   // namespace

EveModel::EveModel() : ramg_(kRamG, 0), frame_(kW * kH, 0xFF000000u), work_(kW * kH, 0xFF000000u)
{
    dl_.reserve(kDlWords);
}

void EveModel::getImgProps(uint32_t& addr, uint32_t& w, uint32_t& h)
{
    addr = props_[0];
    w = props_[1];
    h = props_[2];
}

void EveModel::writeDataRAMG(const uint8_t* d, uint32_t n, uint32_t dst)
{
    if (dst + n > kRamG) { errors_++; log_ += "writeDataRAMG out of range\n"; return; }
    memcpy(&ramg_[dst], d, n);
}

void EveModel::readDataRAMG(uint8_t* d, uint32_t n, uint32_t src)
{
    if (src + n > kRamG) { memset(d, 0, n); return; }
    memcpy(d, &ramg_[src], n);
}

void EveModel::WriteData32(uint32_t w) { in_.push_back(w); }

uint8_t EveModel::sendString(const char* s, uint32_t length)
{
    if (!length) length = uint32_t(strlen(s));
    const uint32_t padded = (length + 1 + 3) & ~3u;
    std::vector<uint8_t> b(padded, 0);
    memcpy(b.data(), s, length);
    for (uint32_t i = 0; i < padded; i += 4)
    {
        uint32_t w;
        memcpy(&w, &b[i], 4);
        in_.push_back(w);
    }
    return uint8_t(padded);
}

void EveModel::writeDataToCMD(const uint8_t*, uint32_t) {}   // image data for CMD_LOADIMAGE: not decoded

// ------------------------------------------------------------------------------------------------------------------
// coprocessor
// ------------------------------------------------------------------------------------------------------------------
void EveModel::emit(uint32_t w)
{
    if (dl_.size() >= size_t(kDlWords)) { stats.dlOverflow++; return; }
    dl_.push_back(w);
}

static int nargs(uint32_t id)
{
    switch (0xFFFFFF00u | id)
    {
        case CMD_DLSTART: case CMD_SWAP: case CMD_LOADIDENTITY: case CMD_SETMATRIX: case CMD_STOP: case CMD_COLDSTART: return 0;
        case CMD_BGCOLOR: case CMD_FGCOLOR: case CMD_GRADCOLOR: case CMD_SNAPSHOT: case CMD_ROTATE: case CMD_SETBASE: case CMD_INFLATE:
        case CMD_INTERRUPT: case CMD_GETPTR: case CMD_SETROTATE: case CMD_SETSCRATCH: return 1;
        case CMD_APPEND: case CMD_MEMZERO: case CMD_TRANSLATE: case CMD_SCALE: case CMD_LOADIMAGE: case CMD_SETFONT: case CMD_MEMWRITE:
        case CMD_REGREAD: case CMD_SPINNER: case CMD_MEDIAFIFO: case CMD_ROMFONT: return 2;
        case CMD_MEMCPY: case CMD_MEMSET: case CMD_GETPROPS: case CMD_SETFONT2: case CMD_SETBITMAP: case CMD_NUMBER: case CMD_MEMCRC: return 3;
        case CMD_GRADIENT: case CMD_SNAPSHOT2: case CMD_SLIDER: return 4;
        default: return -1;
    }
}

void EveModel::run()
{
    while (inPos_ < in_.size())
    {
        const uint32_t w = in_[inPos_];
        if ((w >> 8) != 0xFFFFFFu)
        {
            if (nPending_) { /* never */ }
            emit(w);
            inPos_++;
            continue;
        }
        const uint32_t id = w & 0xFF;
        stats.cmd[id]++;
        int n = nargs(id);
        size_t need = 0;
        if (w == CMD_TEXT)
        {
            // two words, then the string up to and including the word that holds the terminating zero byte
            if (inPos_ + 3 > in_.size()) break;
            size_t k = inPos_ + 3;
            bool done = false;
            while (k < in_.size())
            {
                const uint32_t sw = in_[k++];
                if (!(sw & 0xFF) || !(sw & 0xFF00) || !(sw & 0xFF0000) || !(sw & 0xFF000000)) { done = true; break; }
            }
            if (!done) break;
            need = k - inPos_ - 1;
        }
        else
        {
            if (n < 0) { errors_++; char b[64]; snprintf(b, sizeof b, "unknown coprocessor command %08X\n", w); log_ += b; inPos_++; continue; }
            need = size_t(n);
        }
        if (inPos_ + 1 + need > in_.size()) break;
        inPos_++;
        exec(w);
    }
    if (inPos_ == in_.size()) { in_.clear(); inPos_ = 0; }
}

static uint32_t arg(const std::vector<uint32_t>& v, size_t& p) { return v[p++]; }

void EveModel::exec(uint32_t id)
{
    auto A = [&]() { return in_[inPos_++]; };
    switch (id)
    {
        case CMD_DLSTART: dl_.clear(); break;
        case CMD_SWAP:
            if (dl_.empty() || dl_.back() != 0) emit(0);
            render();
            break;
        case CMD_SETBASE: base_ = int(A()); break;
        case CMD_LOADIDENTITY: { int32_t i[6] = {65536, 0, 0, 0, 65536, 0}; memcpy(m_, i, sizeof m_); break; }
        case CMD_TRANSLATE:
        {
            const int64_t tx = int32_t(A()), ty = int32_t(A());
            m_[2] = int32_t(((int64_t)m_[0] * tx + (int64_t)m_[1] * ty) / 65536 + m_[2]);
            m_[5] = int32_t(((int64_t)m_[3] * tx + (int64_t)m_[4] * ty) / 65536 + m_[5]);
            break;
        }
        case CMD_SCALE:
        {
            const int64_t a = int32_t(A()), b = int32_t(A());
            m_[0] = int32_t((int64_t)m_[0] * a / 65536); m_[3] = int32_t((int64_t)m_[3] * a / 65536);
            m_[1] = int32_t((int64_t)m_[1] * b / 65536); m_[4] = int32_t((int64_t)m_[4] * b / 65536);
            break;
        }
        case CMD_ROTATE:
        {
            const double ang = double(int32_t(A())) * 2.0 * M_PI / 65536.0;
            const double c = cos(ang), s = sin(ang);
            const double a0 = m_[0], b0 = m_[1], d0 = m_[3], e0 = m_[4];
            m_[0] = int32_t(lround(a0 * c + b0 * s)); m_[1] = int32_t(lround(-a0 * s + b0 * c));
            m_[3] = int32_t(lround(d0 * c + e0 * s)); m_[4] = int32_t(lround(-d0 * s + e0 * c));
            break;
        }
        case CMD_SETMATRIX:
        {
            const int32_t v[6] = {m_[0] >> 8, m_[1] >> 8, m_[2] >> 8, m_[3] >> 8, m_[4] >> 8, m_[5] >> 8};
            emit((21u << 24) | (uint32_t(v[0]) & 0x1FFFF)); emit((22u << 24) | (uint32_t(v[1]) & 0x1FFFF));
            emit((23u << 24) | (uint32_t(v[2]) & 0xFFFFFF)); emit((24u << 24) | (uint32_t(v[3]) & 0x1FFFF));
            emit((25u << 24) | (uint32_t(v[4]) & 0x1FFFF)); emit((26u << 24) | (uint32_t(v[5]) & 0xFFFFFF));
            break;
        }
        case CMD_APPEND:
        {
            const uint32_t ptr = A(), num = A();
            for (uint32_t i = 0; i + 4 <= num; i += 4)
            {
                uint32_t w = 0;
                if (ptr + i + 4 <= kRamG) memcpy(&w, &ramg_[ptr + i], 4);
                emit(w);
            }
            break;
        }
        case CMD_MEMCPY:
        {
            const uint32_t dst = A(), src = A(), num = A();
            std::vector<uint8_t> tmp(num, 0);
            if (src >= kRamDl) { for (uint32_t i = 0; i < num; i++) { const size_t wi = (src - kRamDl + i) / 4; tmp[i] = wi < dl_.size() ? uint8_t(dl_[wi] >> (8 * ((src - kRamDl + i) & 3))) : 0; } }
            else if (src + num <= kRamG) memcpy(tmp.data(), &ramg_[src], num);
            if (dst < kRamG && dst + num <= kRamG) memcpy(&ramg_[dst], tmp.data(), num);
            else { errors_++; log_ += "MEMCPY out of RAM_G\n"; }
            break;
        }
        case CMD_MEMZERO: { const uint32_t p = A(), n = A(); if (p + n <= kRamG) memset(&ramg_[p], 0, n); break; }
        case CMD_MEMSET: { const uint32_t p = A(), v = A(), n = A(); if (p + n <= kRamG) memset(&ramg_[p], int(v & 0xFF), n); break; }
        case CMD_SETFONT2:
        {
            // the chip rewrites the handle's BITMAP_SOURCE to the address of glyph 0 (the glyph of 'first' minus first cells), so a cell
            // number is the character code, as the firmware's own glyph loops (cTracker::String2Bitmaps) use it. Applied when the display
            // list runs, in order with the BITMAP_* words around it (private DL word 0x3D)
            const uint32_t f = A(), p = A(), fc = A();
            if (f < 32) { font_[f].ptr = p; font_[f].first = fc; font_[f].set = true; emit(0x3D000000u | (f << 8) | (fc & 0xFF)); }
            break;
        }
        case CMD_SETFONT: { const uint32_t f = A(), p = A(); if (f < 32) { font_[f].ptr = p; font_[f].first = 32; font_[f].set = true; } break; }
        case CMD_SETBITMAP:
        {
            const uint32_t src = A(), fw = A(), h = A();
            const uint32_t fmt = fw & 0xFFFF, w = fw >> 16;
            uint32_t stride;
            switch (fmt)
            {
                case fL1: stride = (w + 7) / 8; break;
                case fL2: stride = (w + 3) / 4; break;
                case fL4: stride = (w + 1) / 2; break;
                case fL8: case fRGB332: case fARGB2: case fPALETTED: case 14: case 15: case 16: stride = w; break;
                default: stride = w * 2; break;
            }
            emit((1u << 24) | (src & 0x3FFFFF));
            emit((7u << 24) | ((fmt & 31) << 19) | ((stride & 1023) << 9) | (h & 511));
            emit((40u << 24) | (((stride >> 10) & 3) << 2) | ((h >> 9) & 3));
            emit((8u << 24) | (0u << 20) | (0u << 19) | (0u << 18) | ((w & 511) << 9) | (h & 511));
            emit((41u << 24) | (((w >> 9) & 3) << 2) | ((h >> 9) & 3));
            break;
        }
        case CMD_GETPROPS: props_[0] = A(); props_[1] = A(); props_[2] = A(); props_[0] = props_[1] = props_[2] = 0; break;
        case CMD_GRADIENT:
        {
            const uint32_t p0 = A(), c0 = A(), p1 = A(), c1 = A();
            emit(kGradientMarker);
            emit(p0); emit(c0); emit(p1); emit(c1);
            break;
        }
        case CMD_TEXT:
        {
            const uint32_t xy = A(), fo = A();
            std::string s;
            for (;;)
            {
                const uint32_t w = A();
                bool end = false;
                for (int b = 0; b < 4; b++) { const char c = char(w >> (8 * b)); if (!c) { end = true; break; } s += c; }
                if (end) break;
            }
            textRun(int16_t(xy & 0xFFFF), int16_t(xy >> 16), int16_t(fo & 0xFFFF), fo >> 16, s);
            break;
        }
        case CMD_NUMBER:
        {
            const uint32_t xy = A(), fo = A(), nn = A();
            const uint32_t opt = fo >> 16;
            const bool sgn = opt & 256;
            const int width = int(opt & 0xFF);   // options below 256 hold the field width
            char buf[40];
            std::string digits;
            uint64_t v;
            bool neg = false;
            if (sgn && int32_t(nn) < 0) { neg = true; v = uint64_t(-(int64_t)int32_t(nn)); } else v = sgn ? uint64_t(int32_t(nn)) : uint64_t(nn);
            do { const int d = int(v % uint32_t(base_)); digits.insert(digits.begin(), char(d < 10 ? '0' + d : 'A' + d - 10)); v /= uint32_t(base_); } while (v);
            while (int(digits.size()) < width) digits.insert(digits.begin(), '0');
            if (neg) digits.insert(digits.begin(), '-');
            (void)buf;
            textRun(int16_t(xy & 0xFFFF), int16_t(xy >> 16), int16_t(fo & 0xFFFF), opt & ~0xFFu & ~256u, digits);
            break;
        }
        case CMD_BGCOLOR: case CMD_FGCOLOR: case CMD_GRADCOLOR: case CMD_SNAPSHOT: case CMD_INFLATE: case CMD_INTERRUPT: case CMD_GETPTR:
        case CMD_SETROTATE: case CMD_SETSCRATCH: A(); break;
        case CMD_LOADIMAGE: case CMD_MEMWRITE: case CMD_REGREAD: case CMD_SPINNER: case CMD_MEDIAFIFO: case CMD_ROMFONT: A(); A(); break;
        case CMD_SNAPSHOT2:
        {
            // ARGB8 snapshot of the last CMD_SWAP into RAM_G. The firmware (cDisplay::doScreenShot) asks for x = 0, y = 0 or 240, a
            // width of 1600 and 240 rows and then reads 3200 bytes per row, so rows are 800 pixels at 4 bytes here, whatever w says.
            const uint32_t fmt = A(), ptr = A(), xy = A(), wh = A();
            const int y0 = int(int16_t(xy >> 16)), rows = int(int16_t(wh >> 16));
            if (fmt != 0x20) { errors_++; log_ += "SNAPSHOT2: only ARGB8 is modelled\n"; break; }
            for (int r = 0; r < rows; r++)
            {
                const int y = y0 + r;
                if (y < 0 || y >= kH || ptr + uint32_t(r + 1) * kW * 4 > kRamG) continue;
                memcpy(&ramg_[ptr + uint32_t(r) * kW * 4], &frame_[size_t(y) * kW], size_t(kW) * 4);
            }
            break;
        }
        case CMD_SLIDER: A(); A(); A(); A(); break;
        case CMD_MEMCRC: A(); A(); A(); break;
        case CMD_STOP: case CMD_COLDSTART: break;
        default: errors_++; break;
    }
}

// character advance of font handle 'font' (metric block widths), the glyph cell and the font height; 0 width when absent
uint32_t EveModel::fontWidth(int font, int ch, int* height, int* cellOut)
{
    if (font < 0 || font >= 32 || !font_[font].set) return 0;
    const uint32_t p = font_[font].ptr;
    if (p + 148 > kRamG) return 0;
    if (height) *height = int(ramg_[p + 140]);
    ch &= 127;
    if (cellOut) *cellOut = ch;
    return ramg_[p + uint32_t(ch)];
}

// ROM fonts (16..31) are not in the host build: text on a handle without CMD_SETFONT2 uses custom font 1, the closest in size
// to ROM font 28/29, and is counted in stats.romTextCalls (the report says which screens do it)
void EveModel::textRun(int x, int y, int font, int options, const std::string& s)
{
    int use = font;
    if (use < 0 || use >= 32 || !font_[use].set)
    {
        stats.romTextCalls++;
        use = -1;
        for (int i = 1; i < 32; i++) if (font_[i].set) { use = i; break; }
        if (use < 0) return;
    }
    int h = 0, cell = 0;
    int total = 0;
    for (char c : s) total += int(fontWidth(use, (unsigned char)c, &h, &cell));
    int px = x;
    int py = y;
    if (options & 2048) px -= total;            // OPT_RIGHTX
    else if (options & 512) px -= total / 2;    // OPT_CENTERX
    if (options & 1024) py -= h / 2;            // OPT_CENTERY
    emit(dSave());
    emit(dVfmt(0));
    emit(dBegin(kBitmaps));
    for (char c : s)
    {
        const uint32_t adv = fontWidth(use, (unsigned char)c, &h, &cell);
        if (adv)
        {
            emit(dHandle(uint32_t(use)));
            emit(dCell(uint32_t(cell)));
            emit(dV2F(px, py));
        }
        px += int(adv);
    }
    emit(dEnd());
    emit(dRestore());
}

// ------------------------------------------------------------------------------------------------------------------
// display list rasteriser
// ------------------------------------------------------------------------------------------------------------------
void EveModel::render()
{
    swaps_++;
    // the frame starts with the default graphics context. The bitmap handle table survives (the firmware sets the font
    // handles once, in the display list of loadFont)
    g_ = Gfx();
    sp_ = 0;
    verts_.clear();
    // a new frame renders on top of nothing: clear to the DL's own CLEAR
    for (auto& p : work_) p = 0xFF000000u;
    draw(dl_.data(), dl_.size());
    flushPrim();
    frame_ = work_;
}

void EveModel::blend(int x, int y, float sr, float sg, float sb, float sa)
{
    if (x < 0 || y < 0 || x >= kW || y >= kH) return;
    if (x < g_.scX || y < g_.scY || x >= g_.scX + g_.scW || y >= g_.scY + g_.scH) return;
    uint32_t& d = work_[size_t(y) * kW + x];
    const float da = ((d >> 24) & 255) / 255.0f, dr = float((d >> 16) & 255), dg = float((d >> 8) & 255), db = float(d & 255);
    auto fac = [&](int f) -> float {
        switch (f) { case 0: return 0; case 1: return 1; case 2: return sa; case 3: return da; case 4: return 1 - sa; case 5: return 1 - da; default: return 0; }
    };
    const float fs = fac(g_.blendSrc), fd = fac(g_.blendDst);
    // colour channels are premultiplied by the factor, the factor is the source alpha (SRC_ALPHA) or a constant
    const float nr = sr * fs + dr * fd, ng = sg * fs + dg * fd, nb = sb * fs + db * fd, na = sa * fs + da * fd;
    auto q = [](float v) { return uint32_t(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f)); };
    d = (q(na * 255.0f) << 24) | (q(nr) << 16) | (q(ng) << 8) | q(nb);
}

void EveModel::putPixel(int x, int y, float r, float g, float b, float a) { blend(x, y, r, g, b, a); }

void EveModel::rect(float x0, float y0, float x1, float y1, float rad)
{
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    const int bx0 = std::max(0, int(floorf(x0 - rad - 1))), bx1 = std::min(kW - 1, int(ceilf(x1 + rad + 1)));
    const int by0 = std::max(0, int(floorf(y0 - rad - 1))), by1 = std::min(kH - 1, int(ceilf(y1 + rad + 1)));
    const float al = g_.color[3] / 255.0f;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++)
        {
            const float cx = x + 0.5f, cy = y + 0.5f;
            const float dx = std::max(std::max(x0 - cx, cx - x1), 0.0f), dy = std::max(std::max(y0 - cy, cy - y1), 0.0f);
            const float cov = clamp01(0.5f + rad - sqrtf(dx * dx + dy * dy));
            if (cov > 0) blend(x, y, g_.color[0], g_.color[1], g_.color[2], cov * al);
        }
}

void EveModel::capsule(float x0, float y0, float x1, float y1, float rad)
{
    const int bx0 = std::max(0, int(floorf(std::min(x0, x1) - rad - 1))), bx1 = std::min(kW - 1, int(ceilf(std::max(x0, x1) + rad + 1)));
    const int by0 = std::max(0, int(floorf(std::min(y0, y1) - rad - 1))), by1 = std::min(kH - 1, int(ceilf(std::max(y0, y1) + rad + 1)));
    const float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    const float al = g_.color[3] / 255.0f;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++)
        {
            const float cx = x + 0.5f - x0, cy = y + 0.5f - y0;
            float t = l2 > 0 ? (cx * vx + cy * vy) / l2 : 0;
            t = clamp01(t);
            const float dx = cx - t * vx, dy = cy - t * vy;
            const float cov = clamp01(0.5f + rad - sqrtf(dx * dx + dy * dy));
            if (cov > 0) blend(x, y, g_.color[0], g_.color[1], g_.color[2], cov * al);
        }
}

void EveModel::dot(float cx, float cy, float rad)
{
    const int bx0 = std::max(0, int(floorf(cx - rad - 1))), bx1 = std::min(kW - 1, int(ceilf(cx + rad + 1)));
    const int by0 = std::max(0, int(floorf(cy - rad - 1))), by1 = std::min(kH - 1, int(ceilf(cy + rad + 1)));
    const float al = g_.color[3] / 255.0f;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float cov = clamp01(0.5f + rad - sqrtf(dx * dx + dy * dy));
            if (cov > 0) blend(x, y, g_.color[0], g_.color[1], g_.color[2], cov * al);
        }
}

void EveModel::gradient(int x0, int y0, uint32_t c0, int x1, int y1, uint32_t c1)
{
    const float vx = float(x1 - x0), vy = float(y1 - y0), l2 = vx * vx + vy * vy;
    const int xa = std::max(0, g_.scX), xb = std::min(kW, g_.scX + g_.scW), ya = std::max(0, g_.scY), yb = std::min(kH, g_.scY + g_.scH);
    const float al = g_.color[3] / 255.0f;
    for (int y = ya; y < yb; y++)
        for (int x = xa; x < xb; x++)
        {
            const float t = l2 > 0 ? clamp01(((x - x0) * vx + (y - y0) * vy) / l2) : 0;
            auto ch = [&](int sh) { return ((c0 >> sh) & 255) * (1 - t) + ((c1 >> sh) & 255) * t; };
            blend(x, y, ch(16), ch(8), ch(0), al);
        }
}

// one bitmap sample, 0..255 per channel, a = 0 outside a BORDER bitmap
static void texel(const std::vector<uint8_t>& ram, uint32_t base, uint32_t fmt, uint32_t stride, int x, int y, float out[4])
{
    const uint32_t row = base + uint32_t(y) * stride;
    auto byteAt = [&](uint32_t off) -> uint32_t { return (row + off) < ram.size() ? ram[row + off] : 0; };
    switch (fmt)
    {
        case fL1: { const uint32_t b = byteAt(uint32_t(x) >> 3); out[3] = ((b >> (7 - (x & 7))) & 1) * 255.0f; out[0] = out[1] = out[2] = 255; break; }
        case fL2: { const uint32_t b = byteAt(uint32_t(x) >> 2); out[3] = ((b >> (6 - 2 * (x & 3))) & 3) * 85.0f; out[0] = out[1] = out[2] = 255; break; }
        case fL4: { const uint32_t b = byteAt(uint32_t(x) >> 1); out[3] = ((b >> ((x & 1) ? 0 : 4)) & 15) * 17.0f; out[0] = out[1] = out[2] = 255; break; }
        case fL8: out[3] = float(byteAt(uint32_t(x))); out[0] = out[1] = out[2] = 255; break;
        case fARGB4: { const uint32_t v = byteAt(uint32_t(x) * 2) | (byteAt(uint32_t(x) * 2 + 1) << 8); out[3] = ((v >> 12) & 15) * 17.0f; out[0] = ((v >> 8) & 15) * 17.0f; out[1] = ((v >> 4) & 15) * 17.0f; out[2] = (v & 15) * 17.0f; break; }
        case fARGB1555: { const uint32_t v = byteAt(uint32_t(x) * 2) | (byteAt(uint32_t(x) * 2 + 1) << 8); out[3] = (v >> 15) ? 255.0f : 0; out[0] = ((v >> 10) & 31) * 255.0f / 31; out[1] = ((v >> 5) & 31) * 255.0f / 31; out[2] = (v & 31) * 255.0f / 31; break; }
        case fRGB565: { const uint32_t v = byteAt(uint32_t(x) * 2) | (byteAt(uint32_t(x) * 2 + 1) << 8); out[3] = 255; out[0] = ((v >> 11) & 31) * 255.0f / 31; out[1] = ((v >> 5) & 63) * 255.0f / 63; out[2] = (v & 31) * 255.0f / 31; break; }
        case fRGB332: { const uint32_t v = byteAt(uint32_t(x)); out[3] = 255; out[0] = ((v >> 5) & 7) * 255.0f / 7; out[1] = ((v >> 2) & 7) * 255.0f / 7; out[2] = (v & 3) * 255.0f / 3; break; }
        case fARGB2: { const uint32_t v = byteAt(uint32_t(x)); out[3] = ((v >> 6) & 3) * 85.0f; out[0] = ((v >> 4) & 3) * 85.0f; out[1] = ((v >> 2) & 3) * 85.0f; out[2] = (v & 3) * 85.0f; break; }
        default: out[0] = out[1] = out[2] = 128; out[3] = 255; break;   // palettes: not modelled, flat grey
    }
}

void EveModel::bitmapAt(int ox, int oy, int handle, int cell)
{
    const Bitmap& b = bmp_[handle & 31];
    stats.fmt[b.format & 31]++;
    if (!b.width || !b.bheight) return;
    const uint32_t cellBytes = b.stride * b.height;
    const uint32_t base = (b.source + uint32_t(cell) * cellBytes) & 0x3FFFFF;
    // transform: a b c / d e f, 8.8 (c f 16.8). The 1.15 form (p bit) is not emitted by the firmware.
    const float A = b.xf[0] / (b.p[0] ? 32768.0f : 256.0f), B = b.xf[1] / (b.p[1] ? 32768.0f : 256.0f), C = b.xf[2] / 256.0f;
    const float D = b.xf[3] / (b.p[2] ? 32768.0f : 256.0f), E = b.xf[4] / (b.p[3] ? 32768.0f : 256.0f), F = b.xf[5] / 256.0f;
    const float al = g_.color[3] / 255.0f;
    for (uint32_t dy = 0; dy < b.bheight; dy++)
        for (uint32_t dx = 0; dx < b.width; dx++)
        {
            const float fx = A * dx + B * dy + C, fy = D * dx + E * dy + F;
            int sxp = int(floorf(fx)), syp = int(floorf(fy));
            float t[4] = {0, 0, 0, 0};
            bool inside = sxp >= 0 && syp >= 0 && sxp < int(b.width) && syp < int(b.bheight) && sxp < int(b.stride * 8) ;
            if (b.wrapx == 1 && b.width) sxp = ((sxp % int(b.width)) + int(b.width)) % int(b.width), inside = syp >= 0 && syp < int(b.bheight);
            if (!inside) continue;
            if (syp >= int(b.height)) continue;
            texel(ramg_, base, b.format, b.stride, sxp, syp, t);
            const bool alphaOnly = b.format == fL1 || b.format == fL2 || b.format == fL4 || b.format == fL8;
            const float r = alphaOnly ? g_.color[0] : t[0] * g_.color[0] / 255.0f;
            const float g = alphaOnly ? g_.color[1] : t[1] * g_.color[1] / 255.0f;
            const float bl = alphaOnly ? g_.color[2] : t[2] * g_.color[2] / 255.0f;
            const float a = (t[3] / 255.0f) * al;
            if (a > 0) blend(ox + int(dx), oy + int(dy), r, g, bl, a);
        }
}

void EveModel::flushPrim() { verts_.clear(); }

void EveModel::vertex(int x, int y)
{
    // vertex coordinates are in 1/(1<<vfrac) pixel after the translate
    const float scale = 1.0f / float(1 << g_.vfrac);
    const float fx = (x + g_.vtx) * scale, fy = (y + g_.vty) * scale;
    const float lr = std::max(g_.lineWidth / 16.0f - 0.5f, 0.5f);
    switch (g_.prim)
    {
        case kBitmaps: bitmapAt(int(floorf(fx)), int(floorf(fy)), g_.handle, g_.cell); break;
        case kPoints: dot(fx, fy, g_.pointSize / 16.0f); break;
        case kLines:
            verts_.push_back({x, y});
            if (verts_.size() == 2)
            {
                capsule((verts_[0].first + g_.vtx) * scale, (verts_[0].second + g_.vty) * scale, fx, fy, lr);
                verts_.clear();
            }
            break;
        case kLineStrip:
            if (!verts_.empty()) capsule((verts_.back().first + g_.vtx) * scale, (verts_.back().second + g_.vty) * scale, fx, fy, lr);
            verts_.push_back({x, y});
            break;
        case kRects:
            verts_.push_back({x, y});
            if (verts_.size() == 2)
            {
                // the OpenTracker note on the device: the area within LINE_WIDTH/2 of the vertex box, rounded beyond 1 px
                rect((verts_[0].first + g_.vtx) * scale, (verts_[0].second + g_.vty) * scale, fx, fy, g_.lineWidth <= 16 ? 0.5f : lr);
                verts_.clear();
            }
            break;
        default: break;
    }
}

void EveModel::doDlWord(uint32_t w)
{
    const uint32_t op = w >> 24;
    if (w >> 30 == 1) { vertex(sx(w >> 15, 15), sx(w, 15)); return; }
    if (w >> 30 == 2)
    {
        // VERTEX2II: whole pixels, handle and cell inside the word
        const int x = int((w >> 21) & 511), y = int((w >> 12) & 511);
        g_.handle = int((w >> 7) & 31); g_.cell = int(w & 127);
        const int sv = g_.vfrac;
        g_.vfrac = 0;
        const int tx = g_.vtx, ty = g_.vty;
        g_.vtx = tx; g_.vty = ty;
        vertex(x, y);
        g_.vfrac = sv;
        return;
    }
    stats.dl[op & 63]++;
    switch (op)
    {
        case 0: break;   // DISPLAY
        case 1: bmp_[g_.handle & 31].source = w & 0x3FFFFF; break;
        case 2: g_.clearColor[0] = uint8_t(w >> 16); g_.clearColor[1] = uint8_t(w >> 8); g_.clearColor[2] = uint8_t(w); break;
        case 4: g_.color[0] = uint8_t(w >> 16); g_.color[1] = uint8_t(w >> 8); g_.color[2] = uint8_t(w); break;
        case 5: g_.handle = int(w & 31); break;
        case 6: g_.cell = int(w & 127); break;
        case 7: { Bitmap& b = bmp_[g_.handle & 31]; b.format = (w >> 19) & 31; b.stride = (w >> 9) & 1023; b.height = w & 511; break; }
        case 40: { Bitmap& b = bmp_[g_.handle & 31]; b.stride = (b.stride & 1023) | (((w >> 2) & 3) << 10); b.height = (b.height & 511) | ((w & 3) << 9); break; }
        case 8: { Bitmap& b = bmp_[g_.handle & 31]; b.filter = (w >> 20) & 1; b.wrapx = (w >> 19) & 1; b.wrapy = (w >> 18) & 1; b.width = (w >> 9) & 511; b.bheight = w & 511; break; }
        case 41: { Bitmap& b = bmp_[g_.handle & 31]; b.width = (b.width & 511) | (((w >> 2) & 3) << 9); b.bheight = (b.bheight & 511) | ((w & 3) << 9); break; }
        case 11: g_.blendSrc = int((w >> 3) & 7); g_.blendDst = int(w & 7); break;
        case 13: g_.pointSize = int(w & 0x1FFF); break;
        case 14: g_.lineWidth = int(w & 0xFFF); break;
        case 15: g_.clearColor[3] = uint8_t(w); break;
        case 16: g_.color[3] = uint8_t(w); break;
        case 21: { Bitmap& b = bmp_[g_.handle & 31]; b.p[0] = (w >> 16) & 1; b.xf[0] = b.p[0] ? sx(w, 16) : sx(w, 17); if (b.p[0]) b.xf[0] = sx(w, 16); break; }
        case 22: { Bitmap& b = bmp_[g_.handle & 31]; b.p[1] = (w >> 16) & 1; b.xf[1] = sx(w, 17); break; }
        case 23: bmp_[g_.handle & 31].xf[2] = sx(w, 24); break;
        case 24: { Bitmap& b = bmp_[g_.handle & 31]; b.p[2] = (w >> 16) & 1; b.xf[3] = sx(w, 17); break; }
        case 25: { Bitmap& b = bmp_[g_.handle & 31]; b.p[3] = (w >> 16) & 1; b.xf[4] = sx(w, 17); break; }
        case 26: bmp_[g_.handle & 31].xf[5] = sx(w, 24); break;
        case 27: g_.scX = int((w >> 11) & 2047); g_.scY = int(w & 2047); break;
        case 28: g_.scW = int((w >> 12) & 4095); g_.scH = int(w & 4095); break;
        case 31: flushPrim(); g_.prim = int(w & 15); break;
        case 33: flushPrim(); g_.prim = 0; break;
        case 34: if (sp_ < 4) stack_[sp_++] = g_; break;
        case 35: if (sp_ > 0) { const int h = g_.handle; (void)h; g_ = stack_[--sp_]; } break;
        case 38:
            if (w & 4)
            {
                const int xa = std::max(0, g_.scX), xb = std::min(kW, g_.scX + g_.scW), ya = std::max(0, g_.scY), yb = std::min(kH, g_.scY + g_.scH);
                const uint32_t c = (uint32_t(g_.clearColor[3]) << 24) | (uint32_t(g_.clearColor[0]) << 16) | (uint32_t(g_.clearColor[1]) << 8) | g_.clearColor[2];
                for (int y = ya; y < yb; y++) for (int x = xa; x < xb; x++) work_[size_t(y) * kW + x] = c;
            }
            break;
        case 39: g_.vfrac = int(std::min<uint32_t>(w & 7, 4)); break;
        case 43: g_.vtx = sx(w, 17); break;
        case 44: g_.vty = sx(w, 17); break;
        default: break;   // TAG, ALPHA_FUNC, STENCIL_*, COLOR_MASK, CALL/JUMP/RETURN/MACRO, PALETTE_SOURCE, NOP: not modelled
    }
}

void EveModel::draw(const uint32_t* words, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        const uint32_t w = words[i];
        if (w == kGradientMarker)
        {
            if (i + 4 < n)
            {
                const uint32_t p0 = words[i + 1], c0 = words[i + 2], p1 = words[i + 3], c1 = words[i + 4];
                gradient(int16_t(p0 & 0xFFFF), int16_t(p0 >> 16), c0, int16_t(p1 & 0xFFFF), int16_t(p1 >> 16), c1);
            }
            i += 4;
            continue;
        }
        if ((w >> 24) == 0x3D)
        {
            Bitmap& b = bmp_[(w >> 8) & 31];
            b.source = (b.source - (w & 0xFF) * b.stride * b.height) & 0x3FFFFF;
            continue;
        }
        if (w == 0 && i + 1 == n) break;
        doDlWord(w);
    }
}

// ------------------------------------------------------------------------------------------------------------------
bool EveModel::savePng(const char* path) const
{
    std::vector<uint8_t> raw;
    raw.reserve(size_t(kH) * (kW * 3 + 1));
    for (int y = 0; y < kH; y++)
    {
        raw.push_back(0);
        for (int x = 0; x < kW; x++)
        {
            const uint32_t p = frame_[size_t(y) * kW + x];
            raw.push_back(uint8_t(p >> 16)); raw.push_back(uint8_t(p >> 8)); raw.push_back(uint8_t(p));
        }
    }
    uLongf zl = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(zl);
    if (compress2(z.data(), &zl, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    auto be32 = [](uint32_t v, uint8_t* o) { o[0] = uint8_t(v >> 24); o[1] = uint8_t(v >> 16); o[2] = uint8_t(v >> 8); o[3] = uint8_t(v); };
    auto chunk = [&](const char* type, const uint8_t* d, uint32_t len) {
        uint8_t h[8]; be32(len, h); memcpy(h + 4, type, 4);
        fwrite(h, 1, 8, f);
        if (len) fwrite(d, 1, len, f);
        uLong crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
        if (len) crc = crc32(crc, d, len);
        uint8_t c[4]; be32(uint32_t(crc), c); fwrite(c, 1, 4, f);
    };
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13]; be32(kW, ihdr); be32(kH, ihdr + 4); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), uint32_t(zl));
    chunk("IEND", nullptr, 0);
    fclose(f);
    return true;
}
