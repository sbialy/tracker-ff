/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "output_i2s.h"
#include "config.h"
#include "memcpy_audio.h"

#include <string.h>

int16_t AudioOutputI2S::buffer_left[kAudioBlockSamples * 2];
int16_t AudioOutputI2S::buffer_right[kAudioBlockSamples * 2];
void (*AudioOutputI2S::updateBuffersFunc)(void* left, void* right) = nullptr;

// The output ring (install-crackle): 8 blocks the DMA plays in a loop, one major loop per block (an interrupt per block), the
// source address wrapped by SMOD 12 (4,096 B, so the ring is 4,096 B aligned: its own section at the start of the RAM,
// mk66fx1m0.ld). The interrupt renders the block after the last one it wrote. Normally that is the block after the one playing,
// the latency of the old ping-pong buffer. While a FlexNVM sector erase is coming (synth-open/plug_nvm.cpp) the installer raises
// ff_audio_lead_target and the interrupt renders ahead, one extra block per interrupt, so the DMA has blocks to play while the
// erase holds every interrupt off (6.3 to 7.4 ms measured). Afterwards it renders nothing until the lead is back to one.
constexpr uint32_t kRingBlocks = 8;
constexpr uint32_t kBlockBytes = kAudioBlockSamples * 4;   // 16 bit stereo
static_assert(kRingBlocks * kBlockBytes == 4096, "SMOD 12 below");
__attribute__((section(".dmaring"), aligned(4096), used)) static uint32_t i2s_ring[kRingBlocks * kAudioBlockSamples];

DMAChannel AudioOutputI2S::dma(false);

// Audio block timing for the FlexNVM installer (synth-open/plug_nvm.cpp) and the measure of the install crackle.
// ff_audio_late counts blocks the DMA played before they were written (the interrupt came too late, the DMA replayed an old
// block). ff_audio_gap_max: the longest time between two interrupts (DWT cycles, 180 MHz, 90 MHz while the installer is out of
// HSRUN, so a masked window counts half). ff_audio_lead: blocks written after the one playing, at the end of the last interrupt.
extern "C"
{
volatile uint32_t ff_audio_blocks;
volatile uint32_t ff_audio_end_cyc;
volatile uint32_t ff_audio_isr_cyc;
volatile uint32_t ff_audio_gap_max;
volatile uint32_t ff_audio_late;
volatile uint32_t ff_audio_lead;
volatile uint32_t ff_audio_lead_target = 1;
}
namespace
{
volatile uint32_t* const kCyc = reinterpret_cast<volatile uint32_t*>(0xE0001004);   // DWT_CYCCNT
// one block at 48 MHz / 1088 = 44,117.6 Hz (the MCLK below): 522,240 cycles at 180 MHz
constexpr uint32_t kPeriodCyc = uint32_t(uint64_t(F_CPU) * kAudioBlockSamples * 1088 / 48000000ull);
uint32_t s_lastStart;   // DWT at the last interrupt
uint32_t s_rd;          // the block the DMA played at the last interrupt (counts up, ring block = s_rd % kRingBlocks)
uint32_t s_wr;          // the next block to write
bool s_started;

void setupTcd(DMAChannel& dma)
{
    memset(i2s_ring, 0, sizeof(i2s_ring));
    dma.TCD->SADDR = i2s_ring;
    dma.TCD->SOFF = 2;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1) | DMA_TCD_ATTR_SMOD(12);
    dma.TCD->NBYTES_MLNO = 2;
    dma.TCD->SLAST = 0;   // SMOD wraps the source
    dma.TCD->DADDR = (void*)((uint32_t)&I2S0_TDR0 + 2);
    dma.TCD->DOFF = 0;
    dma.TCD->CITER_ELINKNO = kBlockBytes / 2;
    dma.TCD->DLASTSGA = 0;
    dma.TCD->BITER_ELINKNO = kBlockBytes / 2;
    dma.TCD->CSR = DMA_TCD_CSR_INTMAJOR;
}

