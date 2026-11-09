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
#include <stdint.h> // for Uint8/16/32 and Int8/16/32 data types
#include <string.h>

void MCU_Delay_3us(void);

uint16_t* cmdOffset;

void FT812_setCMDOffsetPtr(uint16_t* cmdOffset)
{
    ::cmdOffset = cmdOffset;
}

//##############################################################################
// Library functions
//##############################################################################
// uint16_t swizelee = 0;
// Begins co-pro list for display creation
void API_LIB_BeginCoProList()
{
    API_LIB_AwaitCoProEmpty(); // Wait for command FIFO to be empty and record current position in
                               // FIFO

    // if(swizelee > 0 ) swizelee = 0;
    // else swizelee =  0x1B6;
    // EVE_MemWrite16(REG_OUTBITS,  swizelee);

    MCU_CSlow();
    // CS low begins SPI transaction

    EVE_AddrForWr(
      RAM_CMD +
      *cmdOffset); // Send address for writing as the next free location in the co-pro buffer
}

void API_LIB_BeginCoProListNoCheck()
{
    MCU_CSlow(); // CS low begins SPI transaction

    // cmdOffset = 0;

    EVE_AddrForWr(
      RAM_CMD +
      *cmdOffset); // Send address for writing as the next free location in the co-pro buffer

    // EVE_AddrForWr(RAM_CMD);
}

// Ends co-pro list for display creation
void API_LIB_EndCoProList()
{
    MCU_CShigh(); // Chip Select high concludes burst

    EVE_MemWrite32(REG_CMD_WRITE, (*cmdOffset)); // Update the ring buffer pointer
}

// Waits for the read and write pointers to become equal
void API_LIB_AwaitCoProEmpty()
{
    uint8_t success = 0;
    success = EVE_WaitCmdFifoEmpty();          // Await completion of processing
    *cmdOffset = EVE_GetCurrentWritePointer(); // and record starting address for next screen update
    (void)success;
}

uint8_t API_LIB_IsCoProEmpty()
{
    uint8_t success = 0;
    success = EVE_IsCmdFifoEmpty();
    if (success)
    {
        *cmdOffset = EVE_GetCurrentWritePointer();
    }
    /*	{
            cmdOffset = 0;
            EVE_MemWrite32(REG_CMD_READ, cmdOffset);
            EVE_MemWrite32(REG_CMD_WRITE, cmdOffset);
        }
    */

    return success;
}

// Writes a block of data to the RAM_G
void API_LIB_WriteDataRAMG(const uint8_t* ImgData, uint32_t DataSize, uint32_t DestAddress)
{
    uint16_t DataPointer = 0;
    uint16_t BitmapDataSize = 0;

    DataPointer = 0;

    MCU_CSlow();                // CS low begins SPI transaction
    EVE_AddrForWr(DestAddress); // Send address to which first value will be written

    while (DataPointer < DataSize)
    {
        EVE_Write8(ImgData[DataPointer]); // Send data byte-by-byte from array
        DataPointer++;
    }

    BitmapDataSize =
      DataSize - DataPointer; // Add 3, 2 or 1 bytes padding to make it  a multiple of 4 bytes
    BitmapDataSize = BitmapDataSize & 0x03; // Mask off the bottom 2 bits

    if (BitmapDataSize == 0x03)
    {
        EVE_Write8(0x00);
    }
    else if (BitmapDataSize == 0x02)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
    }
    else if (BitmapDataSize == 0x01)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
        EVE_Write8(0x00);
    }

    MCU_CShigh(); // CS high after burst write of image data
}

void API_LIB_WriteFontRAMG(const uint8_t* ImgData, uint32_t DataSize, uint32_t DestAddress)
{
    uint16_t DataPointer = 0;
    uint16_t BitmapDataSize = 0;

    DataPointer = 0;

    MCU_CSlow();                // CS low begins SPI transaction
    EVE_AddrForWr(DestAddress); // Send address to which first value will be written

    while (DataPointer < 144)
    {
        EVE_Write8(ImgData[DataPointer]); // Send data byte-by-byte from array
        DataPointer++;
    }

    uint32_t address = DestAddress + 148;

    EVE_Write8((uint8_t)(address));
    EVE_Write8((uint8_t)(address >> 8));
    EVE_Write8((uint8_t)(address >> 16));
    EVE_Write8((uint8_t)(address >> 24));

    DataPointer += 4;

    while (DataPointer < DataSize)
    {
        EVE_Write8(ImgData[DataPointer]); // Send data byte-by-byte from array
        DataPointer++;
    }

    BitmapDataSize =
      DataSize - DataPointer; // Add 3, 2 or 1 bytes padding to make it  a multiple of 4 bytes
    BitmapDataSize = BitmapDataSize & 0x03; // Mask off the bottom 2 bits

    if (BitmapDataSize == 0x03)
    {
        EVE_Write8(0x00);
    }
    else if (BitmapDataSize == 0x02)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
    }
    else if (BitmapDataSize == 0x01)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
        EVE_Write8(0x00);
    }

    MCU_CShigh(); // CS high after burst write of image data
}

// Read a block of data from the RAM_G
void API_LIB_ReadDataRAMG(uint8_t* ImgData, uint32_t DataSize, uint32_t SourceAddress)
{
    uint16_t DataPointer = 0;
    uint16_t BitmapDataSize = 0;

    DataPointer = 0;

    MCU_CSlow();                  // CS low begins SPI transaction
    EVE_AddrForRd(SourceAddress); // Send address to which first value will be written

    while (DataPointer < DataSize)
    {
        ImgData[DataPointer] = EVE_Read8(); // Send data byte-by-byte from array
        DataPointer++;
    }

    MCU_CShigh(); // CS high after burst write of image data
}

