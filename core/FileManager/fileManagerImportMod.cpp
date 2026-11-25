#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include "log.h"
#include <cctype>
#include <cstring>

extern Sequencer::strPattern fileManagerPatternBuffer;
int16_t* modFile_sample_ptr;
uint32_t byteSampleOffset = 0;

static const uint8_t modSampleNameSize = 22;

struct strImportModFile
{
    uint8_t instrumentsCount = 31;
    uint8_t instrumentsActualIndex = 0;
    uint8_t channelsCount = 4;

    uint8_t patternsMax = 1;
    uint8_t patternsActualIndex = 0;

    const uint8_t sampleInfoSize = 30;
    const uint8_t songInfoSize = 1 + 1 + 128 + 4;

    const uint16_t patternSize = 1024;

    uint8_t sampleActualIndex = 0;
    uint8_t waveWriteFlag = 0;
    int16_t* waveSrcPtr;

    uint32_t modImport_saveLength = 0;

} modFileData;

struct strModFileData
{

    char sampleName[modSampleNameSize];
    uint16_t sampleLengthInWords = 0;
    uint8_t finetune = 0;

    /*
     *  Value:  Finetune:
     0        0
     1       +1
     2       +2
     3       +3
     4       +4
     5       +5
     6       +6
     7       +7
     8       -8
     9       -7
     A       -6
     B       -5
     C       -4
     D       -3
     E       -2
     F       -1

     */

    // Range is $00-$40, or 0-64 decimal.
    uint8_t volume = 0;

    /*
     Repeat point for sample 1. Stored as number of words offset
     from start of sample. Multiply by two to get offset in bytes.
     */
    uint16_t repeatPointInWords = 0;

    /*
     Repeat Length for sample 1. Stored as number of words in
     loop. Multiply by two to get replen in bytes.
     */
    uint16_t repeatLengthInWords = 0;

} modSampleData;
/*
 * 'M.K', '4CHN',
 '6CHN','8CHN','FLT4','FLT8.
 */

struct strModSong
{
    uint8_t length = 0;
    uint8_t oldByte = 127;
    uint8_t playlist[128]{ 0 };
    uint8_t theFourLetters[4]{ 0 };

} modSong;

void cFileManager::updateImportModFile()
{
    switch (importModFileType)
    {
        case importModFiletype_mod:

            switch (currentOperationStep)
            {
                case 0: importModFile_Init(); break;
                case 1: importModFile_GetInstrumentData(); break;
                case 2: importModFile_SaveInstrument(); break;
                case 3: importModFile_SongInit(); break;
                case 4: importModFile_Patterns(); break;
                case 5: importModFile_ImportWave(); break;
                case 6: importModFile_WriteWave(); break;
                case 7: importModFile_Finish(); break;
                default:
                    importModFile_Error();
                    stopOperationWithError(fmImportModError);
                    break;
            }
            break;

        case importModFiletype_it:

            //		Serial.printf("progress: %.2f\n", importItFile_getProgress());
            switch (currentOperationStep)
            {

                case 0: importItFile_Init(); break;
                case 1: importItFile_ProcessHeader(); break;
                case 2: importItFile_ProcessSong(); break;
                case 3: importItFile_ProcessOffsets(); break;

                case 4: importItFile_ProcessInstruments(); break;
                case 5: importItFile_LoadSamples(); break;
                case 6: importItFile_ProcessPatterns(); break;
                case 7: importItFile_WriteWaves(); break;
                case 8: importItFile_Finish(); break;
                default:
                    //			importModFile_Error();
                    stopOperationWithError(fmImportModError);
                    break;
            }

            break;

        default: stopOperationWithError(fmImportModError); break;
    }
}

float cFileManager::importMod_getProgress()
{
    float retVal = 0;
    switch (importModFileType)
    {
        case importModFiletype_mod: retVal = ((float)currentOperationStep / 7) * 100; break;

        case importModFiletype_it: retVal = importItFile_getProgress(); break;
        default: break;
    }

    return retVal;
}

