// FT812 (EVE) model for the host UI renderer. NOT firmware.
//
// The firmware talks to the chip through the abstract DisplayDriver (core/include/DisplayDriver.h): the API_* layer in
// core/libs/FT812 pushes 32 bit words (display list commands and coprocessor commands) through WriteData32 and strings
// through sendString. This class is a DisplayDriver that executes those words:
//   - RAM_G (1 MB) with the fonts, logos and the control display lists the firmware stores there (CMD_MEMCPY from RAM_DL,
//     CMD_APPEND back into the display list),
//   - the 8 KB display list (RAM_DL) with the coprocessor commands expanded the way the widgets do (text, number, gradient,
//     bitmap matrix commands), CMD_SWAP renders it,
//   - a software rasteriser for the display list (RECTS, LINES, LINE_STRIP, FTPOINTS, BITMAPS, blend, scissor, bitmap
//     transform, save/restore context). Edges are anti-aliased from the signed distance at the pixel centre, which is close
//     to but not the same as the chip's 16 sample pattern.
// Not modelled: touch, tags, stencil, alpha test, palettes, video, audio, JPEG/PNG decode (CMD_LOADIMAGE data is dropped),
// the ROM fonts 16..31 (a stand-in glyph set, see rom_font below).
#pragma once

#include "DisplayDriver.h"

#include <stdint.h>
#include <string>
#include <vector>

class EveModel : public DisplayDriver
{
  public:
    static constexpr int kW = 800;
    static constexpr int kH = 480;
    static constexpr uint32_t kRamG = 1024 * 1024;
    static constexpr uint32_t kRamDl = 0x300000;   // RAM_DL address space
    static constexpr int kDlWords = 2048;

    EveModel();

    // ---- DisplayDriver
    void setPowerState(bool) override {}
    void setBrightness(int v) override { brightness_ = v; }
    void setRotation(int v) override { rotation_ = v; }
    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override;
    uint32_t getDisplayListOffset() override { return uint32_t(dl_.size()) * 4u; }
    void IncCMDOffset(uint16_t) override {}
    void WriteData32(uint32_t data) override;
    void beginCoProList() override {}
    void beginCoProListNoCheck() override {}
    void endCoProList() override { run(); }
    void awaitCoProEmpty() override { run(); }
    uint8_t isCoProEmpty() override { return 1; }
    void writeDataRAMG(const uint8_t* d, uint32_t n, uint32_t dst) override;
    void writeFontRAMG(const uint8_t* d, uint32_t n, uint32_t dst) override { writeDataRAMG(d, n, dst); }
    void readDataRAMG(uint8_t* d, uint32_t n, uint32_t src) override;
    void readBackDataRAMG(uint8_t* d, uint32_t n, uint32_t src) override { readDataRAMG(d, n, src); }
    uint8_t sendString(const char* s, uint32_t length) override;
    void writeDataToCMD(const uint8_t* d, uint32_t n) override;
    void initStreamDma(uint16_t, uint16_t, uint16_t, uint8_t*, uint32_t) override {}
    bool updateStreamDma(bool) override { return false; }
    void stopStreamDma() override {}

    // ---- host side
    const uint32_t* frame() const { return frame_.data(); }   // 0xAARRGGBB, kW x kH, the last CMD_SWAP
    int swaps() const { return swaps_; }
    bool savePng(const char* path) const;
    int errors() const { return errors_; }
    const std::string& log() const { return log_; }
    // statistics of what the firmware sent (the inventory)
    struct Stats
    {
        uint32_t cmd[256] = {};      // coprocessor commands by id (low byte)
        uint32_t dl[64] = {};        // display list opcodes by id
        uint32_t prim[16] = {};      // BEGIN primitives
        uint32_t fmt[32] = {};       // bitmap formats drawn
        uint32_t romTextCalls = 0;   // CMD_TEXT / NUMBER on a font without CMD_SETFONT2 (ROM font stand-in used)
        uint32_t dlOverflow = 0;
    } stats;

  private:
    struct Bitmap
    {
        uint32_t source = 0, format = 0, stride = 0, height = 0, width = 0, bheight = 0;
        uint32_t filter = 0, wrapx = 0, wrapy = 0;
        int32_t xf[6] = {256, 0, 0, 0, 256, 0};   // a b c d e f, 8.8 (c, f 16.8), p = 0
        bool p[4] = {};                            // 1.15 form of a b d e
    };
    struct Gfx
    {
        uint8_t color[4] = {255, 255, 255, 255};
        int blendSrc = 2, blendDst = 4;
        int lineWidth = 16, pointSize = 16;
        int vfrac = 4, vtx = 0, vty = 0;
        int scX = 0, scY = 0, scW = 2048, scH = 2048;
        int handle = 0, cell = 0;
        uint8_t clearColor[4] = {0, 0, 0, 0};
        int prim = 0;
    };
    struct FontInfo
    {
        uint32_t ptr = 0, first = 0;
        bool set = false;
    };

    void run();
    void exec(uint32_t id);
    void emit(uint32_t w);
    void emitVertex(int x, int y, int handle, int cell);
    void textRun(int x, int y, int font, int options, const std::string& s);
    void render();
    void doDlWord(uint32_t w);
    void draw(const uint32_t* words, size_t n);
    void putPixel(int x, int y, float r, float g, float b, float a);
    void rect(float x0, float y0, float x1, float y1, float rad);
    void capsule(float x0, float y0, float x1, float y1, float rad);
    void dot(float cx, float cy, float rad);
    void bitmapAt(int x, int y, int handle, int cell);
    void gradient(int x0, int y0, uint32_t c0, int x1, int y1, uint32_t c1);
    uint32_t fontWidth(int font, int ch, int* height, int* cellOut);
    bool romGlyph(int font, int ch, int* w, int* h, const uint8_t** px);
    void blend(int x, int y, float sr, float sg, float sb, float sa);

    std::vector<uint8_t> ramg_;
    std::vector<uint32_t> dl_;
    std::vector<uint32_t> in_;
    size_t inPos_ = 0;
    std::vector<uint32_t> frame_;
    std::vector<uint32_t> work_;
    Bitmap bmp_[32];
    FontInfo font_[32];
    Gfx g_, stack_[4];
    Bitmap bstack_[4][32];   // the handle table as SAVE_CONTEXT found it (the chip's graphics context holds BITMAP_SIZE and the transform)
    int sp_ = 0;
    int base_ = 10;
    // coprocessor matrix (16.16), used by LOADIDENTITY, TRANSLATE, ROTATE, SCALE, SETMATRIX
    int32_t m_[6] = {65536, 0, 0, 0, 65536, 0};
    // the last three words of the command fifo: CMD_GETPROPS result
    uint32_t props_[3] = {};
    uint32_t pendingVertex_[2] = {};
    int nPending_ = 0;
    int swaps_ = 0, errors_ = 0, brightness_ = 0, rotation_ = 0;
    std::string log_;
    // BEGIN state
    std::vector<std::pair<int, int>> verts_;
    void flushPrim();
    void vertex(int x, int y);
};
