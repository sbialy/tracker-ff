#include "SPI.h"

void SPI_init(SPI_Type* base)
{
    if (base == SPI0)
    {
        SIM->SCGC6 |= SIM_SCGC6_SPI0_MASK;
    }
    else if (base == SPI1)
    {
        SIM->SCGC6 |= SIM_SCGC6_SPI1_MASK;
    }
    else if (base == SPI2)
    {
        SIM->SCGC3 |= SIM_SCGC3_SPI2_MASK;
    }

    base->MCR = SPI_MCR_MDIS_MASK | SPI_MCR_HALT_MASK | SPI_MCR_PCSIS(0x1F);
    base->CTAR[0] = SPI_CTAR_FMSZ(7) | // Frame Size
                    SPI_CTAR_PBR(2) |  // Prescaler
                    SPI_CTAR_BR(1) |   // Baud Rate Scaler
                    SPI_CTAR_CSSCK(1); // PCS to SCK Delay Scaler

    // SCK baud rate = (fP /PBR) x [(1+DBR)/BR]

    base->MCR = SPI_MCR_MSTR_MASK | SPI_MCR_PCSIS(0x1F);
}

void SPI_start(SPI_Type* base, uint32_t /*speed*/)
{
    base->MCR = SPI_MCR_MDIS_MASK | SPI_MCR_HALT_MASK | SPI_MCR_PCSIS(0x1F);
    base->CTAR[0] = SPI_CTAR_FMSZ(7) | // Frame Size
                    SPI_CTAR_DBR(1) |  // Double Baud Rate
                    SPI_CTAR_PBR(1) |  // Prescaler
                    SPI_CTAR_BR(0) |   // Baud Rate Scaler
                    SPI_CTAR_CSSCK(0); // PCS to SCK Delay Scaler

    base->MCR = SPI_MCR_MSTR_MASK | SPI_MCR_PCSIS(0x1F);
}

void SPI_end(SPI_Type* base)
{
    base->MCR = SPI_MCR_MDIS_MASK | SPI_MCR_HALT_MASK | SPI_MCR_PCSIS(0x1F);
}