void setFx(Sequencer::strPattern::strTrack::strStep* step, uint8_t, uint8_t, uint8_t);

enImportModMode cFileManager::checkImportFileType()
{
    char lowCase[255];
    strcpy(lowCase, impFileData.filename);
    for (uint16_t a = 0; a < 255; a++)
    {
        lowCase[a] = tolower(lowCase[a]);
        if (lowCase[a] == 0)
            break;
    }

    if (strstr(lowCase, ".mod\0") != NULL)
    {
        return importModFiletype_mod;
    }
    else if (strstr(lowCase, ".it\0") != NULL)
    {
        return importModFiletype_it;
    }

    return importModFiletype_notSupported;
}

void cFileManager::importModFile_Init()
{

    importModFileType = checkImportFileType();
    if (importModFileType != importModFiletype_mod)
        return;

    stopAudioEngine();

    // wart wspólne
    byteSampleOffset = 0;
    modFileData.patternsActualIndex = 0;
    modFileData.instrumentsActualIndex = 0;
    modFileData.sampleActualIndex = 0;

    modFileData.waveWriteFlag = 0;

    modFileData.patternsMax = 1;

    modFile_sample_ptr = externalRamSampleMemory;
    memset(externalRamSampleMemory, 0, SAMPLE_POOL_BYTES);

    // wew
    char byteBuffer[4];
    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)byteBuffer,
                                                       4,
                                                       1080,
                                                       fileWholeOnce); // fileDivIntoParts

    //	moveToNextOperationStep();
    if (loadStatus == fileTransferEnd)
    {

        if (strstr(byteBuffer, "M.K.") != NULL)
        {
            modFileData.instrumentsCount = 31;
            modFileData.channelsCount = 4;
            moveToNextOperationStep();
        }
        else
        {
            modFileData.instrumentsCount = 15;
            modFileData.channelsCount = 4;
            moveToNextOperationStep();
            //			stopOperationWithError(2);
        }
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
    //	moveToNextOperationStep();
}
void cFileManager::importModFile_GetInstrumentData()
{
    char byteBuffer[modFileData.sampleInfoSize];
    uint8_t buffOffset = 0;
    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)byteBuffer,
                                                       modFileData.sampleInfoSize, // memo
                                                       20 + modFileData.instrumentsActualIndex *
                                                              modFileData.sampleInfoSize, // offset
                                                       fileWholeOnce); // fileDivIntoParts

    if (loadStatus == fileTransferEnd)
    {

        // sampleName
        //		uint8_t a = 0;
        memset(modSampleData.sampleName, 0, sizeof(modSampleData.sampleName));
        for (uint8_t a = 0; a < modSampleNameSize; a++)
        {
            if (byteBuffer[a] != 0)
            {
                modSampleData.sampleName[a] = byteBuffer[a];
            }
            else
                break;
        }
        buffOffset = modSampleNameSize;

        // sampleLengthInWords
        modSampleData.sampleLengthInWords =
          byteBuffer[buffOffset] << 8 | byteBuffer[buffOffset + 1];
        buffOffset += 2;
        modSampleData.finetune = byteBuffer[buffOffset] & 0b00001111;
        buffOffset++;
        modSampleData.volume = byteBuffer[buffOffset++];

        modSampleData.repeatPointInWords = byteBuffer[buffOffset] << 8 | byteBuffer[buffOffset + 1];
        buffOffset += 2;
        modSampleData.repeatLengthInWords =
          byteBuffer[buffOffset] << 8 | byteBuffer[buffOffset + 1];
        buffOffset += 2;

        //;TUTAJ UTWORZYC PLIK INSTRUMENTU

        moveToNextOperationStep();
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::importModFile_SaveInstrument()
{

    strInstrument* instr = &mtProject.instrument[modFileData.instrumentsActualIndex];
    setDefaultActiveInstrument(instr);

    instr->isActive = 1;
    instr->sample.type = mtSampleTypeWaveFile;

    if (strlen(modSampleData.sampleName) > 0)
    {
        strcpy(instr->sample.file_name, modSampleData.sampleName);
        instr->sample.file_name[31] = 0;
    }
    else
    {
        strcpy(instr->sample.file_name, "-");
        instr->sample.file_name[1] = 0;
    }

    if (modSampleData.repeatLengthInWords > 1)
    {
        instr->playMode = playModeLoopForward;
    }
    else
    {
        instr->playMode = playModeSingleShot;
    }

    // todo: to oblicza z zaladowanegolu sampla
    instr->sample.length = modSampleData.sampleLengthInWords;
    instr->volume = mapClamped<int>(modSampleData.volume, 0, 64, 0, DEFAULT_INSTRUMENT_VOLUME);

    instr->startPoint = 0;

    if (instr->playMode == playModeLoopForward)
    {

        //	instr->loopPoint1 = modSampleData.repeatPointInWords * 2;

        instr->loopPoint1 = mapClamped<int>(
          modSampleData.repeatPointInWords, 0, modSampleData.sampleLengthInWords, 0, MAX_16BIT);

        instr->loopPoint2 =
          mapClamped<int>(modSampleData.repeatPointInWords + modSampleData.repeatLengthInWords,
                          0,
                          modSampleData.sampleLengthInWords,
                          0,
                          MAX_16BIT);
    }

    instr->endPoint = MAX_16BIT;
    //	instr->volume = 100;
    //	instr->volume = 50;

    if (saveInstrument(modFileData.instrumentsActualIndex))
    {
        modFileData.instrumentsActualIndex++;
        if (modFileData.instrumentsActualIndex >= modFileData.instrumentsCount)
        {
            moveToNextOperationStep();
        }
        else
        {
            currentOperationStep--;
        }
    }
}

void cFileManager::importModFile_SongInit()
{

    char byteBuffer[modFileData.songInfoSize];
    //	uint8_t buffOffset = 0;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(impFileData.path,
                                                       (uint8_t*)byteBuffer,
                                                       modFileData.songInfoSize, // memo
                                                       20 + modFileData.instrumentsCount *
                                                              modFileData.sampleInfoSize, // offset
                                                       fileWholeOnce);

    if (loadStatus == fileTransferEnd)
    {

        memcpy(&modSong, &byteBuffer, modFileData.songInfoSize);

        for (uint8_t a = 0; a < 128; a++)
        {
            if (modSong.playlist[a] > modFileData.patternsMax)
            {
                modFileData.patternsMax = modSong.playlist[a];
            }
        }

        moveToNextOperationStep();
    }
    else if (loadStatus >= fileTransferError)
    {
        stopOperationWithError(fmImportModError);
    }
}

