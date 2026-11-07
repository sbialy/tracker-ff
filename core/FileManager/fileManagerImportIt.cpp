#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include "log.h"

#include <cstring>
#include <math.h>

#define DEBUGMOD 0

extern Sequencer::strPattern fileManagerPatternBuffer;

int16_t* itFile_sampleDest_ptr;

// elementy pliku IT
uint16_t OrdNum;

uint16_t InsNum = 100;
uint16_t SmpNum = 100;
uint16_t PatNum = 100;

uint16_t Cwt;
uint16_t Cmwt;
uint16_t Flags;
uint16_t Special;
uint16_t GV;
uint16_t MV;
uint16_t IS;
uint16_t IT;

uint16_t Sep;
uint16_t PWD;
uint16_t MsgLgth;

uint32_t MessageOffset;
uint8_t oldImpulseInstrumentFormat;

uint8_t sampleNumber = 0;

const uint8_t FLAGS__STEREO = 0b00000001;
const uint8_t FLAGS__USE_INSTRUMENTS = 0b00000100;
const uint8_t SPECIAL__SONG_MESSAGE = 0b00000001;

// zmienne wewnetrzne

uint8_t processedPattern = 0;
uint8_t processedInstrument = 0;
uint8_t processedSample = 0;

int8_t instrumentTuneCorrection[kSampleInstrumentsCount]{ 0 };

uint8_t waveWriteFlag = 0;
uint32_t saveLength = 0;
int16_t* waveSrcPtr;
int16_t* sample_ptr;
uint32_t bSampleOffset = 0;

extern Sequencer::strPattern fileManagerPatternBuffer;

void cFileManager::importItFile_Init()
{
    importModFileType = checkImportFileType();
    if (importModFileType != importModFiletype_it)
        return;

    stopAudioEngine();

    // init wartosci
    processedPattern = 0;
    processedInstrument = 0;
    processedSample = 0;

    InsNum = 100;
    SmpNum = 100;
    PatNum = 100;

    memset(instrumentTuneCorrection, 0, sizeof(instrumentTuneCorrection));

    sampleNumber = 0;

    waveWriteFlag = 0;
    saveLength = 0;
    sample_ptr = externalRamSampleMemory;
    bSampleOffset = 0;

    itFile_sampleDest_ptr = externalRamSampleMemory;

    moveToNextOperationStep();
}
uint32_t readUint(uint8_t* buffStart, uint8_t size)
{
    if (size > 4)
        return 0;

    uint32_t retVal = 0;
    for (uint8_t a = 0; a < size; a++)
    {
        retVal |= *(buffStart + a) << (a * 8);
    }

    return retVal;
}

