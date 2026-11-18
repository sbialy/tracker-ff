#include "FT812.h"
#include "display.h"

#include "log.h"
#include <cr_section_macros.h>

static constexpr uint32_t address = 840000;

static constexpr uint16_t byte_res = 1;
static constexpr uint16_t max_byte_res = 1;

static constexpr uint16_t screen_width = 256; // 320;
static constexpr uint16_t screen_height = 240;

static uint16_t screen_x = (800 - (screen_width * 2)) / 2; // 240;
static uint16_t screen_y = 0;                              // 120;

__NOINIT(EXTERNAL_RAM)
uint8_t screen[screen_height * screen_width * max_byte_res] __attribute__((aligned(32)));

#define FPS_METER 1

#if FPS_METER
uint32_t fpsCounter;
elapsedMillis fpsTimer;
elapsedMicros tajner;
#endif

static bool frameDataSent = false;

void cDisplay::startVideoStream()
{
    displayDriver->initStreamDma(byte_res, screen_width, screen_height, screen, address);
}

void cDisplay::stopVideoStream()
{
    displayDriver->stopStreamDma();
}

void cDisplay::updateVideoStream(bool lastFrame)
{
#if FPS_METER
    if (fpsTimer > 1000)
    {
        fpsTimer = 0;
        LOGF("%d\n", fpsCounter);
        fpsCounter = 0;
    }
#endif

    if (!frameDataSent && displayDriver->updateStreamDma(lastFrame))
    {
        frameDataSent = true;
    }

    if (frameDataSent && showVideoStreamFrame())
    {
        frameDataSent = false;
#if FPS_METER
        fpsCounter++;
#endif
    }
}

bool cDisplay::showVideoStreamFrame()
{
    if (!displayDriver->isCoProEmpty())
        return false;

    displayDriver->beginCoProListNoCheck();

    API_CMD_DLSTART();

    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);
    API_VERTEX_FORMAT(0);

    API_SAVE_CONTEXT();
    API_COLOR(0x444444);
    API_BITMAP_HANDLE(fonts[3].handle);
    API_BEGIN(BITMAPS);

#ifndef TRACKER_MINI
    API_CELL('B');
    API_VERTEX2F(80, 450);

    API_CELL('A');
    API_VERTEX2F(110, 450);
#endif

    /*
    API_CELL('S');
    API_VERTEX2F(660, 450);
    API_CELL('E');
    API_VERTEX2F(672, 450);
    API_CELL('L');
    API_VERTEX2F(684, 450);

    API_CELL('S');
    API_VERTEX2F(705, 450);
    API_CELL('T');
    API_VERTEX2F(717, 450);
     */

    API_CELL('.');
    API_VERTEX2F(729, 450);
    API_CELL('.');
    API_VERTEX2F(741, 450);
    API_CELL('.');
    API_VERTEX2F(753, 450);

    API_END();
    API_RESTORE_CONTEXT();

    uint16_t format = 4UL;
    if (byte_res == 2)
        format = 7UL;

    API_CMD_SETBITMAP(address, format, screen_width, screen_height);

    API_BITMAP_TRANSFORM_A(128);
    API_BITMAP_TRANSFORM_E(128);

    API_BITMAP_SIZE(NEAREST, BORDER, BORDER, screen_width * 16, screen_height * 2);

    API_BEGIN(BITMAPS);
    API_VERTEX2F(screen_x, screen_y);
    API_END();

    API_DISPLAY();
    API_CMD_SWAP();

    displayDriver->endCoProList();

    return true;
}

uint16_t* cDisplay::getLineBuffer(int j)
{
    return ((uint16_t*)&screen[j * screen_width]);
}

/*
void cDisplay::writeScreen(int width, int height, int stride, uint8_t *buf, uint16_t *palette16)
{
  uint8_t *buffer=buf;
  uint8_t *src;

  if (screen != NULL) {
  uint16_t *dst = (uint16_t*)&screen[0];
    int i,j;
    if (width*2 <= screen_width) {
      for (j=0; j<height; j++)
      {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
          *dst++ = val;
          *dst++ = val;
        }
        dst += (screen_width-width*2);
        if (height*2 <= screen_height) {
          src=buffer;
          for (i=0; i<width; i++)
          {
            uint16_t val = palette16[*src++];
            *dst++ = val;
            *dst++ = val;
          }
          dst += (screen_width-width*2);
        }
        buffer += stride;
      }
    }
    else if (width <= screen_width) {
      dst += (screen_width-width)/2;
      for (j=0; j<height; j++)
      {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
          *dst++ = val;
        }
        dst += (screen_width-width);
        if (height*2 <= screen_height) {
          src=buffer;
          for (i=0; i<width; i++)
          {
            uint16_t val = palette16[*src++];
            *dst++ = val;
          }
          dst += (screen_width-width);
        }
        buffer += stride;
      }
    }
  }

}
*/

void cDisplay::writeLine(int width,
                         int /*height*/,
                         int stride,
                         uint8_t* buf,
                         uint8_t* palette8,
                         uint16_t* palette16)
{
    if (byte_res == 2)
    {
        uint8_t* src = buf;
        uint16_t* dst = (uint16_t*)&screen[screen_width * stride * 2];
        for (int i = 0; i < width; i++)
        {
            uint8_t val = *src++;
            *dst++ = palette16[val];
        }
    }
    else
    {
        uint8_t* src = buf;
        uint8_t* dst = (uint8_t*)&screen[screen_width * stride];
        for (int i = 0; i < width; i++)
        {
            uint8_t val = *src++;
            *dst++ = palette8[val];
        }
    }
}

uint16_t* cDisplay::getFrameBuffer(void)
{
    return ((uint16_t*)screen);
}