inline uint32_t ringBlock(const DMAChannel& dma)
{
    return (((uint32_t)dma.TCD->SADDR - (uint32_t)i2s_ring) / kBlockBytes) % kRingBlocks;
}
}

void AudioOutputI2S::begin(void (*updateBuffersFunc)(void* left, void* right))
{
    this->updateBuffersFunc = updateBuffersFunc;
    dma.begin(true); // Allocate the DMA channel first

    // TODO: should we set & clear the I2S_TCSR_SR bit here?
    config_i2s();
    // CORE_PIN22_CONFIG = PORT_PCR_MUX(6); // pin 22, PTC1, I2S0_TXD0
    PORTE_PCR10 = PORT_PCR_MUX(4);

#if defined(KINETISK)
    setupTcd(dma);
#endif
    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_I2S0_TX);
    dma.enable();

    I2S0_TCSR = I2S_TCSR_SR;
    I2S0_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;
    dma.attachInterrupt(isr);
    dma.setInterruptPriority(kAudioOutputInterruptLevel);
}

void AudioOutputI2S::isr(void)
{
    const uint32_t c0 = *kCyc;
    dma.clearInterrupt();
    const uint32_t rb = ringBlock(dma);   // the block the DMA plays now
    if (!s_started)
    {   // the first block ended: the DMA plays block rb (zeros), writing starts after it
        s_started = true;
        s_rd = rb;
        s_wr = rb + 1;
    }
    else
    {
        const uint32_t gap = c0 - s_lastStart;
        if (gap > ff_audio_gap_max) ff_audio_gap_max = gap;
        // blocks the DMA went on since the last interrupt: the ring tells it modulo 8, the time a lower bound (the DWT runs at
        // half speed while the installer is out of HSRUN), enough for any masked window shorter than 8 blocks
        const uint32_t est = gap / kPeriodCyc;
        uint32_t adv = (rb - s_rd) % kRingBlocks;
        while (adv + 1 < est) adv += kRingBlocks;
        s_rd += adv;
    }
    s_lastStart = c0;
    int32_t ahead = int32_t(s_wr - s_rd) - 1;   // written blocks after the one playing
    if (ahead < 0)
    {   // the DMA reached blocks that were never written: it replayed old ones. Write on from the one after it
        ff_audio_late = ff_audio_late + uint32_t(-ahead);
        s_wr = s_rd + 1;
        ahead = 0;
    }
    uint32_t target = ff_audio_lead_target;
    if (target < 1) target = 1;
    if (target > kRingBlocks - 2) target = kRingBlocks - 2;
    // one block, or two while the lead is being raised (never more: at a high load two take most of a period), none while it
    // drains back after an erase
    for (int n = 0; n < 2 && uint32_t(ahead) < target; n++, ahead++)
    {
        if (updateBuffersFunc != nullptr)
        {
            // both halves of the old ping-pong buffer rendered through buffer_left + 128 (GCC folded the unset offset), kept
            (*updateBuffersFunc)(buffer_left + kAudioBlockSamples, buffer_right + kAudioBlockSamples);
        }
        memcpy_tointerleaveLR((int16_t*)&i2s_ring[(s_wr % kRingBlocks) * kAudioBlockSamples], buffer_left + kAudioBlockSamples,
                              buffer_right + kAudioBlockSamples);
        const uint32_t now = s_rd + (ringBlock(dma) - rb) % kRingBlocks;
        if (int32_t(now - s_wr) >= 0) ff_audio_late = ff_audio_late + 1;   // the DMA was in it before it was written
        s_wr++;
    }
    ff_audio_lead = uint32_t(ahead);
    const uint32_t c1 = *kCyc;
    ff_audio_isr_cyc = c1 - c0;
    ff_audio_end_cyc = c1;
    ff_audio_blocks = ff_audio_blocks + 1;
}