void cFileManager::importItFile_ProcessHeader()
{
    // wew
    uint8_t byteBuffer[0x40];
    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)byteBuffer,
                                                       sizeof(byteBuffer),
                                                       0,
                                                       fileWholeOnce); // fileDivIntoParts
    if (loadStatus == fileTransferEnd)
    {

        [[maybe_unused]] uint16_t PHiligt = readUint(&byteBuffer[0x1e], 2);
        OrdNum = readUint(&byteBuffer[0x20], 2);

        InsNum = readUint(&byteBuffer[0x22], 2);
        SmpNum = readUint(&byteBuffer[0x24], 2);
        PatNum = readUint(&byteBuffer[0x26], 2);
        Cwt = readUint(&byteBuffer[0x28], 2);
        Cmwt = readUint(&byteBuffer[0x2A], 2);
        Flags = readUint(&byteBuffer[0x2C], 2);
        Special = readUint(&byteBuffer[0x2E], 2);
        GV = readUint(&byteBuffer[0x30], 1);
        MV = readUint(&byteBuffer[0x31], 1);
        IS = readUint(&byteBuffer[0x32], 1);
        IT = readUint(&byteBuffer[0x33], 1);
        Sep = readUint(&byteBuffer[0x34], 1);
        PWD = readUint(&byteBuffer[0x35], 1);
        MsgLgth = readUint(&byteBuffer[0x36], 1);
        MessageOffset = readUint(&byteBuffer[0x38], 4);
        oldImpulseInstrumentFormat = Cmwt < 0x200;

#if DEBUGMOD
        LOGF("%d\n", PHiligt);
        LOGF("OrdNum = %d\n", OrdNum);
        LOGF("%s", "Flags: ");
        LOGF("%x\n", Flags);
        LOGF("InsNum = %d\n", InsNum);
        LOGF("SmpNum = %d\n", SmpNum);
        LOGF("PatNum = %d\n", PatNum);
        LOGF("Cwt = %d\n", Cwt);
        LOGF("Cmwt = 0h%04X\n", Cwt);

        LOG(oldImpulseInstrumentFormat ? "old impulse format" : "new impulse format");
        LOG("");

        LOGF("%x\n", Special);
        LOGF("MsgLgth = %d\n", MsgLgth);
        LOGF("MessageOffset = %d\n", MessageOffset);
#endif

        if (oldImpulseInstrumentFormat)
        {
            stopOperationWithError(fmImportModError);
        }
        else
        {
            moveToNextOperationStep();
        }
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::importItFile_ProcessSong()
{

    mtProject.values.globalTempo = clamp<float>(IT, 10.0f, 400.0f);

    // OrdNum comes from the file header (0..65535), the playlist holds SONG_MAX entries.
    // Read at most SONG_MAX orders into a fixed buffer. OrdNum itself stays unclamped
    // because it is also the offset of the instrument, sample and pattern tables.
    uint8_t byteBuffer[SONG_MAX];
    const uint32_t ordersToRead = (OrdNum < SONG_MAX) ? OrdNum : SONG_MAX;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)byteBuffer,
                                                       ordersToRead,
                                                       0x00C0,         // offset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        //		Serial.println("song:");
        //		for (uint8_t &a : byteBuffer)
        //		{
        //			Serial.println(a);
        //		}

        for (uint8_t a = 0; (a < SONG_MAX) && a < OrdNum; a++)
        {
            mtProject.song.playlist[a] = byteBuffer[a];

            if (mtProject.song.playlist[a] == 255)
                mtProject.song.playlist[a] = 0;
            else
            {
                mtProject.song.playlist[a]++;
            }
        }

        moveToNextOperationStep();
    }

    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}
uint32_t cFileManager::getInstrumentOffset(uint8_t index)
{
    if (index >= InsNum)
        return 0;

    uint8_t byteBuffer[4];

    uint32_t fileOffset = 0x00C0 + OrdNum + index * sizeof(uint32_t);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       byteBuffer,
                                                       sizeof(uint32_t),
                                                       fileOffset,     // offset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        return readUint(byteBuffer, 4);
    }

    return 0;
}

uint32_t cFileManager::getSampleOffset(uint8_t index)
{
    if (index >= SmpNum)
        return 0;

    uint8_t byteBuffer[4];

    uint32_t fileOffset = 0x00C0 + OrdNum + InsNum * 4 + index * sizeof(uint32_t);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       byteBuffer,
                                                       sizeof(uint32_t),
                                                       fileOffset,     // fileOffset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        return readUint(byteBuffer, 4);
    }

    return 0;
}

uint32_t cFileManager::getPatternOffset(uint8_t index)
{
    if (index >= PatNum)
        return 0;

    uint8_t byteBuffer[4];

    uint32_t fileOffset = 0x00C0 + OrdNum + InsNum * 4 + SmpNum * 4 + index * sizeof(uint32_t);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       byteBuffer,
                                                       sizeof(uint32_t),
                                                       fileOffset,     // fileOffset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        return readUint(byteBuffer, 4);
    }

    return 0;
}
uint32_t cFileManager::getFileVariable(uint32_t subFileOffset, uint32_t offset, uint8_t varSize)
{

    uint8_t byteBuffer[varSize];

    uint32_t fileOffset = subFileOffset + offset;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       byteBuffer,
                                                       sizeof(varSize),
                                                       fileOffset,     // fileOffset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {
        return readUint(byteBuffer, varSize);
    }

    return 0;
}
void cFileManager::importItFile_ProcessOffsets()
{

#if DEBUGMOD
    for (uint8_t a = 0; a < InsNum; a++)
    {
        LOGF("offset instr %d: %08X\n", a, getInstrumentOffset(a));
    }

    for (uint8_t a = 0; a < SmpNum; a++)
    {

        LOGF("offset sample %d: %08X\n", a, getSampleOffset(a));
    }
    for (uint8_t a = 0; a < PatNum; a++)
    {

        LOGF("offset pattern %d: %08X\n", a, getPatternOffset(a));
    }
#endif

    moveToNextOperationStep();
}

