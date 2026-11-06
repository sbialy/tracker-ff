#include "DevConsole.h"

#ifdef ENABLE_DEV_CONSOLE

#include "PlatformImpl.h"
#include "TimeControl.h"
#include "config.h"
#include "fsl_common.h"

#include <cstdio>
#include <cstring>

bool onDevStorageMode(bool enable);

namespace
{
constexpr uint32_t kTxBufferSize = 4096; // must be a power of two
constexpr uint32_t kTxBufferMask = kTxBufferSize - 1;
constexpr size_t kFormatBufferSize = 256;
constexpr size_t kLineBufferSize = 96;
// The Original Tracker's bootloader reads the name into a 32-byte buffer;
// keep the same limit here so one .ptf naming rule covers all devices.
constexpr size_t kMaxFirmwareNameLength = 31;
// Time for the reply to reach the host before the MCU resets under it.
constexpr uint32_t kPendingActionDelayMs = 250;
constexpr uint32_t kPendingActionTimeoutMs = 2000;

#ifdef TRACKER_MINI
constexpr char kProductName[] = "mini";
#else
constexpr char kProductName[] = "plus";
#endif

enum class PendingAction
{
    kNone,
    kFlash,
    kReboot,
};

uint8_t txBuffer[kTxBufferSize];
// Free-running indices: head is advanced by writers under an interrupt lock,
// tail only by the single reader in the main loop.
volatile uint32_t txHead = 0;
volatile uint32_t txTail = 0;
volatile uint32_t txDroppedBytes = 0;
volatile uint8_t txLastByte = '\n';

char lineBuffer[kLineBufferSize];
size_t lineLength = 0;
bool lineOverflow = false;

PlatformImpl* devPlatform = nullptr;
PendingAction pendingAction = PendingAction::kNone;
uint32_t pendingSinceMs = 0;
char pendingFirmwareName[kMaxFirmwareNameLength + 1];

void pushLocked(const uint8_t* data, size_t length)
{
    uint32_t freeBytes = kTxBufferSize - (txHead - txTail);
    size_t accepted = length < freeBytes ? length : freeBytes;

    for (size_t i = 0; i < accepted; i++)
    {
        txBuffer[(txHead + i) & kTxBufferMask] = data[i];
    }

    txHead += accepted;
    txDroppedBytes += length - accepted;

    if (accepted > 0)
    {
        txLastByte = data[accepted - 1];
    }
}

// Replies start with '@' at the beginning of a line so the host can tell them
// apart from log output, which may be mid-line when a command arrives.
void reply(const char* format, ...)
{
    char buffer[kFormatBufferSize];
    buffer[0] = '@';

    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer + 1, sizeof(buffer) - 2, format, args);
    va_end(args);

    if (length < 0)
    {
        return;
    }

    size_t total =
      1 + (static_cast<size_t>(length) < sizeof(buffer) - 2 ? static_cast<size_t>(length)
                                                            : sizeof(buffer) - 3);
    buffer[total++] = '\n';

    uint32_t primask = DisableGlobalIRQ();
    if (txLastByte != '\n')
    {
        const uint8_t newline = '\n';
        pushLocked(&newline, 1);
    }
    pushLocked(reinterpret_cast<const uint8_t*>(buffer), total);
    EnableGlobalIRQ(primask);
}

bool isValidFirmwareName(const char* name)
{
    size_t length = strlen(name);

    if (length == 0 || length > kMaxFirmwareNameLength)
    {
        return false;
    }

    return strpbrk(name, "/\\: ") == nullptr;
}

void schedule(PendingAction action)
{
    pendingAction = action;
    pendingSinceMs = millis();
}

void handleStorage(const char* argument)
{
    bool enable = strcmp(argument, "on") == 0;

    if (!enable && strcmp(argument, "off") != 0)
    {
        reply("err usage: storage on|off");
        return;
    }

    bool active = onDevStorageMode(enable);

    if (active == enable)
    {
        reply("ok storage %s", argument);
    }
    else
    {
        // The interface ignores the request outside its normal run state,
        // for example while a project is still loading.
        reply("err storage is still %s", active ? "on" : "off");
    }
}

void handleFlash(const char* name)
{
    if (!isValidFirmwareName(name))
    {
        reply("err bad firmware name");
        return;
    }

    strcpy(pendingFirmwareName, name);
    schedule(PendingAction::kFlash);
    reply("ok flash pending");
}

void handleLine(char* line)
{
    char* argument = strchr(line, ' ');

    if (argument != nullptr)
    {
        *argument++ = '\0';
    }
    else
    {
        argument = line + strlen(line);
    }

    if (strcmp(line, "ping") == 0)
    {
        reply("tracker-dev %s %s", kProductName, devPlatform->getSystemInfo()->buildId);
    }
    else if (strcmp(line, "storage") == 0)
    {
        handleStorage(argument);
    }
    else if (strcmp(line, "flash") == 0)
    {
        handleFlash(argument);
    }
    else if (strcmp(line, "reboot") == 0)
    {
        schedule(PendingAction::kReboot);
        reply("ok reboot pending");
    }
    else if (strcmp(line, "help") == 0)
    {
        reply("ok commands: ping, storage on|off, flash <name>, reboot, help");
    }
    else
    {
        reply("err unknown command: %s", line);
    }
}

