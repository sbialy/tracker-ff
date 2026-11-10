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

int16_t AudioOutputI2S::buffer_left[kAudioBlockSamples * 2];
int16_t AudioOutputI2S::buffer_right[kAudioBlockSamples * 2];
void (*AudioOutputI2S::updateBuffersFunc)(void* left, void* right) = nullptr;

DMAMEM static uint32_t i2s_tx_buffer[kAudioBlockSamples * 2];

DMAChannel AudioOutputI2S::dma(false);

// Audio block timing for the FlexNVM installer (synth-open/plug_nvm.cpp, install-crackle): it starts each erase slice right after
// a block ends and sizes it by the last block's length. Also the measure of the install crackle: ff_audio_late counts blocks that
// came 1.75 periods or more after the previous one (the interrupt was held off, the DMA replayed an old half) or ended with the
// DMA already in the half they wrote. DWT cycles (180 MHz, 90 MHz while the installer is out of HSRUN).
extern "C"
{
volatile uint32_t ff_audio_blocks;
volatile uint32_t ff_audio_end_cyc;
volatile uint32_t ff_audio_isr_cyc;
volatile uint32_t ff_audio_gap_max;
volatile uint32_t ff_audio_late;
}
namespace
{
volatile uint32_t* const kCyc = reinterpret_cast<volatile uint32_t*>(0xE0001004);   // DWT_CYCCNT
// one block at 48 MHz / 1088 = 44,117.6 Hz (the MCLK below): 522,240 cycles at 180 MHz
constexpr uint32_t kPeriodCyc = uint32_t(uint64_t(F_CPU) * kAudioBlockSamples * 1088 / 48000000ull);
uint32_t s_lastStart;
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
    dma.TCD->SADDR = i2s_tx_buffer;
    dma.TCD->SOFF = 2;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
    dma.TCD->NBYTES_MLNO = 2;
    dma.TCD->SLAST = -sizeof(i2s_tx_buffer);
    dma.TCD->DADDR = (void*)((uint32_t)&I2S0_TDR0 + 2);
    dma.TCD->DOFF = 0;
    dma.TCD->CITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
    dma.TCD->DLASTSGA = 0;
    dma.TCD->BITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
    dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
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
    int16_t* dest;
    // offset was left unset on the second branch: GCC folded it to kAudioBlockSamples (the 1.2.0 image renders both halves
    // through buffer_left + 128), now explicit so the added code cannot change it
    uint32_t saddr, offset = kAudioBlockSamples;

    const uint32_t c0 = *kCyc;
    const uint32_t gap = c0 - s_lastStart;
    if (s_lastStart)
    {
        if (gap > ff_audio_gap_max) ff_audio_gap_max = gap;
        if (gap >= kPeriodCyc / 4 * 7) ff_audio_late = ff_audio_late + 1;
    }
    s_lastStart = c0 ? c0 : 1;

    saddr = (uint32_t)(dma.TCD->SADDR);
    dma.clearInterrupt();
    const bool dmaFirst = saddr < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2;
    if (dmaFirst)
    {
        // DMA is transmitting the first half of the buffer
        // so we must fill the second half
        dest = (int16_t*)&i2s_tx_buffer[kAudioBlockSamples];
        offset = kAudioBlockSamples;
    }
    else
    {
        // DMA is transmitting the second half of the buffer
        // so we must fill the first half
        dest = (int16_t*)i2s_tx_buffer;
    }

    if (updateBuffersFunc != nullptr)
    {
        (*updateBuffersFunc)(buffer_left + offset, buffer_right + offset);
    }

    memcpy_tointerleaveLR(dest, buffer_left + offset, buffer_right + offset);

    const bool dmaFirstNow = (uint32_t)(dma.TCD->SADDR) < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2;
    if (dmaFirstNow != dmaFirst) ff_audio_late = ff_audio_late + 1;   // the DMA reached the half before it was written
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
    dma.TCD->SADDR = i2s_tx_buffer;
    dma.TCD->SOFF = 2;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
    dma.TCD->NBYTES_MLNO = 2;
    dma.TCD->SLAST = -sizeof(i2s_tx_buffer);
    dma.TCD->DADDR = (void*)((uint32_t)&I2S0_TDR0 + 2);
    dma.TCD->DOFF = 0;
    dma.TCD->CITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
    dma.TCD->DLASTSGA = 0;
    dma.TCD->BITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
    dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
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