void cFileManager::importItFile_ProcessInstruments()
{
    strInstrument* instr = &mtProject.instrument[processedInstrument];
    setDefaultActiveInstrument(instr);

    uint32_t fileOffset = getInstrumentOffset(processedInstrument);

    uint8_t tempSampleNumber = 0;
    bool found = 0;
    for (uint8_t a = 0; a < 120; a++)
    {
        tempSampleNumber = getFileVariable(fileOffset, 0x41 + a * 2, 1);
        //		Serial.printf("tempSampleNumber: %d\n", tempSampleNumber);

        if (tempSampleNumber > 0 && !found)
        {
            sampleNumber = tempSampleNumber;
            found = 1;
            break; // powyżej na potrzeby testow
        }
    }

    uint8_t GbV = getFileVariable(fileOffset, 0x18, 1); // Global Volume, 0->128

    instr->volume = mapClamped<int>(GbV, 0, 128, 0, DEFAULT_INSTRUMENT_VOLUME);

#if DEBUGMOD
    LOGF("Instrument: %d,\nNoS: %d, smp no: %d, GbV %d\nNNA: %d, DCT: %d, DCA: %d, "
         "Fade: %d, PPS: %d, PPC: %d, DfP: %d, RV: %d, RP: %d, ver: %d, \n",
         processedInstrument,
         getFileVariable(fileOffset, 0x1e, 1),
         sampleNumber,
         GbV,
         getFileVariable(fileOffset, 0x11, 1), // nna
         getFileVariable(fileOffset, 0x12, 1), // dct
         getFileVariable(fileOffset, 0x13, 1), // dca
         getFileVariable(fileOffset, 0x14, 2), // fade
         getFileVariable(fileOffset, 0x16, 1), // pps
         getFileVariable(fileOffset, 0x17, 1), // ppc
         getFileVariable(fileOffset, 0x19, 1), // dfp
         getFileVariable(fileOffset, 0x1a, 1), // rv
         getFileVariable(fileOffset, 0x1b, 1), // rp
         getFileVariable(fileOffset, 0x1c, 2)  // ver
    );
#endif

    // pobieranie nazwy
    char nameBuffer[26];
    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)nameBuffer,
                                                       sizeof(nameBuffer),
                                                       fileOffset + 0x20, // fileOffset
                                                       fileWholeOnce);    // fileDivIntoParts
    if (loadStatus == fileTransferEnd)
    {
#if DEBUGMOD
        LOG(instr->sample.file_name);
#endif
        if (strlen(nameBuffer) > 0)
        {
            strcpy(instr->sample.file_name, nameBuffer);
            instr->sample.file_name[31] = 0;
        }
        else
        {
            strcpy(instr->sample.file_name, "-");
            instr->sample.file_name[1] = 0;
        }
    }

    if (sampleNumber > 0)
    {
        moveToNextOperationStep();
    }
    else if (sampleNumber == 0 && (getSampleOffset(processedInstrument) > 0))
    {
        sampleNumber = processedInstrument + 1;
        moveToNextOperationStep();
    }
    else // jesli nie ma sampla to powtarzamy krok
    {
        processedInstrument++;
        // jesli skonczyly sie instrumenty to 2 kroki dalej
        if (processedInstrument >= InsNum || processedInstrument > kSampleInstrumentsCount - 1)
        {
            moveToNextOperationStep();
            moveToNextOperationStep();
        }
    }
}