void endLine()
{
    if (lineOverflow)
    {
        reply("err line too long");
    }
    else if (lineLength > 0)
    {
        lineBuffer[lineLength] = '\0';
        handleLine(lineBuffer);
    }

    lineLength = 0;
    lineOverflow = false;
}

void writeTurnOnFile()
{
    FileSystem* fileSystem = devPlatform->getFileSystem();
    constexpr char kTurnOnFileData[] = "start";
    auto [isOpen, fileHandle] =
      fileSystem->openFile(kTurnOnFilePath, FileSystem::FileOpenMode::kWriteCreateNew);

    if (isOpen)
    {
        fileSystem->writeFile(kTurnOnFileData, strlen(kTurnOnFileData), fileHandle);
        fileSystem->closeFile(fileHandle);
    }
}

void runFlash()
{
    // The host may have rewritten the card through USB storage mode behind
    // FatFs's back; cached allocation state would be stale without this.
    if (!devPlatform->getFileSystemImpl()->remount())
    {
        reply("err cannot remount the SD card");
        return;
    }

    char path[sizeof("/Firmware/") + kMaxFirmwareNameLength];
    snprintf(path, sizeof(path), "/Firmware/%s", pendingFirmwareName);

    if (!devPlatform->getFileSystem()->exists(path))
    {
        reply("err no such file: %s", path);
        return;
    }

    devPlatform->runBootloader(pendingFirmwareName);
    reply("err cannot write the firmware info file");
}

void runReboot()
{
    // Without this a Mini release build powers itself off after the reset
    // unless the power button happens to be held.
    writeTurnOnFile();
    devPlatform->resetMCU();
}
} // namespace

extern "C" void DevConsole_Write(const uint8_t* data, size_t length)
{
    uint32_t primask = DisableGlobalIRQ();
    pushLocked(data, length);
    EnableGlobalIRQ(primask);
}

extern "C" void DevConsole_Vprintf(const char* format, va_list args)
{
    char buffer[kFormatBufferSize];
    int length = vsnprintf(buffer, sizeof(buffer), format, args);

    if (length <= 0)
    {
        return;
    }

    size_t stored = static_cast<size_t>(length) < sizeof(buffer) ? static_cast<size_t>(length)
                                                                 : sizeof(buffer) - 1;
    DevConsole_Write(reinterpret_cast<const uint8_t*>(buffer), stored);
}

extern "C" void DevConsole_Printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    DevConsole_Vprintf(format, args);
    va_end(args);
}

extern "C" size_t DevConsole_TxRead(uint8_t* destination, size_t maxLength)
{
    uint32_t tail = txTail;
    uint32_t available = txHead - tail;

    if (available == 0)
    {
        uint32_t primask = DisableGlobalIRQ();
        uint32_t dropped = txDroppedBytes;
        txDroppedBytes = 0;
        EnableGlobalIRQ(primask);

        if (dropped == 0)
        {
            return 0;
        }

        int length = snprintf(reinterpret_cast<char*>(destination),
                              maxLength,
                              "\n@log dropped %lu bytes\n",
                              static_cast<unsigned long>(dropped));
        txLastByte = '\n';
        return length > 0 && static_cast<size_t>(length) < maxLength ? length : 0;
    }

    size_t count = available < maxLength ? available : maxLength;

    for (size_t i = 0; i < count; i++)
    {
        destination[i] = txBuffer[(tail + i) & kTxBufferMask];
    }

    txTail = tail + count;
    return count;
}

extern "C" void DevConsole_RxBytes(const uint8_t* data, size_t length)
{
    if (devPlatform == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < length; i++)
    {
        char c = static_cast<char>(data[i]);

        if (c == '\n' || c == '\r')
        {
            endLine();
        }
        else if (lineLength < kLineBufferSize - 1)
        {
            lineBuffer[lineLength++] = c;
        }
        else
        {
            lineOverflow = true;
        }
    }
}

void DevConsole_Init(PlatformImpl* platformImpl)
{
    devPlatform = platformImpl;
}

void DevConsole_Update()
{
    if (pendingAction == PendingAction::kNone)
    {
        return;
    }

    uint32_t elapsedMs = millis() - pendingSinceMs;
    bool outputDrained = txHead == txTail;

    // The timeout covers a host that sent the command and stopped reading.
    if (elapsedMs < kPendingActionDelayMs ||
        (!outputDrained && elapsedMs < kPendingActionTimeoutMs))
    {
        return;
    }

    PendingAction action = pendingAction;
    pendingAction = PendingAction::kNone;

    if (action == PendingAction::kFlash)
    {
        runFlash();
    }
    else
    {
        runReboot();
    }
}

#endif /* ENABLE_DEV_CONSOLE */