void cFileManager::importModFile_Patterns()
{

    char byteBuffer[modFileData.patternSize];
    //	uint8_t buffOffset = 0;

    [[maybe_unused]] uint8_t loadStatus = fileTransfer.loadFileToMemory(
      impFileData.path,
      (uint8_t*)byteBuffer,
      modFileData.patternSize, // memo
      20 + modFileData.instrumentsCount * modFileData.sampleInfoSize + modFileData.songInfoSize +
        modFileData.patternSize * modFileData.patternsActualIndex, // offset
      fileWholeOnce);
    Sequencer::strPattern* patt = sequencer.getActualPattern();

    sequencer.clearPattern(patt);

    mtProject.values.actualPattern = modFileData.patternsActualIndex + 1;

    // przepisuję playlistę
    for (uint8_t a = 0; a < 128; a++)
    {
        mtProject.song.playlist[a] = modSong.playlist[a] + 1;
    }

    // mod narzuca length 64
    for (uint8_t a = 0; a < 8; a++)
    {
        patt->track[a].length = 63;
    }

    uint8_t lastInstr[4]{ kSampleInstrumentsCount - 1, kSampleInstrumentsCount - 1, kSampleInstrumentsCount - 1, kSampleInstrumentsCount - 1 };

    for (uint16_t cell = 0, step = 0; cell < 256; cell++)
    {

        // todo: kiedyś odwrócić te bajty
        uint32_t chData;
        uint8_t chan = cell % 4;

        *((uint8_t*)&chData + 3) = byteBuffer[cell * 4];
        *((uint8_t*)&chData + 2) = byteBuffer[cell * 4 + 1];
        *((uint8_t*)&chData + 1) = byteBuffer[cell * 4 + 2];
        *((uint8_t*)&chData + 0) = byteBuffer[cell * 4 + 3];

        uint8_t modInstr = ((chData >> 12) & 0x0000000f) | ((chData >> 24) & 0xf0);
        modInstr = clamp<int>(modInstr, 0, kSampleInstrumentsCount - 1);

        uint16_t period = (chData >> 16) & 0xfff;
        int8_t note = periodToNote(period) - 5;

        uint8_t fx3 = chData & 0x00f;
        uint8_t fx2 = (chData & 0x0f0) >> 4;
        uint8_t fx1 = (chData & 0xf00) >> 8;

        //		Serial.printf("inst %d note %d, fx1 %d, fx2 %d, fx3 %d\t\t",
        //						modInstr,
        //						note,
        //						fx1,
        //						fx2, fx3);

        if (step <= Sequencer::MAXSTEP)
        {
            if (note >= 0 && modInstr > 0 && modInstr <= kSampleInstrumentsCount - 1)
            {
                patt->track[chan].step[step].note = note;
                patt->track[chan].step[step].instrument = modInstr - 1;
                lastInstr[chan] = patt->track[chan].step[step].instrument;
            }
            else if (note >= 0 && modInstr == 0)
            {
                patt->track[chan].step[step].note = note;
                patt->track[chan].step[step].instrument = lastInstr[chan];
            }

            setFx(&patt->track[chan].step[step], fx1, fx2, fx3);
        }
        if (chan == 3)
        {
            step++;
            LOG("");
        }
    }

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
        sequencer.saveToFileDone();
        if (modFileData.patternsActualIndex >= modFileData.patternsMax)
        {
            moveToNextOperationStep();
        }
        else
        {
            modFileData.patternsActualIndex++;
        }
    }
    else
    {
        stopOperationWithError(fmImportModError);
    }
}