void cFileManager::importItFile_LoadSamples()
{
    strInstrument* instr = &mtProject.instrument[processedInstrument];

    uint32_t fileOffset = getSampleOffset(sampleNumber - 1);
    if (fileOffset <= 0)
    {
        processedInstrument++;
        // jesli skonczyly sie instrumenty to 2 kroki dalej
        if (processedInstrument >= InsNum || processedInstrument > kSampleInstrumentsCount - 1)
        {
            moveToNextOperationStep();
        }
        else
        {
            moveToPrevOperationStep();
        }
        return;
    }

    uint8_t sampleHeader[0x50];

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)sampleHeader,
                                                       sizeof(sampleHeader),
                                                       fileOffset,     // fileOffset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {

        uint32_t Flg = readUint(&sampleHeader[0x12], 4);
        uint32_t length = readUint(&sampleHeader[0x30], 4);
        uint32_t loopBegin = readUint(&sampleHeader[0x34], 4);
        uint32_t loopEnd = readUint(&sampleHeader[0x38], 4);
        uint32_t susLoopBegin = readUint(&sampleHeader[0x40], 4);
        uint32_t susLoopEnd = readUint(&sampleHeader[0x44], 4);
        uint32_t C5Speed = readUint(&sampleHeader[0x3c], 4);
        uint32_t SmpPoint = readUint(&sampleHeader[0x48], 4); //'Long' Offset of sample in file.
        uint32_t Cvt = readUint(&sampleHeader[0x2e], 1);

        uint32_t GvL = readUint(&sampleHeader[0x11], 1);
        uint32_t Vol = readUint(&sampleHeader[0x13], 1);

        instr->volume = mapClamped<int>(Vol, 0, 64, 0, instr->volume);

        bool isHeader = (Flg & 0b00000001);
        bool is16or8bit = (Flg & 0b00000010);
        bool isStereo = (Flg & 0b00000100);
        bool isCompression = (Flg & 0b00001000);
        bool isLoop = (Flg & 0b00010000);
        bool isPingPong = (Flg & 0b01000000);
        bool isPingPongSustain = (Flg & 0b10000000);
        bool isSigned = Cvt & 0b00000001;
        bool isPCM = Cvt & 0b00000100;
        bool isHiLo = Cvt & 0b00000010;
        bool isPTMLoad = Cvt & 0b00001000;
        bool isTX12Bit = Cvt & 0b00010000;

        char sampleName[30]{ 0 };

#if DEBUGMOD

        strcpy(sampleName, (char*)&sampleHeader[0x14]);

        LOGF("##sampleName: %.26s\n", sampleName);
        LOGF("###SAMPLE/nflags: %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s \n",
             isHeader ? "header" : "no header",
             is16or8bit ? "16bit" : "8bit",
             isSigned ? "signed" : "unsign",
             isPCM ? "delta" : "PCM",
             isHiLo ? "HiLo" : "LoHi",
             isPTMLoad ? "PTM" : "no PTM",
             isTX12Bit ? "12bittxwave" : "no 12bit",
             isStereo ? "stereo" : "mono",
             isCompression ? "compressed" : "no compress",
             isLoop ? "ude loop" : "no loop",
             isPingPong ? "ping pong" : "forward",
             isPingPongSustain ? "ping pong sutain" : "forward sustain");

        LOGF("length: %d\nloopBegin: %d\tloopEnd %d\tC5Speed: %d\tSmpPoint "
             "%d\nsusLoopBegin: %d\tsusLoopEnd: %d\t GvL: %d\tVol: %d\n",
             length,
             loopBegin,
             loopEnd,
             C5Speed,
             SmpPoint,
             susLoopBegin,
             susLoopEnd,
             GvL,
             Vol);