void API_LIB_ReadBackDataRAMG(uint8_t* ImgData, uint32_t DataSize, uint32_t SourceAddress)
{
    int32_t DataPointer = DataSize - 1;

    MCU_CSlow();                      // CS low begins SPI transaction
    EVE_AddrForRd(SourceAddress - 1); // Send address to which first value will be written

    while (DataPointer >= 0)
    {
        ImgData[DataPointer] = EVE_Read8(); // Send data byte-by-byte from array
        DataPointer--;
    }

    MCU_CShigh(); // CS high after burst write of image data
}

// Writes a string over SPI
uint8_t API_SendString(const char* string, uint32_t length)
{
    uint32_t StringLength = 0;
    uint32_t rl_length = strlen(string);
    if (length == 0)
        length = rl_length;
    else if (length > rl_length)
        length = rl_length;

    uint8_t command_size = 0;

    StringLength = (length + 1);
    while (length--) // Send string data
    {
        EVE_Write8(*string);
        string++;
    }
    EVE_Write8(0); // Append one null as ending flag
    command_size = (command_size + StringLength);

    // Pad to multiple of 4
    StringLength = StringLength & 0x03; // Mask off the bottom 2 bits and see if multiple of 4
    if (StringLength == 0x03)
    {
        EVE_Write8(0x00);
        command_size = (command_size + 1);
    }
    else if (StringLength == 0x02)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
        command_size = (command_size + 2);
    }
    else if (StringLength == 0x01)
    {
        EVE_Write8(0x00);
        EVE_Write8(0x00);
        EVE_Write8(0x00);
        command_size = (command_size + 3);
    }
    return command_size;
}

void API_LIB_WriteDataToCMD(const uint8_t* ImgData, uint32_t TotalDataSize)
{
    // This code works by sending the data in a series of one or more bursts.
    // If the data is more than 1000 bytes, it is sent as a series of one or more bursts and then
    // the remainder. It uses a target value which specifies the last index (of the source data) for
    // each chunk e.g. if sending 1500 bytes, we set the target to 999 first and send 1000 bytes
    // (image data index 0 to 999). Then we set the target to 1499 and send the remainder of the
    // data from image data index 1000 to 1499.

    uint16_t CurrentIndex = 0;
    uint16_t Target = 0;
    uint16_t MaxChunkSize = 1000;
    uint16_t CurrentChunk = 0;
    uint16_t EndIndex = 0;
    uint8_t IsLastChunk = 0;
    uint8_t PaddingCheck = 0;
    uint16_t Freespace = 0;

    EndIndex =
      TotalDataSize - 1; // Determine last index since array index is 0 to (TotalDataSize - 1)

    while (CurrentIndex < EndIndex) // While not all data is sent
    {
        if ((EndIndex - CurrentIndex) > MaxChunkSize) // If more than ChunkSize bytes to send
        {
            Target =
              CurrentIndex +
              MaxChunkSize; // ... then add ChunkSize to the current target index to make new target
            IsLastChunk = 0; // ... and this is not the last chunk
        }
        else // or if all remaining bytes can fit in one chunk
        {
            Target = EndIndex;  // ... then add the amount of data to the current target
            IsLastChunk = 0xFF; // .. and this is the last chunk
        }

        Freespace = 0; // Wait until there is space
        while (Freespace < MaxChunkSize)
        {
            Freespace = EVE_CheckFreeSpace(*cmdOffset);
        }

        CurrentChunk = 0;
        MCU_CSlow();                         // Begin an SPI burst write
        EVE_AddrForWr(RAM_CMD + *cmdOffset); // to the next location in the FIFO

        while (CurrentIndex < Target) // From current index value, keep sending and incrementing
                                      // until we hit the new target
        {
            EVE_Write8(ImgData[CurrentIndex]);
            CurrentIndex++;
            CurrentChunk++;
        }

        if (IsLastChunk != 0) // If this is the last chunk of the data,
        {
            EVE_Write8(ImgData[CurrentIndex]); // ... send last data value ...
            CurrentIndex++;
            CurrentChunk++;
            // ... and then pad with 00s if necessary to make overall data size a multiple of 4
            // bytes

            PaddingCheck = (CurrentChunk)&0x03; // Mask off the bottom 2 bits to see if non-zero
                                                // (indicating not multiple 4))
            if (PaddingCheck == 0x03)
            {
                EVE_Write8(0x00); // Send 1 dummy byte to make up to multiple of 4
                CurrentChunk++;
            }
            else if (PaddingCheck == 0x02)
            {
                EVE_Write8(0x00); // Send 2 dummy byte to make up to multiple of 4
                EVE_Write8(0x00);
                CurrentChunk = (CurrentChunk + 2);
            }
            else if (PaddingCheck == 0x01)
            {
                EVE_Write8(0x00); // Send 3 dummy byte to make up to multiple of 4
                EVE_Write8(0x00);
                EVE_Write8(0x00);
                CurrentChunk = (CurrentChunk + 3);
            }
        }

        MCU_CShigh(); // End the SPI burst

        *cmdOffset =
          EVE_IncCMDOffset(*cmdOffset, (CurrentChunk)); // Calculate where end of data lies
        EVE_MemWrite32(REG_CMD_WRITE, *cmdOffset);      // and move write pointer to here
    }
}