void setFx(Sequencer::strPattern::strTrack::strStep* step, uint8_t fx1, uint8_t fx2, uint8_t fx3)
{
    uint16_t modfxTempo;

    switch (fx1)
    {
        case 0: break;
        case 3:
            /*
             Where [3][x][y] means "smoothly change the period of current sample
             by x*16+y after each tick in the division, never sliding beyond
             current period". Any note in this channel's division is not played,
             but changes the "remembered" note - it can be thought of as a
             parameter to this effect. Sliding to a note is similar to effects
             [1] and [2], but the slide will not go beyond the given period, and
             the direction is implied by that period. If x and y are both 0,
             then the old slide will continue.
             */
            step->fx[0].type = sequencer.fx.FX_TYPE_GLIDE;
            step->fx[0].value = clamp<int>(fx2 * 16 + fx3,
                                           sequencer.getFxMin(sequencer.fx.FX_TYPE_GLIDE),
                                           sequencer.getFxMax(sequencer.fx.FX_TYPE_GLIDE));

            break;
        case 9:
            /*
             * [9]: Set sample offset
             Where [9][x][y] means "play the sample from offset x*4096 + y*256".
             The offset is measured in words. If no sample is given, yet one is
             still playing on this channel, it should be retriggered to the new
             offset using the current volume..*/

            step->fx[0].type = sequencer.fx.FX_TYPE_POSITION;
            step->fx[0].value =
              clamp<int>(mapClamped<int>(fx2 * 16 + fx3,
                                         0,
                                         256,
                                         sequencer.getFxMin(sequencer.fx.FX_TYPE_POSITION),
                                         sequencer.getFxMax(sequencer.fx.FX_TYPE_POSITION)),
                         sequencer.getFxMin(sequencer.fx.FX_TYPE_POSITION),
                         sequencer.getFxMax(sequencer.fx.FX_TYPE_POSITION));

            break;
        case 12:
            /*
             [12]: Set volume
             Where [12][x][y] means "set current sample's volume to x*16+y".
             Legal volumes are 0..64.*/

            step->fx[0].type = sequencer.fx.FX_TYPE_VOLUME;
            step->fx[0].value =
              clamp<int>(mapClamped<int>(fx2 * 16 + fx3,
                                         0,
                                         64,
                                         sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                                         DEFAULT_INSTRUMENT_VOLUME),
                         sequencer.getFxMin(sequencer.fx.FX_TYPE_VOLUME),
                         DEFAULT_INSTRUMENT_VOLUME);

            break;
        case 13:
            /*
             * break
             */

            step->fx[0].type = sequencer.fx.FX_TYPE_BREAK_PATTERN;
            step->fx[0].value = sequencer.getFxDefault(sequencer.fx.FX_TYPE_BREAK_PATTERN);

            break;
        case 15:
            /*
             * TEMPO
             *  20-256*/
            modfxTempo = clamp<int>(fx2 * 16 + fx3,
                                    sequencer.getFxMin(sequencer.fx.FX_TYPE_TEMPO),
                                    sequencer.getFxMax(sequencer.fx.FX_TYPE_TEMPO));

            if (modfxTempo > 32)
            {
                step->fx[0].type = sequencer.fx.FX_TYPE_TEMPO;
                step->fx[0].value = modfxTempo;
            }

            break;

        default: break;
    }
}