#endif
        if (sampleNumber > 0 && length > 0) // 0 == no sample
        {
            instr->isActive = 1;
            instr->sample.type = mtSampleTypeWaveFile;

            instr->sample.length = length;
            instr->startPoint = 0;

            instr->loopPoint1 =
              clamp<int>(mapClamped<int>(loopBegin, 0, length, 0, MAX_16BIT), 1, MAX_16BIT);
            instr->loopPoint2 =
              clamp<int>(mapClamped<int>(loopEnd, 0, length, 0, MAX_16BIT), 1, MAX_16BIT);

            instr->endPoint = MAX_16BIT;

            // todo save instrument

            if (isLoop || isPingPong)
            {
                if (isPingPong)
                {
                    instr->playMode = playModePingpong;
                }
                else
                {
                    instr->playMode = playModeLoopForward;
                }
            }
            else
            {
                instr->playMode = playModeSingleShot;
                instr->loopPoint1 = 1;
                instr->loopPoint2 = MAX_16BIT - 1;
            }

            checkLoopParameters(instr);

#if DEBUGMOD
            LOGF("loopPoint1: %d, loopPoint2 %d\n", instr->loopPoint1, instr->loopPoint2);
#endif
            // kopiujemy dane sampli:
            if (is16or8bit)
            {
                uint32_t totalToRead = instr->sample.length * 2;
                if ((mtProject.used_memory + instr->sample.length * 2) > SAMPLE_MEMORY_SIZE)
                {
                    moveToNextOperationStep();
                    return;
                }
                //				uint8_t buff[512] { 0 };

                uint8_t* tempPtr = (uint8_t*)itFile_sampleDest_ptr;
                while (totalToRead)
                {
                    uint32_t bytesToRead = totalToRead >= 2048 ? 2048 : totalToRead;

                    loadStatus = fileTransfer.loadFileToMemory(
                      impFileData.path,
                      (uint8_t*)tempPtr,
                      bytesToRead,                                         // memo
                      SmpPoint + (instr->sample.length * 2 - totalToRead), // offset
                      fileWholeOnce);

                    totalToRead -= bytesToRead;
                    tempPtr += bytesToRead;
                }

                if (C5Speed == 44100)
                {
                }
                else
                {
                    float noteTune = 12 * log2((float)C5Speed / (float)44100);

                    if (abs(noteTune) >= 25)
                    {
                        instrumentTuneCorrection[processedInstrument] = noteTune;
                    }
                    else
                    {
                        instr->tune = clamp<int>(noteTune, -24, 24);
                    }

                    instr->fineTune = clamp<int>((noteTune - ((int8_t)noteTune)) * 100, -100, 100);
#if DEBUGMOD
                    LOGF("tune= %d, fine= %d, instr corr=%d\n",
                         instr->tune,
                         instr->fineTune,
                         instrumentTuneCorrection[processedInstrument]);
#endif
                }

                instr->sample.address = itFile_sampleDest_ptr;

                itFile_sampleDest_ptr += instr->sample.length;

                mtProject.used_memory += instr->sample.length * 2;
            }
            else // 8bit
            {
                uint32_t totalToRead = instr->sample.length;
                if ((mtProject.used_memory + instr->sample.length * 2) > SAMPLE_MEMORY_SIZE)
                {
                    moveToNextOperationStep();
                    return;
                }
                uint8_t buff[512]{ 0 };

                uint16_t* tempPtr = (uint16_t*)itFile_sampleDest_ptr;
                while (totalToRead)
                {
                    uint32_t bytesToRead = totalToRead >= 512 ? 512 : totalToRead;

                    loadStatus = fileTransfer.loadFileToMemory(
                      impFileData.path,
                      (uint8_t*)buff,
                      bytesToRead,                                     // memo
                      SmpPoint + (instr->sample.length - totalToRead), // offset
                      fileWholeOnce);

                    for (uint16_t s = 0; s < bytesToRead; s++)
                    {
                        *tempPtr = buff[s] << 8;

                        tempPtr++;
                    }

                    totalToRead -= bytesToRead;
                }

                if (C5Speed == 22050)
                {
                }
                else
                {
                    float noteTune = 12 * log2((float)C5Speed / (float)22050);

                    instr->tune = clamp<int>(noteTune, -24, 24);
                    instr->fineTune = clamp<int>((noteTune - ((int8_t)noteTune)) * 100, -100, 100);
#if DEBUGMOD
                    LOGF("tune= %d, fine= %d\n", instr->tune, instr->fineTune);
#endif
                }

                instr->sample.address = itFile_sampleDest_ptr;

                itFile_sampleDest_ptr += instr->sample.length;

                mtProject.used_memory += instr->sample.length * 2;
            }

            mtProject.sampleMemory[processedInstrument].address[strSampleMemory::Channel::kLeft] =
              instr->sample.address;
            mtProject.sampleMemory[processedInstrument].address[strSampleMemory::Channel::kRight] =
              mtProject.sampleMemory[processedInstrument].address[strSampleMemory::Channel::kLeft];

            saveInstrument(processedInstrument);
        }
        else
        {
            instr->isActive = 0;
            instr->sample.length = 0;
        }

#if DEBUGMOD
        LOG("");