// MCLK needs to be 48e6 / 1088 * 256 = 11.29411765 MHz -> 44.117647 kHz sample rate
//
#if F_CPU == 96000000 || F_CPU == 48000000 || F_CPU == 24000000
// PLL is at 96 MHz in these modes
#define MCLK_MULT 2
#define MCLK_DIV 17
#elif F_CPU == 72000000
#define MCLK_MULT 8
#define MCLK_DIV 51
#elif F_CPU == 120000000
#define MCLK_MULT 8
#define MCLK_DIV 85
#elif F_CPU == 144000000
#define MCLK_MULT 4
#define MCLK_DIV 51
#elif F_CPU == 168000000
#define MCLK_MULT 8
#define MCLK_DIV 119
#elif F_CPU == 180000000
#define MCLK_MULT 16
#define MCLK_DIV 255
#define MCLK_SRC 0
#elif F_CPU == 192000000
#define MCLK_MULT 1
#define MCLK_DIV 17
#elif F_CPU == 216000000
#define MCLK_MULT 8
#define MCLK_DIV 153
#define MCLK_SRC 0
#elif F_CPU == 240000000
#define MCLK_MULT 4
#define MCLK_DIV 85
#elif F_CPU == 16000000
#define MCLK_MULT 12
#define MCLK_DIV 17
#else
#error "This CPU Clock Speed is not supported by the Audio library";
#endif

#ifndef MCLK_SRC
#if F_CPU >= 20000000
#define MCLK_SRC 3 // the PLL
#else
#define MCLK_SRC 0 // system clock
#endif
#endif

void AudioOutputI2S::config_i2s(void)
{
    SIM_SCGC6 |= SIM_SCGC6_I2S;
    SIM_SCGC7 |= SIM_SCGC7_DMA;
    SIM_SCGC6 |= SIM_SCGC6_DMAMUX;

    // if either transmitter or receiver is enabled, do nothing
    if (I2S0_TCSR & I2S_TCSR_TE)
        return;
    if (I2S0_RCSR & I2S_RCSR_RE)
        return;

    // enable MCLK output
    I2S0_MCR = I2S_MCR_MICS(MCLK_SRC) | I2S_MCR_MOE;
    while (I2S0_MCR & I2S_MCR_DUF)
        ;
    I2S0_MDR = I2S_MDR_FRACT((MCLK_MULT - 1)) | I2S_MDR_DIVIDE((MCLK_DIV - 1));

    // configure transmitter
    I2S0_TMR = 0;
    I2S0_TCR1 = I2S_TCR1_TFW(1); // watermark at half fifo size
    I2S0_TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP | I2S_TCR2_MSEL(1) | I2S_TCR2_BCD | I2S_TCR2_DIV(1);
    I2S0_TCR3 = I2S_TCR3_TCE;
    I2S0_TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(31) | I2S_TCR4_MF | I2S_TCR4_FSE | I2S_TCR4_FSP |
                I2S_TCR4_FSD;
    I2S0_TCR5 = I2S_TCR5_WNW(31) | I2S_TCR5_W0W(31) | I2S_TCR5_FBT(31);

    // configure receiver (sync'd to transmitter clocks)
    I2S0_RMR = 0;
    I2S0_RCR1 = I2S_RCR1_RFW(1);
    I2S0_RCR2 = I2S_RCR2_SYNC(1) | I2S_TCR2_BCP | I2S_RCR2_MSEL(1) | I2S_RCR2_BCD | I2S_RCR2_DIV(1);
    I2S0_RCR3 = I2S_RCR3_RCE;
    I2S0_RCR4 = I2S_RCR4_FRSZ(1) | I2S_RCR4_SYWD(31) | I2S_RCR4_MF | I2S_RCR4_FSE | I2S_RCR4_FSP |
                I2S_RCR4_FSD;
    I2S0_RCR5 = I2S_RCR5_WNW(31) | I2S_RCR5_W0W(31) | I2S_RCR5_FBT(31);

    // configure pin mux for 3 clock signals
    // CORE_PIN23_CONFIG = PORT_PCR_MUX(6); // pin 23, PTC2, I2S0_TX_FS (LRCLK)
    // CORE_PIN9_CONFIG  = PORT_PCR_MUX(6); // pin  9, PTC3, I2S0_TX_BCLK
    // CORE_PIN11_CONFIG = PORT_PCR_MUX(6); // pin 11, PTC6, I2S0_MCLK

    PORTE_PCR11 = PORT_PCR_MUX(4); // LRCLK
    PORTE_PCR12 = PORT_PCR_MUX(4); // BCLK
    PORTE_PCR6 = PORT_PCR_MUX(4);  // MCLK
}