// pobieramy wave z mod
void cFileManager::importModFile_ImportWave()
{

    //	mtProject.used_memory

    strInstrument* instr = &mtProject.instrument[modFileData.sampleActualIndex];

    // sdram_sampleBank
    //	char byteBuffer[modFileData.modPatternSize];
    //	uint8_t buffOffset = 0;

    uint8_t loadStatus = fileTransfer.loadFileToMemory(
      impFileData.path,
      (uint8_t*)modFile_sample_ptr,
      instr->sample.length * 2, // memo
      20 + modFileData.instrumentsCount * modFileData.sampleInfoSize + modFileData.songInfoSize +
        modFileData.patternSize * (modFileData.patternsMax + 1) + byteSampleOffset, // offset
      fileWholeOnce);

    instr->sample.address = modFile_sample_ptr;

    mtProject.sampleMemory[modFileData.sampleActualIndex].address[strSampleMemory::Channel::kLeft] =
      instr->sample.address;
    mtProject.sampleMemory[modFileData.sampleActualIndex]
      .address[strSampleMemory::Channel::kRight] =
      mtProject.sampleMemory[modFileData.sampleActualIndex]
        .address[strSampleMemory::Channel::kLeft];

    if (loadStatus == fileTransferEnd)
    {
        mtProject.used_memory += instr->sample.length * 2;
        if (modFileData.sampleActualIndex >= modFileData.instrumentsCount)
        {
            // 2 do przodu
            moveToNextOperationStep();
            moveToNextOperationStep();
        }
        else
        {

            moveToNextOperationStep();
        }
    }
    else
    {
    }

    //	instr->sample.address = modSampleData.sampleLengthInWords;
}