#endif
        processedInstrument++;
        // jesli skonczyly sie instrumenty to 2 kroki dalej
        if (processedInstrument >= InsNum || processedInstrument > kSampleInstrumentsCount - 1)
        {
            moveToNextOperationStep();
        }
        else
        {
            moveToPrevOperationStep();
        }
    }

    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::importItFile_ProcessPatterns()
{

    uint32_t patternOffset = getPatternOffset(processedPattern);

    if (patternOffset == 0)
    {
#if DEBUGMOD
        LOGF("patt %d, len: %d, rows: %d\n", processedPattern, 0, 0);
#endif
        /*
         Note that if the (long) offset to a pattern = 0, then the
         pattern is assumed to be a 64 row empty pattern.
         */
        processedPattern++;
        if (processedPattern >= PatNum)
        {
            moveToNextOperationStep();
            moveToNextOperationStep();
            processedInstrument = 0;
        }
        return;
    }

    uint8_t byteBuffer[4];
    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       byteBuffer,
                                                       sizeof(byteBuffer),
                                                       patternOffset,  // fileOffset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {

        uint16_t length = readUint(&byteBuffer[0], 2);
        uint16_t rows = readUint(&byteBuffer[2], 2);

#if DEBUGMOD
        LOGF("patt %d, len: %d, rows: %d\n", processedPattern, length, rows);
#endif
        importItFile_setPattern(processedPattern + 1, rows - 1);

        importItFile_ProcessPattern(patternOffset, length, rows);

        importItFile_savePattern();

        processedPattern++;
        if (processedPattern >= PatNum)
        {
            moveToNextOperationStep();
            processedInstrument = 0;
        }
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}

uint32_t bytesInPattern = 0;    // ile bajtow w calym patternie
int32_t bytesInPatternLeft = 0; // pozostalo do odczytu

__NOINIT(EXTERNAL_RAM)
uint8_t newBuffer[512];

uint16_t buffPos = 0;
uint32_t patternOffset = 0;
uint8_t buffersRead = 0; // ile buforów odczytane

uint8_t cFileManager::importItFile_getNextPatternByte()
{
    uint8_t byteToRet = 0;
    if (bytesInPatternLeft > 0)
    {

        if (buffPos >= 512)
        {
            buffersRead++;

            uint16_t bytesToLoad = bytesInPatternLeft;
            if (bytesToLoad > 512)
                bytesToLoad = 512;

            uint8_t loadStatus =
              fileTransfer.loadFileToMemory(impFileData.path,
                                            newBuffer,
                                            bytesToLoad,
                                            patternOffset + 8 + buffersRead * 512, // fileOffset
                                            fileWholeOnce);                        //
            if (loadStatus >= fileTransferError)
            {
                stopOperationWithError(fmImportModError);
                return 0;
            }

            buffPos = 0;
        }
        byteToRet = newBuffer[buffPos++];
        bytesInPatternLeft--;
    }
    else
    {
        byteToRet = 0;
    }

    //	Serial.printf("bleft: %d\n", bytesInPatternLeft);

    return byteToRet;
}

