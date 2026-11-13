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

#include "input_i2s.h"
#include "output_i2s.h"

DMAMEM static uint32_t i2s_rx_buffer[2 * kAudioBlockSamples];
void (*AudioInputI2S::updateBuffersFunc)(void* data) = nullptr;
DMAChannel AudioInputI2S::dma(false);

void AudioInputI2S::begin(void (*updateBuffersFunc)(void* data))
{
    AudioInputI2S::updateBuffersFunc = updateBuffersFunc;

    dma.begin(true); // Allocate the DMA channel first

    // block_left_1st = NULL;
    // block_right_1st = NULL;

    // TODO: should we set & clear the I2S_RCSR_SR bit here?
    AudioOutputI2S::config_i2s();

    // CORE_PIN13_CONFIG = PORT_PCR_MUX(4); // pin 13, PTC5, I2S0_RXD0
    PORTE_PCR7 = PORT_PCR_MUX(4); // RX
#if defined(KINETISK)
    dma.TCD->SADDR = (void*)((uint32_t)&I2S0_RDR0 + 2);
    dma.TCD->SOFF = 0;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
    dma.TCD->NBYTES_MLNO = 2;
    dma.TCD->SLAST = 0;
    dma.TCD->DADDR = i2s_rx_buffer;
    dma.TCD->DOFF = 2;
    dma.TCD->CITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
    dma.TCD->DLASTSGA = -sizeof(i2s_rx_buffer);
    dma.TCD->BITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
    dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
#endif
    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_I2S0_RX);
    dma.enable();

    I2S0_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
    I2S0_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE; // TX clock enable, because sync'd to TX
    dma.attachInterrupt(isr);
    dma.setInterruptPriority(kAudioInputInterruptLevel);
}

// Power off (hardware.cpp disableHardware): the recorder and the USB capture behind the interrupt write the SDRAM, which is
// unpowered right after this. Request and interrupt off, a start brings both back.
void AudioInputI2S::stop(void)
{
    if (dma.channel >= DMA_NUM_CHANNELS) return;   // never begun
    dma.disable();
    NVIC_DISABLE_IRQ(IRQ_DMA_CH0 + (dma.channel & 15));
    NVIC_CLEAR_PENDING(IRQ_DMA_CH0 + (dma.channel & 15));
}

void AudioInputI2S::isr(void)
{
    uint32_t daddr, offset;

    daddr = (uint32_t)(dma.TCD->DADDR);
    dma.clearInterrupt();

    if (daddr < (uint32_t)i2s_rx_buffer + sizeof(i2s_rx_buffer) / 2)
    {
        // DMA is receiving to the first half of the buffer
        // need to remove data from the second half
        offset = kAudioBlockSamples;
    }
    else
    {
        // DMA is receiving to the second half of the buffer
        // need to remove data from the first half
        offset = 0;
    }

    if (updateBuffersFunc != nullptr)
    {
        updateBuffersFunc(&i2s_rx_buffer[offset]);
    }
}

/******************************************************************/

void AudioInputI2Sslave::begin(void)
{
    dma.begin(true); // Allocate the DMA channel first

    // block_left_1st = NULL;
    // block_right_1st = NULL;

    AudioOutputI2Sslave::config_i2s();

    CORE_PIN13_CONFIG = PORT_PCR_MUX(4); // pin 13, PTC5, I2S0_RXD0
#if defined(KINETISK)
    dma.TCD->SADDR = (void*)((uint32_t)&I2S0_RDR0 + 2);
    dma.TCD->SOFF = 0;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
    dma.TCD->NBYTES_MLNO = 2;
    dma.TCD->SLAST = 0;
    dma.TCD->DADDR = i2s_rx_buffer;
    dma.TCD->DOFF = 2;
    dma.TCD->CITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
    dma.TCD->DLASTSGA = -sizeof(i2s_rx_buffer);
    dma.TCD->BITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
    dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
#endif
    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_I2S0_RX);
    dma.enable();

    I2S0_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
    I2S0_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE; // TX clock enable, because sync'd to TX
    dma.attachInterrupt(isr);
}