// zapisujemy do pliku
void cFileManager::importModFile_WriteWave()
{
    static FileSystem::FileHandle waveFile = nullptr;

    strInstrument* instr = &mtProject.instrument[modFileData.sampleActualIndex];

    if (!modFileData.waveWriteFlag)
    {
        modFileData.waveWriteFlag = 1;
        modFileData.modImport_saveLength = instr->sample.length * 2;

        if (modFileData.modImport_saveLength > 1)
        {
            char currentFilename[PATCH_SIZE];
            sprintf(currentFilename,
                    cWorkspaceInstrumentFileFormatNew,
                    modFileData.sampleActualIndex + 1);

            [[maybe_unused]] auto [isOpen, fileHandle] = fileSystem->openFile(
              currentFilename, FileSystem::FileOpenMode::kWriteOpen, &waveFile);

            fileSystem->seekFile(sizeof(strInstrumentFile), waveFile);

            modFileData.waveSrcPtr = instr->sample.address;
        }
        else
        {
            modFileData.waveWriteFlag = 0;
        }
    }

    if (modFileData.modImport_saveLength > 1)
    {
        if (modFileData.modImport_saveLength > 2048)
        {
            fileSystem->writeFile(modFileData.waveSrcPtr, 2048, waveFile);

            modFileData.waveSrcPtr += 1024;
            modFileData.modImport_saveLength -= 2048;
        }
        else
        {
            modFileData.waveWriteFlag = 0;

            fileSystem->writeFile(
              modFileData.waveSrcPtr, modFileData.modImport_saveLength, waveFile);
            fileSystem->closeFile(waveFile, false);

            modFileData.modImport_saveLength = 0;

            // tu warunek na zmianę kroków
            currentOperationStep--;

            byteSampleOffset += instr->sample.length * 2;
            modFile_sample_ptr += instr->sample.length;
            modFileData.sampleActualIndex++;
        }
    }
    else
    {
        currentOperationStep--;

        byteSampleOffset += instr->sample.length * 2;
        modFile_sample_ptr += instr->sample.length;
        modFileData.sampleActualIndex++;
    }

    // dokonczyc

    /*
     * koniec zapisu
     */
}
void cFileManager::importModFile_Finish()
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

void cFileManager::importModFile_Error()
{
    importModFileAfterNewProject = 0;
}

int8_t cFileManager::periodToNote(uint16_t period)
{
    switch (period)
    {
        case 1712: return 0;
        case 1616: return 1;
        case 1525: return 2;
        case 1440: return 3;
        case 1357: return 4;
        case 1281: return 5;
        case 1209: return 6;
        case 1141: return 7;
        case 1077: return 8;
        case 1017: return 9;
        case 961: return 10;
        case 907: return 11;
        case 856: return 12;
        case 808: return 13;
        case 762: return 14;
        case 720: return 15;
        case 678: return 16;
        case 640: return 17;
        case 604: return 18;
        case 570: return 19;
        case 538: return 20;
        case 508: return 21;
        case 480: return 22;
        case 453: return 23;
        case 428: return 24;
        case 404: return 25;
        case 381: return 26;
        case 360: return 27;
        case 339: return 28;
        case 320: return 29;
        case 302: return 30;
        case 285: return 31;
        case 269: return 32;
        case 254: return 33;
        case 240: return 34;
        case 226: return 35;
        case 214: return 36;
        case 202: return 37;
        case 190: return 38;
        case 180: return 39;
        case 170: return 40;
        case 160: return 41;
        case 151: return 42;
        case 143: return 43;
        case 135: return 44;
        case 127: return 45;
        case 120: return 46;
        case 113: return 47;
        case 107: return 48;
        case 101: return 49;
        case 95: return 50;
        case 90: return 51;
        case 85: return 52;
        case 80: return 53;
        case 76: return 54;
        case 71: return 55;
        case 67: return 56;
        case 64: return 57;
        case 60: return 58;
        case 57: return 59;

        default: return -1;
    }
    return -1;
}

void cFileManager::printNote(uint8_t note)
{
    switch (note % 12)
    {
        case 0: LOGF("%s", "C"); break;
        case 1: LOGF("%s", "C#"); break;
        case 2: LOGF("%s", "D"); break;
        case 3: LOGF("%s", "D#"); break;
        case 4: LOGF("%s", "E"); break;
        case 5: LOGF("%s", "F"); break;
        case 6: LOGF("%s", "F"); break;
        case 7: LOGF("%s", "G"); break;
        case 8: LOGF("%s", "G#"); break;
        case 9: LOGF("%s", "A"); break;
        case 10: LOGF("%s", "A#"); break;
        case 11: LOGF("%s", "B"); break;
    }

    LOGF("%d", note / 12);
}