/******************************************************************/

void AudioOutputI2Sslave::begin(void)
{
    dma.begin(true); // Allocate the DMA channel first

    // pinMode(2, OUTPUT);

    AudioOutputI2Sslave::config_i2s();
    CORE_PIN22_CONFIG = PORT_PCR_MUX(6); // pin 22, PTC1, I2S0_TXD0
#if defined(KINETISK)
    setupTcd(dma);
#endif
    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_I2S0_TX);
    dma.enable();

    I2S0_TCSR = I2S_TCSR_SR;
    I2S0_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;
    dma.attachInterrupt(isr);
}

void AudioOutputI2Sslave::config_i2s(void)
{
    SIM_SCGC6 |= SIM_SCGC6_I2S;
    SIM_SCGC7 |= SIM_SCGC7_DMA;
    SIM_SCGC6 |= SIM_SCGC6_DMAMUX;

    // if either transmitter or receiver is enabled, do nothing
    if (I2S0_TCSR & I2S_TCSR_TE)
        return;
    if (I2S0_RCSR & I2S_RCSR_RE)
        return;

    // Select input clock 0
    // Configure to input the bit-clock from pin, bypasses the MCLK divider
    I2S0_MCR = I2S_MCR_MICS(0);
    I2S0_MDR = 0;

    // configure transmitter
    I2S0_TMR = 0;
    I2S0_TCR1 = I2S_TCR1_TFW(1); // watermark at half fifo size
    I2S0_TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP;

    I2S0_TCR3 = I2S_TCR3_TCE;
    I2S0_TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(31) | I2S_TCR4_MF | I2S_TCR4_FSE | I2S_TCR4_FSP;

    I2S0_TCR5 = I2S_TCR5_WNW(31) | I2S_TCR5_W0W(31) | I2S_TCR5_FBT(31);

    // configure receiver (sync'd to transmitter clocks)
    I2S0_RMR = 0;
    I2S0_RCR1 = I2S_RCR1_RFW(1);
    I2S0_RCR2 = I2S_RCR2_SYNC(1) | I2S_TCR2_BCP;

    I2S0_RCR3 = I2S_RCR3_RCE;
    I2S0_RCR4 = I2S_RCR4_FRSZ(1) | I2S_RCR4_SYWD(31) | I2S_RCR4_MF | I2S_RCR4_FSE | I2S_RCR4_FSP |
                I2S_RCR4_FSD;

    I2S0_RCR5 = I2S_RCR5_WNW(31) | I2S_RCR5_W0W(31) | I2S_RCR5_FBT(31);

    // configure pin mux for 3 clock signals
    CORE_PIN23_CONFIG = PORT_PCR_MUX(6); // pin 23, PTC2, I2S0_TX_FS (LRCLK)
    CORE_PIN9_CONFIG = PORT_PCR_MUX(6);  // pin  9, PTC3, I2S0_TX_BCLK
    CORE_PIN11_CONFIG = PORT_PCR_MUX(6); // pin 11, PTC6, I2S0_MCLK
}
