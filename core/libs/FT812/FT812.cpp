/*
********************************************************************************
*             FT81x simple library for PIC MCU                                 *
********************************************************************************
Copyright (c) Future Technology Devices International / BridgeTek

THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FTDI DRIVERS MAY BE USED ONLY IN CONJUNCTION WITH PRODUCTS BASED ON FTDI PARTS.

FTDI DRIVERS MAY BE DISTRIBUTED IN ANY FORM AS LONG AS LICENSE INFORMATION IS NOT MODIFIED.

IF A CUSTOM VENDOR ID AND/OR PRODUCT ID OR DESCRIPTION STRING ARE USED, IT IS THE
RESPONSIBILITY OF THE PRODUCT MANUFACTURER TO MAINTAIN ANY CHANGES AND SUBSEQUENT WHQL
RE-CERTIFICATION AS A RESULT OF MAKING THESE CHANGES.

********************************************************************************
* This code is provided as an example only and is not guaranteed by FTDI.
* FTDI accept no responsibility for any issues resulting from its use.
* The developer of the final application incorporating any parts of this
* sample project is responsible for ensuring its safe and correct operation
* and for any consequences resulting from its use.
*
* Rev       Date        Author          Comment
********************************************************************************
* 1.0       June 2017	G Brown         Initial Release
* 1.1       Feb  2018   G Brown         No changes to this file
*  *****************************************************************************
*/

#include "FT812.h"
#include "display.h"

//##############################################################################
// Display List commands for co-processor
//##############################################################################

