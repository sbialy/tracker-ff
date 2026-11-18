#include "ExportQueues.h"

AudioRecordQueue* ExportQueues::getQueue(ExportChannels channel, mtAudioExport::enStem stem)
{
    return &queue[channel][stem];
}