void cFileManager::importItFile_initPatternBuffer(uint32_t pattLength, uint32_t _patternOffset)
{
    buffPos = 0;
    buffersRead = 0;
    bytesInPattern = pattLength;
    bytesInPatternLeft = pattLength;
    patternOffset = _patternOffset;

    uint16_t bytesToLoad = pattLength;
    if (bytesToLoad > 512)
        bytesToLoad = 512;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       newBuffer,
                                                       bytesToLoad,
                                                       patternOffset + 8, // fileOffset
                                                       fileWholeOnce);    //
    if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }

    //	buffersRead++;

    //	bytesInPatternLeft -= bytesToLoad;
}
void cFileManager::importItFile_ProcessPattern(uint32_t patternOffset,
                                               uint16_t length,
                                               uint16_t rows)
{
    //	if (length > 512) return;

    //	uint8_t byteBuffer[length];
    //	buffPos = 0;

    importItFile_initPatternBuffer(length, patternOffset);

    uint8_t previousmaskvariable[64]{ 0 };
    uint8_t lastNote[64]{ 0 };
    uint8_t lastInstrument[64]{ 0 };
    uint8_t lastVolume[64]{ 0 };
    uint8_t lastCommand[64]{ 0 };
    uint8_t lastCommandValue[64]{ 0 };

    //	if (bytesInPatternLeft > 0)
    //	{
    //		uint16_t byteIndex = 0;

    uint8_t channel = 0;
    uint8_t maskvariable = 0;
    uint8_t note = 0;
    uint8_t instrument = 0;
    int16_t volume = -1;
    uint8_t command = 0;
    uint8_t commandValue = 0;
    uint8_t row = 0;

    do
    {

        uint8_t channelvariable = importItFile_getNextPatternByte();

        if (channelvariable == 0)
        {
            //			Serial.println("end of row");
            row++;
        }
        else
        {

            channel = (channelvariable - 1) & 63;
            if (channel >= 64)
            {
                stopOperationWithError(fmImportModError);
                return;
            }

            if (channelvariable & 128)
            {
                maskvariable = importItFile_getNextPatternByte();

                previousmaskvariable[channel] = maskvariable;
            }
            else
            {
                maskvariable = previousmaskvariable[channel];
            }

            if (maskvariable & 1)
            {
                note = importItFile_getNextPatternByte();
                lastNote[channel] = note;
            }
            if (maskvariable & 2)
            {
                instrument = importItFile_getNextPatternByte();
                lastInstrument[channel] = instrument;
            }
            if (maskvariable & 4)
            {
                volume = importItFile_getNextPatternByte();
                lastVolume[channel] = volume;
            }
            if (maskvariable & 8)
            {
                command = importItFile_getNextPatternByte();
                commandValue = importItFile_getNextPatternByte();

                lastCommand[channel] = command;
                lastCommandValue[channel] = commandValue;
            }
            if (maskvariable & 16)
            {
                note = lastNote[channel];
            }
            if (maskvariable & 32)
            {
                instrument = lastInstrument[channel];
            }
            if (maskvariable & 64)
            {
                volume = lastVolume[channel];
            }
            if (maskvariable & 128)
            {
                command = lastCommand[channel];
                commandValue = lastCommandValue[channel];
            }

            //			Serial.printf("row: %d, chan: %d, note: %d, ins: %d, command: %d val: %d\n",
            //							row,
            //							channel,
            //							note,
            //							instrument,
            //							command,
            //							commandValue);

            if ((instrument - 1) <= kSampleInstrumentsCount - 1)
            {
                note = clamp<int>(note + instrumentTuneCorrection[instrument - 1], 0, 127);
            }

            importItFile_setStep(row, channel, note, volume, instrument, 0, 0);

            volume = -1;
        }

    } while (bytesInPatternLeft > 0);
    //	else if (loadStatus >= fileTransferError)
    //	{
    //		stopOperationWithError(1);
    //	}
}

void cFileManager::importItFile_setStep(uint8_t step,
                                        uint8_t track,
                                        uint8_t note,
                                        int16_t volume,
                                        uint8_t instrument,
                                        uint8_t fx,
                                        uint8_t fxVal)
{

    if (step > Sequencer::MAXSTEP)
        return;
    if (track > kAllTracks - 1)
        return;
    if (instrument > kSampleInstrumentsCount - 1 || instrument == 0)
        return;

    Sequencer::strPattern::strTrack::strStep* pattStep =
      &sequencer.getActualPattern()->track[track].step[step];

    //	if (volume > 0) Serial.printf("volume: %d\n", volume);
    pattStep->note = note;
    pattStep->instrument = instrument - 1;

    // Volume ranges from 0->64
    if (volume >= 0 && volume <= 64)
    {
        pattStep->fx[0].type = sequencer.fx.FX_TYPE_VOLUME;
        pattStep->fx[0].value = mapClamped<int>(volume, 0, 64, 0, DEFAULT_INSTRUMENT_VOLUME);
    }
    // Panning ranges from 0->64, mapped onto 128->192
    else if (volume > 128 && volume <= 192)
    {
        uint8_t panning = volume - 128;

        pattStep->fx[0].type = sequencer.fx.FX_TYPE_PANNING;
        pattStep->fx[0].value = mapClamped<int>(panning, 0, 64, 0, 100);
    }
    /*
     // Prepare for the following also:
     //  65->74 = Fine volume up
     //  75->84 = Fine volume down
     //  85->94 = Volume slide up
     //  95->104 = Volume slide down
     //  105->114 = Pitch Slide down
     //  115->124 = Pitch Slide up
     //  193->202 = Portamento to
     //  203->212 = Vibrato
     */
}