void API_CLEAR_COLOR_RGB(uint8_t R, uint8_t G, uint8_t B)
{
    display.getDisplayDriver()->WriteData32(CLEAR_COLOR_RGB(R, G, B));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CLEAR_COLOR(uint32_t RGB)
{
    display.getDisplayDriver()->WriteData32((2UL << 24) | (RGB & 0x00FFFFFF));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CLEAR(uint8_t C, uint8_t S, uint8_t T)
{
    display.getDisplayDriver()->WriteData32(CLEAR((C & 0x01), (S & 0x01), (T & 0x01)));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_COLOR_RGB(uint8_t R, uint8_t G, uint8_t B)
{
    display.getDisplayDriver()->WriteData32(COLOR_RGB(R, G, B));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_COLOR(uint32_t RGB)
{
    display.getDisplayDriver()->WriteData32((4UL << 24) | (RGB & 0x00FFFFFF));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_VERTEX2F(int16_t x, int16_t y)
{
    display.getDisplayDriver()->WriteData32(VERTEX2F(x, y));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_VERTEX2II(uint16_t x, uint16_t y, uint8_t handle, uint8_t cell)
{
    display.getDisplayDriver()->WriteData32(VERTEX2II(x, y, handle, cell));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_HANDLE(uint8_t handle)
{
    display.getDisplayDriver()->WriteData32(BITMAP_HANDLE(handle));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_SOURCE(uint32_t addr)
{
    display.getDisplayDriver()->WriteData32(BITMAP_SOURCE(addr));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_LAYOUT(uint8_t format, uint16_t linestride, uint16_t height)
{
    display.getDisplayDriver()->WriteData32(BITMAP_LAYOUT(format, linestride, height));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_SIZE(uint8_t filter, uint8_t wrapx, uint8_t wrapy, uint16_t width, uint16_t height)
{
    display.getDisplayDriver()->WriteData32(BITMAP_SIZE(filter, wrapx, wrapy, width, height));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CELL(uint8_t cell)
{
    display.getDisplayDriver()->WriteData32(CELL(cell));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_TAG(uint8_t s)
{
    display.getDisplayDriver()->WriteData32(TAG(s));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_ALPHA_FUNC(uint8_t func, uint8_t ref)
{
    display.getDisplayDriver()->WriteData32(ALPHA_FUNC(func, ref));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_STENCIL_FUNC(uint8_t func, uint8_t ref, uint8_t mask)
{
    display.getDisplayDriver()->WriteData32(STENCIL_FUNC(func, ref, mask));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BLEND_FUNC(uint8_t src, uint8_t dst)
{
    display.getDisplayDriver()->WriteData32(BLEND_FUNC(src, dst));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_STENCIL_OP(uint8_t sfail, uint8_t spass)
{
    display.getDisplayDriver()->WriteData32(STENCIL_OP(sfail, spass));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_POINT_SIZE(uint16_t size)
{
    display.getDisplayDriver()->WriteData32(POINT_SIZE(size));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_LINE_WIDTH(uint16_t width)
{
    display.getDisplayDriver()->WriteData32(LINE_WIDTH(width));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CLEAR_COLOR_A(uint8_t alpha)
{
    display.getDisplayDriver()->WriteData32(CLEAR_COLOR_A(alpha));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_COLOR_A(uint8_t alpha)
{
    display.getDisplayDriver()->WriteData32(COLOR_A(alpha));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CLEAR_STENCIL(uint8_t s)
{
    display.getDisplayDriver()->WriteData32(CLEAR_STENCIL(s));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CLEAR_TAG(uint8_t s)
{
    display.getDisplayDriver()->WriteData32(CLEAR_TAG(s));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_STENCIL_MASK(uint8_t mask)
{
    display.getDisplayDriver()->WriteData32(STENCIL_MASK(mask));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_TAG_MASK(uint8_t mask)
{
    display.getDisplayDriver()->WriteData32(TAG_MASK(mask));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_SCISSOR_XY(uint16_t x, uint16_t y)
{
    display.getDisplayDriver()->WriteData32(SCISSOR_XY(x, y));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_SCISSOR_SIZE(uint16_t width, uint16_t height)
{
    display.getDisplayDriver()->WriteData32(SCISSOR_SIZE(width, height));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_CALL(uint16_t dest)
{
    display.getDisplayDriver()->WriteData32(CALL(dest));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_JUMP(uint16_t dest)
{
    display.getDisplayDriver()->WriteData32(JUMP(dest));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BEGIN(uint8_t prim)
{
    display.getDisplayDriver()->WriteData32(BEGIN(prim));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_COLOR_MASK(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    display.getDisplayDriver()->WriteData32(COLOR_MASK(r, g, b, a));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_END(void)
{
    display.getDisplayDriver()->WriteData32(END());
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_SAVE_CONTEXT(void)
{
    display.getDisplayDriver()->WriteData32(SAVE_CONTEXT());
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_RESTORE_CONTEXT(void)
{
    display.getDisplayDriver()->WriteData32(RESTORE_CONTEXT());
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_RETURN(void)
{
    display.getDisplayDriver()->WriteData32(RETURN());
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_MACRO(uint8_t m)
{
    display.getDisplayDriver()->WriteData32(MACRO(m));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_DISPLAY(void)
{
    display.getDisplayDriver()->WriteData32(FT_DISPLAY());
    display.getDisplayDriver()->IncCMDOffset(4);
}

//##############################################################################
// Co-Processor Widgets
//##############################################################################

// ft_void_t Ft_Gpu_CoCmd_Text(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_TEXT(int16_t x,
                  int16_t y,
                  int16_t font,
                  uint16_t options,
                  const char* string,
                  uint32_t length)
{
    uint32_t command_size = 0;
    uint32_t StringLength = 0;

    display.getDisplayDriver()->WriteData32(CMD_TEXT);
    display.getDisplayDriver()->WriteData32(((uint32_t)y << 16) | (x & 0xffff));
    display.getDisplayDriver()->WriteData32(((uint32_t)options << 16) | (font & 0xffff));
    command_size = (command_size + 12);

    StringLength = display.getDisplayDriver()->sendString(string, length);
    command_size = (command_size + StringLength);

    display.getDisplayDriver()->IncCMDOffset(command_size);
}

void API_CMD_TEXT(int16_t x, int16_t y, int16_t font, uint16_t options, const char* string)
{
    API_CMD_TEXT(x, y, font, options, string, 0);
}

// ft_void_t Ft_Gpu_CoCmd_Button(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_int16_t font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_BUTTON(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    int16_t font,
                    uint16_t options,
                    const char* string)
{
    uint32_t command_size = 0, StringLength = 0;

    display.getDisplayDriver()->WriteData32(CMD_BUTTON);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (font & 0xffff)));
    command_size = (command_size + 16);

    StringLength = display.getDisplayDriver()->sendString(string, 0);
    command_size = (command_size + StringLength);

    display.getDisplayDriver()->IncCMDOffset(command_size);
}

// ft_void_t Ft_Gpu_CoCmd_Keys(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t w,
// ft_int16_t h, ft_int16_t font, ft_uint16_t options, const ft_char8_t* s);
void API_CMD_KEYS(int16_t x,
                  int16_t y,
                  int16_t w,
                  int16_t h,
                  int16_t font,
                  uint16_t options,
                  const char* string)
{
    //    uint32_t length = strlen(string);
    uint32_t command_size = 0, StringLength = 0;

    display.getDisplayDriver()->WriteData32(CMD_KEYS);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (font & 0xffff)));
    command_size = (command_size + 16);

    StringLength = display.getDisplayDriver()->sendString(string, 0);
    command_size = (command_size + StringLength);

    display.getDisplayDriver()->IncCMDOffset(command_size);
}

// ft_void_t Ft_Gpu_CoCmd_Number(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// font, ft_uint16_t options, ft_int32_t n);
void API_CMD_NUMBER(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    display.getDisplayDriver()->WriteData32(CMD_NUMBER);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (font & 0xffff)));
    display.getDisplayDriver()->WriteData32(n);
    display.getDisplayDriver()->IncCMDOffset(16);
}

/*
void API_CMD_NUMBER_2(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    if()


    display.getDisplayDriver()->WriteData32(CMD_NUMBER);
    display.getDisplayDriver()->WriteData32( (((uint32_t)y<<16)|(x & 0xffff)));
    display.getDisplayDriver()->WriteData32( (((uint32_t)options<<16)|(font&0xffff)));
    display.getDisplayDriver()->WriteData32( n);
    display.getDisplayDriver()->IncCMDOffset(16);


}
*/

// ft_void_t Ft_Gpu_CoCmd_LoadIdentity(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_LOADIDENTITY(void)
{
    display.getDisplayDriver()->WriteData32(CMD_LOADIDENTITY);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Toggle(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t font, ft_uint16_t options, ft_uint16_t state, const ft_char8_t* s);
void API_CMD_TOGGLE(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t font,
                    uint16_t options,
                    uint16_t state,
                    const char* string)
{
    // uint32_t length = strlen(string);
    uint32_t command_size = 0, StringLength = 0;

    display.getDisplayDriver()->WriteData32(CMD_TOGGLE);
    display.getDisplayDriver()->WriteData32(((uint32_t)y << 16) | (x & 0xffff));
    display.getDisplayDriver()->WriteData32(((uint32_t)font << 16) | (w & 0xffff));
    display.getDisplayDriver()->WriteData32(((uint32_t)state << 16) | options);
    command_size = (command_size + 16);

    StringLength = display.getDisplayDriver()->sendString(string, 0);
    command_size = (command_size + StringLength);

    display.getDisplayDriver()->IncCMDOffset(command_size);
}

/* Error handling for val is not done, so better to always use range of 65535 in order that needle
 * is drawn within display region */
// ft_void_t Ft_Gpu_CoCmd_Gauge(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// r, ft_uint16_t options, ft_uint16_t major, ft_uint16_t minor, ft_uint16_t val, ft_uint16_t
// range);
void API_CMD_GAUGE(int16_t x,
                   int16_t y,
                   int16_t r,
                   uint16_t options,
                   uint16_t major,
                   uint16_t minor,
                   uint16_t val,
                   uint16_t range)
{
    display.getDisplayDriver()->WriteData32(CMD_GAUGE);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (r & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)minor << 16) | (major & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)range << 16) | (val & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_RegRead(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t result);
void API_CMD_REGREAD(uint32_t ptr, uint32_t /*result*/)
{
    display.getDisplayDriver()->WriteData32(CMD_REGREAD);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(0);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_GetProps(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t w,
// ft_uint32_t h);
void API_CMD_GETPROPS(uint32_t ptr, uint32_t w, uint32_t h)
{
    display.getDisplayDriver()->WriteData32(CMD_GETPROPS);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(w);
    display.getDisplayDriver()->WriteData32(h);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_Memcpy(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t dest, ft_uint32_t src,
// ft_uint32_t num);
void API_CMD_MEMCPY(uint32_t dest, uint32_t src, uint32_t num)
{
    display.getDisplayDriver()->WriteData32(CMD_MEMCPY);
    display.getDisplayDriver()->WriteData32(dest);
    display.getDisplayDriver()->WriteData32(src);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_Spinner(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_uint16_t style, ft_uint16_t scale);
void API_CMD_SPINNER(int16_t x, int16_t y, uint16_t style, uint16_t scale)
{
    display.getDisplayDriver()->WriteData32(CMD_SPINNER);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)scale << 16) | (style & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_BgColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_BGCOLOR(uint32_t c)
{
    display.getDisplayDriver()->WriteData32(CMD_BGCOLOR);
    display.getDisplayDriver()->WriteData32(c);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_Swap(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SWAP(void)
{
    display.getDisplayDriver()->WriteData32(CMD_SWAP);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Inflate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr);
void API_CMD_INFLATE(uint32_t ptr)
{
    display.getDisplayDriver()->WriteData32(CMD_INFLATE);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_Translate(Ft_Gpu_Hal_Context_t *phost,ft_int32_t tx, ft_int32_t ty);
void API_CMD_TRANSLATE(int32_t tx, int32_t ty)
{
    display.getDisplayDriver()->WriteData32(CMD_TRANSLATE);
    display.getDisplayDriver()->WriteData32(tx);
    display.getDisplayDriver()->WriteData32(ty);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Stop(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_STOP(void)
{
    display.getDisplayDriver()->WriteData32(CMD_STOP);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Slider(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t range);
void API_CMD_SLIDER(int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    uint16_t options,
                    uint16_t val,
                    uint16_t range)
{
    display.getDisplayDriver()->WriteData32(CMD_SLIDER);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)val << 16) | (options & 0xffff)));
    display.getDisplayDriver()->WriteData32(range);
    display.getDisplayDriver()->IncCMDOffset(20);
}

void API_BITMAP_TRANSFORM_A(long a)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_A(a)); //    ((21UL<<24)|(((a)&131071UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}
void API_BITMAP_TRANSFORM_B(long b)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_B(b)); //  ((22UL<<24)|(((b)&131071UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}
void API_BITMAP_TRANSFORM_C(long c)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_C(c)); //  ((23UL<<24)|(((c)&16777215UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}
void API_BITMAP_TRANSFORM_D(long d)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_D(d)); //   ((24UL<<24)|(((d)&131071UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}
void API_BITMAP_TRANSFORM_E(long e)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_E(e)); //   ((25UL<<24)|(((e)&131071UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}
void API_BITMAP_TRANSFORM_F(long f)
{
    display.getDisplayDriver()->WriteData32(
      BITMAP_TRANSFORM_F(f)); //  ((26UL<<24)|(((f)&16777215UL)<<0))
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Interrupt(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ms);
void API_CMD_INTERRUPT(uint32_t ms)
{
    display.getDisplayDriver()->WriteData32(CMD_INTERRUPT);
    display.getDisplayDriver()->WriteData32(ms);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_FgColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_FGCOLOR(uint32_t c)
{
    display.getDisplayDriver()->WriteData32(CMD_FGCOLOR);
    display.getDisplayDriver()->WriteData32(c);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_Rotate(Ft_Gpu_Hal_Context_t *phost,ft_int32_t a);
void API_CMD_ROTATE(int32_t a)
{
    display.getDisplayDriver()->WriteData32(CMD_ROTATE);
    display.getDisplayDriver()->WriteData32(a);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_MemWrite(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_MEMWRITE(uint32_t ptr, uint32_t num)
{
    display.getDisplayDriver()->WriteData32(CMD_MEMWRITE);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Scrollbar(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_int16_t w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t size, ft_uint16_t
// range);
void API_CMD_SCROLLBAR(int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       uint16_t options,
                       uint16_t val,
                       uint16_t size,
                       uint16_t range)
{
    display.getDisplayDriver()->WriteData32(CMD_SCROLLBAR);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)val << 16) | (options & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)range << 16) | (size & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_GetMatrix(Ft_Gpu_Hal_Context_t *phost,ft_int32_t a, ft_int32_t b,
// ft_int32_t c, ft_int32_t d, ft_int32_t e, ft_int32_t f);
void API_CMD_GETMATRIX(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
{
    display.getDisplayDriver()->WriteData32(CMD_GETMATRIX);
    display.getDisplayDriver()->WriteData32(a);
    display.getDisplayDriver()->WriteData32(b);
    display.getDisplayDriver()->WriteData32(c);
    display.getDisplayDriver()->WriteData32(d);
    display.getDisplayDriver()->WriteData32(e);
    display.getDisplayDriver()->WriteData32(f);
    display.getDisplayDriver()->IncCMDOffset(28);
}

// ft_void_t Ft_Gpu_CoCmd_Sketch(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_uint16_t
// w, ft_uint16_t h, ft_uint32_t ptr, ft_uint16_t format);
void API_CMD_SKETCH(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t ptr, uint16_t format)
{
    display.getDisplayDriver()->WriteData32(CMD_SKETCH);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(format);
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_MemSet(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t value,
// ft_uint32_t num);
void API_CMD_MEMSET(uint32_t ptr, uint32_t value, uint32_t num)
{
    display.getDisplayDriver()->WriteData32(CMD_MEMSET);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(value);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_GradColor(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t c);
void API_CMD_GRADCOLOR(uint32_t c)
{
    display.getDisplayDriver()->WriteData32(CMD_GRADCOLOR);
    display.getDisplayDriver()->WriteData32(c);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_Bitmap_Transform(Ft_Gpu_Hal_Context_t *phost,ft_int32_t x0, ft_int32_t y0,
// ft_int32_t x1, ft_int32_t y1, ft_int32_t x2, ft_int32_t y2, ft_int32_t tx0, ft_int32_t ty0,
// ft_int32_t tx1, ft_int32_t ty1, ft_int32_t tx2, ft_int32_t ty2, ft_uint16_t result);
void API_CMD_BITMAP_TRANSFORM(int32_t x0,
                              int32_t y0,
                              int32_t x1,
                              int32_t y1,
                              int32_t x2,
                              int32_t y2,
                              int32_t tx0,
                              int32_t ty0,
                              int32_t tx1,
                              int32_t ty1,
                              int32_t tx2,
                              int32_t ty2,
                              uint16_t result)
{
    display.getDisplayDriver()->WriteData32(CMD_BITMAP_TRANSFORM);
    display.getDisplayDriver()->WriteData32(x0);
    display.getDisplayDriver()->WriteData32(y0);
    display.getDisplayDriver()->WriteData32(x1);
    display.getDisplayDriver()->WriteData32(y1);
    display.getDisplayDriver()->WriteData32(x2);
    display.getDisplayDriver()->WriteData32(y2);
    display.getDisplayDriver()->WriteData32(tx0);
    display.getDisplayDriver()->WriteData32(ty0);
    display.getDisplayDriver()->WriteData32(tx1);
    display.getDisplayDriver()->WriteData32(ty1);
    display.getDisplayDriver()->WriteData32(tx2);
    display.getDisplayDriver()->WriteData32(ty2);
    display.getDisplayDriver()->WriteData32(result);
    display.getDisplayDriver()->IncCMDOffset(56);
}

// ft_void_t Ft_Gpu_CoCmd_Calibrate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t result);
void API_CMD_CALIBRATE(uint32_t result)
{
    display.getDisplayDriver()->WriteData32(CMD_CALIBRATE);
    display.getDisplayDriver()->WriteData32(result);
    display.getDisplayDriver()->IncCMDOffset(8);
    // Ft_Gpu_Hal_WaitCmdfifo_empty(phost);
}

// ft_void_t Ft_Gpu_CoCmd_SetFont(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t ptr);
void API_CMD_SETFONT(uint32_t font, uint32_t ptr)
{
    display.getDisplayDriver()->WriteData32(CMD_SETFONT);
    display.getDisplayDriver()->WriteData32(font);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Logo(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_LOGO(void)
{
    display.getDisplayDriver()->WriteData32(CMD_LOGO);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Append(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_APPEND(uint32_t ptr, uint32_t num)
{
    display.getDisplayDriver()->WriteData32(CMD_APPEND);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_MemZero(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num);
void API_CMD_MEMZERO(uint32_t ptr, uint32_t num)
{
    display.getDisplayDriver()->WriteData32(CMD_MEMZERO);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Scale(Ft_Gpu_Hal_Context_t *phost,ft_int32_t sx, ft_int32_t sy);
void API_CMD_SCALE(int32_t sx, int32_t sy)
{
    display.getDisplayDriver()->WriteData32(CMD_SCALE);
    display.getDisplayDriver()->WriteData32(sx);
    display.getDisplayDriver()->WriteData32(sy);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Clock(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// r, ft_uint16_t options, ft_uint16_t h, ft_uint16_t m, ft_uint16_t s, ft_uint16_t ms);
void API_CMD_CLOCK(int16_t x,
                   int16_t y,
                   int16_t r,
                   uint16_t options,
                   uint16_t h,
                   uint16_t m,
                   uint16_t s,
                   uint16_t ms)
{
    display.getDisplayDriver()->WriteData32(CMD_CLOCK);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (r & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)m << 16) | (h & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)ms << 16) | (s & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_Gradient(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x0, ft_int16_t y0,
// ft_uint32_t rgb0, ft_int16_t x1, ft_int16_t y1, ft_uint32_t rgb1);
void API_CMD_GRADIENT(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
    display.getDisplayDriver()->WriteData32(CMD_GRADIENT);
    display.getDisplayDriver()->WriteData32((((uint32_t)y0 << 16) | (x0 & 0xffff)));
    display.getDisplayDriver()->WriteData32(rgb0);
    display.getDisplayDriver()->WriteData32((((uint32_t)y1 << 16) | (x1 & 0xffff)));
    display.getDisplayDriver()->WriteData32(rgb1);
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_SetMatrix(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SETMATRIX(void)
{
    display.getDisplayDriver()->WriteData32(CMD_SETMATRIX);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Track(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t
// w, ft_int16_t h, ft_int16_t tag);
void API_CMD_TRACK(int16_t x, int16_t y, int16_t w, int16_t h, int16_t tag)
{
    display.getDisplayDriver()->WriteData32(CMD_TRACK);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32(tag);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_GetPtr(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t result);
void API_CMD_GETPTR(uint32_t result)
{
    display.getDisplayDriver()->WriteData32(CMD_GETPTR);
    display.getDisplayDriver()->WriteData32(result);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_Progress(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_int16_t w, ft_int16_t h, ft_uint16_t options, ft_uint16_t val, ft_uint16_t range);
void API_CMD_PROGRESS(int16_t x,
                      int16_t y,
                      int16_t w,
                      int16_t h,
                      uint16_t options,
                      uint16_t val,
                      uint16_t range)
{
    display.getDisplayDriver()->WriteData32(CMD_PROGRESS);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)val << 16) | (options & 0xffff)));
    display.getDisplayDriver()->WriteData32(range);
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_ColdStart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_COLDSTART(void)
{
    display.getDisplayDriver()->WriteData32(CMD_COLDSTART);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Dial(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y, ft_int16_t r,
// ft_uint16_t options, ft_uint16_t val);
void API_CMD_DIAL(int16_t x, int16_t y, int16_t r, uint16_t options, uint16_t val)
{
    display.getDisplayDriver()->WriteData32(CMD_DIAL);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)options << 16) | (r & 0xffff)));
    display.getDisplayDriver()->WriteData32(val);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_LoadImage(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t
// options);
void API_CMD_LOADIMAGE(uint32_t ptr, uint32_t options)
{
    display.getDisplayDriver()->WriteData32(CMD_LOADIMAGE);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(options);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Dlstart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_DLSTART(void)
{
    display.getDisplayDriver()->WriteData32(CMD_DLSTART);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Snapshot(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr);
void API_CMD_SNAPSHOT(uint32_t ptr)
{
    display.getDisplayDriver()->WriteData32(CMD_SNAPSHOT);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_ScreenSaver(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SCREENSAVER(void)
{
    display.getDisplayDriver()->WriteData32(CMD_SCREENSAVER);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_Memcrc(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t num,
// ft_uint32_t result);
void API_CMD_MEMCRC(uint32_t ptr, uint32_t num, uint32_t result)
{
    display.getDisplayDriver()->WriteData32(CMD_MEMCRC);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(num);
    display.getDisplayDriver()->WriteData32(result);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// FT81X-only features
#ifdef FT_81X_ENABLE

// ###############       GPU        ############################################

void API_VERTEX_FORMAT(uint8_t frac)
{
    display.getDisplayDriver()->WriteData32(VERTEX_FORMAT(frac));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_LAYOUT_H(uint8_t linestride, uint8_t height)
{
    display.getDisplayDriver()->WriteData32(BITMAP_LAYOUT_H(linestride, height));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_BITMAP_SIZE_H(uint8_t width, uint8_t height)
{
    display.getDisplayDriver()->WriteData32(BITMAP_SIZE_H(width, height));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_PALETTE_SOURCE(uint32_t addr)
{
    display.getDisplayDriver()->WriteData32(PALETTE_SOURCE(addr));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_VERTEX_TRANSLATE_X(uint32_t x)
{
    display.getDisplayDriver()->WriteData32(VERTEX_TRANSLATE_X(x));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_VERTEX_TRANSLATE_Y(uint32_t y)
{
    display.getDisplayDriver()->WriteData32(VERTEX_TRANSLATE_Y(y));
    display.getDisplayDriver()->IncCMDOffset(4);
}

void API_NOP(void)
{
    display.getDisplayDriver()->WriteData32(NOP());
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ###############       CO-PRO        #########################################

// ft_void_t Ft_Gpu_CoCmd_SetRotate(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t r);
void API_CMD_SETROTATE(uint32_t r)
{
    display.getDisplayDriver()->WriteData32(CMD_SETROTATE);
    display.getDisplayDriver()->WriteData32(r);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_SetFont2(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t ptr,
// ft_uint32_t firstchar);
void API_CMD_SETFONT2(uint32_t font, uint32_t ptr, uint32_t firstchar)
{
    display.getDisplayDriver()->WriteData32(CMD_SETFONT2);
    display.getDisplayDriver()->WriteData32(font);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(firstchar);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_Snapshot2(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t fmt, ft_uint32_t ptr,
// ft_int16_t x, ft_int16_t y, ft_int16_t w, ft_int16_t h);
void API_CMD_SNAPSHOT2(uint32_t fmt, uint32_t ptr, int16_t x, int16_t y, int16_t w, int16_t h)
{
    display.getDisplayDriver()->WriteData32(CMD_SNAPSHOT2);
    display.getDisplayDriver()->WriteData32(fmt);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_MediaFifo(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t size);
void API_CMD_MEDIAFIFO(uint32_t ptr, uint32_t size)
{
    display.getDisplayDriver()->WriteData32(CMD_MEDIAFIFO);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(size);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Int_SWLoadImage(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t ptr, ft_uint32_t
// options);
void API_CMD_INT_SWLOADIMAGE(uint32_t ptr, uint32_t options)
{
    display.getDisplayDriver()->WriteData32(CMD_INT_SWLOADIMAGE);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32(options);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_Sync(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_SYNC(void)
{
    display.getDisplayDriver()->WriteData32(CMD_SYNC);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_CSketch(Ft_Gpu_Hal_Context_t *phost,ft_int16_t x, ft_int16_t y,
// ft_uint16_t w, ft_uint16_t h, ft_uint32_t ptr, ft_uint16_t format,ft_uint16_t freq);
void API_CMD_CSKETCH(int16_t x,
                     int16_t y,
                     uint16_t w,
                     uint16_t h,
                     uint32_t ptr,
                     uint16_t format,
                     uint16_t freq)
{
    display.getDisplayDriver()->WriteData32(CMD_CSKETCH);
    display.getDisplayDriver()->WriteData32((((uint32_t)y << 16) | (x & 0xffff)));
    display.getDisplayDriver()->WriteData32((((uint32_t)h << 16) | (w & 0xffff)));
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->WriteData32((((uint32_t)freq << 16) | (format & 0xffff)));
    display.getDisplayDriver()->IncCMDOffset(20);
}

// ft_void_t Ft_Gpu_CoCmd_RomFont(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t font, ft_uint32_t
// romslot);
void API_CMD_ROMFONT(uint32_t font, uint32_t romslot)
{
    display.getDisplayDriver()->WriteData32(CMD_ROMFONT);
    display.getDisplayDriver()->WriteData32(font);
    display.getDisplayDriver()->WriteData32(romslot);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_PlayVideo(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t options);
void API_CMD_PLAYVIDEO(uint32_t options)
{
    display.getDisplayDriver()->WriteData32(CMD_PLAYVIDEO);
    display.getDisplayDriver()->WriteData32(options);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_VideoFrame(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t dst, ft_uint32_t ptr);
void API_CMD_VIDEOFRAME(uint32_t dst, uint32_t ptr)
{
    display.getDisplayDriver()->WriteData32(CMD_VIDEOFRAME);
    display.getDisplayDriver()->WriteData32(dst);
    display.getDisplayDriver()->WriteData32(ptr);
    display.getDisplayDriver()->IncCMDOffset(12);
}

// ft_void_t Ft_Gpu_CoCmd_VideoStart(Ft_Gpu_Hal_Context_t *phost);
void API_CMD_VIDEOSTART(void)
{
    display.getDisplayDriver()->WriteData32(CMD_VIDEOSTART);
    display.getDisplayDriver()->IncCMDOffset(4);
}

// ft_void_t Ft_Gpu_CoCmd_SetBase(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t base);
void API_CMD_SETBASE(uint32_t base)
{
    display.getDisplayDriver()->WriteData32(CMD_SETBASE);
    display.getDisplayDriver()->WriteData32(base);
    display.getDisplayDriver()->IncCMDOffset(8);
}

// ft_void_t Ft_Gpu_CoCmd_SetBitmap(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t source, ft_uint16_t fmt,
// ft_uint16_t w, ft_uint16_t h);
void API_CMD_SETBITMAP(uint32_t source, uint16_t fmt, uint16_t w, uint16_t h)
{
    display.getDisplayDriver()->WriteData32(CMD_SETBITMAP);
    display.getDisplayDriver()->WriteData32(source);
    display.getDisplayDriver()->WriteData32((((uint32_t)w << 16) | (fmt & 0xffff)));
    display.getDisplayDriver()->WriteData32(h);
    display.getDisplayDriver()->IncCMDOffset(16);
}

// ft_void_t Ft_Gpu_CoCmd_SetScratch(Ft_Gpu_Hal_Context_t *phost,ft_uint32_t handle);
void API_CMD_SETSCRATCH(uint32_t handle)
{
    display.getDisplayDriver()->WriteData32(CMD_SETSCRATCH);
    display.getDisplayDriver()->WriteData32(handle);
    display.getDisplayDriver()->IncCMDOffset(8);
}
#endif // FT81X-only features