void cFileManager::importItFile_setPattern(uint8_t index, uint8_t length)
{
    Sequencer::strPattern* patt = sequencer.getActualPattern();
    length = clamp<int>(length, 0, Sequencer::MAXSTEP);

    sequencer.clearPattern(patt);
    mtProject.values.actualPattern = index;
    for (uint8_t a = 0; a < kAllTracks; a++)
    {
        patt->track[a].length = length;
    }
}

void cFileManager::importItFile_savePattern()
{

    Sequencer::strPattern* patt = sequencer.getActualPattern();

    updatePatternBitmask(mtProject.values.actualPattern, patt);

    if (!sequencer.copyCurrentPatternToBuff((uint8_t*)&fileManagerPatternBuffer))
    {
        stopOperationWithError(fmImportModError);
    }

    char patternToSave[PATCH_SIZE];
    sprintf(patternToSave, cWorkspacePatternFileFormat, mtProject.values.actualPattern);

    uint8_t saveStatus = fileTransfer.saveMemoryToFile(
      (uint8_t*)&fileManagerPatternBuffer, patternToSave, sizeof(Sequencer::strPattern));

    if (saveStatus == fileTransferEnd)
    {
    }
    else
    {
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::importItFile_WriteWaves()
{
    strInstrument* instr = &mtProject.instrument[processedSample];

    if (!waveWriteFlag && instr->isActive)
    {
        waveWriteFlag = 1;
        saveLength = instr->sample.length * 2;

        if (saveLength > 1)
        {
            char currentFilename[PATCH_SIZE];
            sprintf(currentFilename, cWorkspaceInstrumentFileFormatNew, processedSample + 1);

            [[maybe_unused]] auto [isOpen, fileHandle] =
              fileSystem->openFile(currentFilename, FileSystem::FileOpenMode::kWriteOpen, &wavfile);
            fileSystem->seekFile(sizeof(strInstrumentFile), wavfile);

            waveSrcPtr = instr->sample.address;
        }
        else
        {
            waveWriteFlag = 0;
        }
    }

    if (saveLength > 1)
    {
        if (saveLength > 2048)
        {
            fileSystem->writeFile(waveSrcPtr, 2048, wavfile);

            waveSrcPtr += 1024;
            saveLength -= 2048;
        }
        else
        {
            waveWriteFlag = 0;

            fileSystem->writeFile(waveSrcPtr, saveLength, wavfile);

            saveLength = 0;

            fileSystem->closeFile(wavfile, false);

            // tu warunek na zmianę kroków
            //			currentOperationStep--;

            //*************

            bSampleOffset += instr->sample.length * 2;
            sample_ptr += instr->sample.length;

            processedSample++;
            if (processedSample > kSampleInstrumentsCount - 1 || processedSample > InsNum)
            {
                moveToNextOperationStep();
            }
        }
    }
    else
    {

        bSampleOffset += instr->sample.length * 2;
        sample_ptr += instr->sample.length;

        processedSample++;
        if (processedSample > kSampleInstrumentsCount - 1 || processedSample > InsNum)
        {
            moveToNextOperationStep();
        }
    }
}

void cFileManager::importItFile_Finish()
{
    mtProject.values.actualPattern = 1;
    newFileManager.loadWorkspacePatternNow(mtProject.values.actualPattern);
    sequencer.switchRamPatternsNow();

    setProjectStructChanged();

    startAudioEngine();

    importModFileAfterNewProject = 0;

    status = fmLoadEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

float cFileManager::importItFile_getProgress()
{
    uint16_t totalSteps = (InsNum > kSampleInstrumentsCount ? kSampleInstrumentsCount : InsNum) +
                          (SmpNum > kSampleInstrumentsCount ? kSampleInstrumentsCount : SmpNum) + PatNum +
                          (InsNum > kSampleInstrumentsCount ? kSampleInstrumentsCount : InsNum);
    uint16_t actualStep = 0;

    if (currentOperationStep < 7)
    {
        actualStep = processedInstrument + processedPattern + processedSample;
    }
    else
    {
        actualStep = SmpNum + InsNum + PatNum + processedSample - 1;
    }

    float retVal = ((float)actualStep / (float)totalSteps) * 100.0;
    retVal = clamp<float>(retVal, 0.0, 100.0);
    return retVal;
}
