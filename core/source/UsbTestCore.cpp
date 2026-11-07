#include "UsbTestCore.h"

#if defined(ENABLE_DEV_CONSOLE) && !defined(PLATFORM_IMX)

#include <string.h>

namespace usbtest
{
namespace
{
constexpr uint32_t kTaskTimeoutMs = 3000; // the host stopped reading or writing
constexpr uint32_t kOpenTimeoutMs = 120000;
constexpr uint32_t kIdleTimeoutMs = 15000;
constexpr uint32_t kIdleSettleMs = 100;
constexpr uint32_t kRebootDelayMs = 250;
constexpr uint32_t kTapHoldMs = 70;
constexpr uint32_t kTapGapMs = 70;
constexpr uint32_t kReplyRoom = 600; // free ring bytes needed before the next command line is taken
constexpr int kMaxArgs = 8;

const uint32_t kCrcNibble[16] = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
                                  0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
                                  0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                  0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };

struct ButtonName
{
    const char* name;
    uint8_t id;
};

// ids as Interface::buttonChange takes them (same table as synth-open/tools/ui_shot)
const ButtonName kButtons[] = {
    { "F1", 0 },          { "F2", 1 },         { "F3", 2 },         { "F4", 3 },
    { "F5", 4 },          { "F6", 5 },         { "F7", 6 },         { "F8", 7 },
    { "Params", 8 },      { "Performance", 9 }, { "File", 10 },     { "Config", 11 },
    { "Master", 12 },     { "SampleLoad", 13 }, { "SamplePlay", 14 }, { "SampleEdit", 15 },
    { "SampleRec", 16 },  { "Song", 17 },      { "Note", 18 },      { "Instr", 19 },
    { "Vol", 20 },        { "Fx", 21 },        { "Pattern", 22 },   { "Play", 23 },
    { "Copy", 24 },       { "Insert", 25 },    { "Up", 26 },        { "Delete", 27 },
    { "Rec", 28 },        { "Shift", 29 },     { "Left", 30 },      { "Down", 31 },
    { "Right", 32 },
};
constexpr int kButtonNames = sizeof(kButtons) / sizeof(kButtons[0]);

inline void store32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

inline uint32_t load32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint32_t pixelDelta(const uint32_t* cur, const uint32_t* prev, int i)
{
    return prev != nullptr ? (cur[i] ^ prev[i]) : cur[i];
}

// both passes of the row coder walk the row the same way
size_t rleWalk(uint8_t* out, const uint32_t* cur, const uint32_t* prev, int count)
{
    size_t n = 0;
    int i = 0;
    while (i < count)
    {
        const uint32_t v = pixelDelta(cur, prev, i);
        int run = 1;
        while (i + run < count && run < 129 && pixelDelta(cur, prev, i + run) == v)
        {
            run++;
        }
        if (run >= 2)
        {
            if (out != nullptr)
            {
                out[n] = uint8_t(0x80 | (run - 2));
                store32(out + n + 1, v);
            }
            n += 5;
            i += run;
            continue;
        }
        const int start = i;
        int literals = 1;
        i++;
        while (i < count && literals < 128)
        {
            if (i + 1 < count && pixelDelta(cur, prev, i) == pixelDelta(cur, prev, i + 1))
            {
                break;
            }
            literals++;
            i++;
        }
        if (out != nullptr)
        {
            out[n] = uint8_t(literals - 1);
            for (int k = 0; k < literals; k++)
            {
                store32(out + n + 1 + 4 * k, pixelDelta(cur, prev, start + k));
            }
        }
        n += 1 + 4 * size_t(literals);
    }
    return n;
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// decimal, 0x hex, optional sign
bool parseNumber(const char* s, long long* out)
{
    if (s == nullptr || *s == 0)
    {
        return false;
    }
    bool negative = false;
    if (*s == '-' || *s == '+')
    {
        negative = *s == '-';
        s++;
    }
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (*s == 0)
    {
        return false;
    }
    unsigned long long v = 0;
    for (; *s != 0; s++)
    {
        int d;
        if (isDigit(*s))
        {
            d = *s - '0';
        }
        else if (*s >= 'a' && *s <= 'f')
        {
            d = *s - 'a' + 10;
        }
        else if (*s >= 'A' && *s <= 'F')
        {
            d = *s - 'A' + 10;
        }
        else
        {
            return false;
        }
        if (d >= base)
        {
            return false;
        }
        v = v * unsigned(base) + unsigned(d);
        if (v > 0xFFFFFFFFull)
        {
            return false;
        }
    }
    *out = negative ? -(long long)(v) : (long long)(v);
    return true;
}

bool parseInt(const char* s, long* out)
{
    long long v;
    if (!parseNumber(s, &v) || v > 0x7FFFFFFFll || v < -0x7FFFFFFFll)
    {
        return false;
    }
    *out = long(v);
    return true;
}

bool parseU32(const char* s, uint32_t* out)
{
    long long v;
    if (!parseNumber(s, &v) || v < 0)
    {
        return false;
    }
    *out = uint32_t(v);
    return true;
}

// Splits in place on spaces. A double quoted part is one argument (project names have spaces).
int tokenize(char* line, char** argv, int maxArgs)
{
    int argc = 0;
    char* p = line;
    while (*p != 0 && argc < maxArgs)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == 0)
        {
            break;
        }
        if (*p == '"')
        {
            p++;
            argv[argc++] = p;
            while (*p != 0 && *p != '"')
            {
                p++;
            }
        }
        else
        {
            argv[argc++] = p;
            while (*p != 0 && *p != ' ' && *p != '\t')
            {
                p++;
            }
        }
        if (*p != 0)
        {
            *p++ = 0;
        }
    }
    return argc;
}

void copyText(char* dest, size_t size, const char* src)
{
    size_t i = 0;
    for (; i + 1 < size && src[i] != 0; i++)
    {
        dest[i] = src[i];
    }
    dest[i] = 0;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// A small formatter instead of newlib's: %d %i %u %x %X %c %s %%, 'l', a 0 flag, a width and a precision for %s. No floats
// (a %f prints ?). It needs a few dozen bytes of stack where vsnprintf with float support needs hundreds, and the console's
// LOG path runs in the audio interrupt, which has no stack to spare.
size_t vformat(char* out, size_t capacity, const char* format, va_list args)
{
    size_t n = 0;
    auto put = [&](char c) {
        if (n + 1 < capacity)
        {
            out[n] = c;
        }
        n++;
    };
    for (const char* f = format; *f != 0; f++)
    {
        if (*f != '%')
        {
            put(*f);
            continue;
        }
        f++;
        bool zero = false;
        int width = 0;
        int precision = -1;
        if (*f == '0')
        {
            zero = true;
            f++;
        }
        while (*f >= '0' && *f <= '9')
        {
            width = width * 10 + (*f++ - '0');
        }
        if (*f == '.')
        {
            f++;
            precision = 0;
            while (*f >= '0' && *f <= '9')
            {
                precision = precision * 10 + (*f++ - '0');
            }
        }
        bool isLong = false;
        while (*f == 'l' || *f == 'z')
        {
            isLong = true;
            f++;
        }
        const char kind = *f;
        if (kind == 0)
        {
            break;
        }
        char digits[24];
        int count = 0;
        bool negative = false;
        const char* text = nullptr;
        switch (kind)
        {
            case '%': put('%'); continue;
            case 'c': put(char(va_arg(args, int))); continue;
            case 's':
            {
                text = va_arg(args, const char*);
                if (text == nullptr)
                {
                    text = "(null)";
                }
                int length = 0;
                while (text[length] != 0 && (precision < 0 || length < precision))
                {
                    length++;
                }
                for (int i = length; i < width; i++)
                {
                    put(' ');
                }
                for (int i = 0; i < length; i++)
                {
                    put(text[i]);
                }
                continue;
            }
            case 'd':
            case 'i':
            {
                const long v = isLong ? va_arg(args, long) : long(va_arg(args, int));
                negative = v < 0;
                unsigned long u = negative ? 0ul - static_cast<unsigned long>(v) : static_cast<unsigned long>(v);
                do
                {
                    digits[count++] = char('0' + u % 10);
                    u /= 10;
                } while (u != 0);
                break;
            }
            case 'u':
            {
                unsigned long u = isLong ? va_arg(args, unsigned long) : static_cast<unsigned long>(va_arg(args, unsigned));
                do
                {
                    digits[count++] = char('0' + u % 10);
                    u /= 10;
                } while (u != 0);
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned long u = isLong ? va_arg(args, unsigned long) : static_cast<unsigned long>(va_arg(args, unsigned));
                do
                {
                    const unsigned d = unsigned(u & 15);
                    digits[count++] = char(d < 10 ? '0' + d : (kind == 'x' ? 'a' : 'A') + d - 10);
                    u >>= 4;
                } while (u != 0);
                break;
            }
            default:
                if (kind == 'f' || kind == 'g' || kind == 'e')
                {
                    (void)va_arg(args, double);
                }
                put('?');
                continue;
        }
        const int length = count + (negative ? 1 : 0);
        if (!zero)
        {
            for (int i = length; i < width; i++)
            {
                put(' ');
            }
        }
        if (negative)
        {
            put('-');
        }
        if (zero)
        {
            for (int i = length; i < width; i++)
            {
                put('0');
            }
        }
        while (count > 0)
        {
            put(digits[--count]);
        }
    }
    if (capacity > 0)
    {
        out[n < capacity ? n : capacity - 1] = 0;
    }
    return n;
}

size_t format(char* out, size_t capacity, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const size_t n = vformat(out, capacity, format, args);
    va_end(args);
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------
uint32_t crc32(uint32_t crc, const void* data, size_t length)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (length-- > 0)
    {
        crc ^= *p++;
        crc = (crc >> 4) ^ kCrcNibble[crc & 15];
        crc = (crc >> 4) ^ kCrcNibble[crc & 15];
    }
    return ~crc;
}

size_t rleSize(const uint32_t* cur, const uint32_t* prev, int count)
{
    return rleWalk(nullptr, cur, prev, count);
}

size_t rleEncode(uint8_t* out, const uint32_t* cur, const uint32_t* prev, int count)
{
    return rleWalk(out, cur, prev, count);
}

bool rleDecode(const uint8_t* in, size_t length, uint32_t* row, const uint32_t* prev, int count)
{
    size_t n = 0;
    int i = 0;
    while (n < length && i < count)
    {
        const uint8_t c = in[n++];
        if (c < 0x80)
        {
            const int literals = c + 1;
            if (n + 4 * size_t(literals) > length || i + literals > count)
            {
                return false;
            }
            for (int k = 0; k < literals; k++, n += 4)
            {
                row[i + k] = load32(in + n) ^ (prev != nullptr ? prev[i + k] : 0);
            }
            i += literals;
        }
        else
        {
            const int run = (c & 0x7F) + 2;
            if (n + 4 > length || i + run > count)
            {
                return false;
            }
            const uint32_t v = load32(in + n);
            n += 4;
            for (int k = 0; k < run; k++)
            {
                row[i + k] = v ^ (prev != nullptr ? prev[i + k] : 0);
            }
            i += run;
        }
    }
    return i == count && n == length;
}

int buttonByName(const char* name)
{
    for (int i = 0; i < kButtonNames; i++)
    {
        if (strcmp(kButtons[i].name, name) == 0)
        {
            return kButtons[i].id;
        }
    }
    return -1;
}

const char* buttonName(int id)
{
    for (int i = 0; i < kButtonNames; i++)
    {
        if (kButtons[i].id == id)
        {
            return kButtons[i].name;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
void Ring::clear()
{
    head = 0;
    tail = 0;
    lastByte = '\n';
}

bool Ring::push(const uint8_t* data, size_t length)
{
    if (length > freeBytes())
    {
        dropped += uint32_t(length);
        return false;
    }
    for (size_t i = 0; i < length; i++)
    {
        buf[(head + i) & (kRingSize - 1)] = data[i];
    }
    head += uint32_t(length);
    if (length > 0)
    {
        lastByte = data[length - 1];
    }
    return true;
}

size_t Ring::peek(uint8_t* dest, size_t maxLength)
{
    size_t n = used();
    if (n > maxLength)
    {
        n = maxLength;
    }
    for (size_t i = 0; i < n; i++)
    {
        dest[i] = buf[(tail + i) & (kRingSize - 1)];
    }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------
void Console::init(Host* host)
{
    memset(this, 0, sizeof(*this));
    host_ = host;
    ring_.clear();
}

bool Console::pending() const
{
    // a put waits for the host's bytes, stepping does not move it on
    if ((task_ != kTaskNone && task_ != kTaskPut) || ring_.used() != 0)
    {
        return true;
    }
    for (const Scheduled& item : queue_)
    {
        if (item.used != 0)
        {
            return true;
        }
    }
    return false;
}

bool Console::pushLocked(const uint8_t* data, size_t length)
{
    return ring_.push(data, length);
}

// A line of text (log or reply). Starts on a fresh line, ends with a newline, all or nothing.
void Console::log(const char* text)
{
    if (!enabled_ || !linkWasUp_ || host_ == nullptr)
    {
        return;
    }
    const size_t length = strlen(text);
    const uint32_t state = host_->irqLock();
    const bool lead = ring_.lastByte != '\n';
    if (blockOpen_ == 0 && ring_.freeBytes() >= length + 1 + (lead ? 1 : 0))
    {
        const uint8_t nl = '\n';
        if (lead)
        {
            pushLocked(&nl, 1);
        }
        pushLocked(reinterpret_cast<const uint8_t*>(text), length);
        pushLocked(&nl, 1);
    }
    else
    {
        ring_.dropped += uint32_t(length);
    }
    host_->irqUnlock(state);
}

void Console::logf(const char* format, va_list args)
{
    if (!enabled_ || !linkWasUp_ || host_ == nullptr)
    {
        return;
    }
    // one shared buffer, not the caller's stack: logf runs in the audio interrupt. Whoever finds it taken (an interrupt that
    // arrived while the main loop was formatting) drops its line.
    uint32_t state = host_->irqLock();
    if (formatBusy_ != 0)
    {
        ring_.dropped++;
        host_->irqUnlock(state);
        return;
    }
    formatBusy_ = 1;
    host_->irqUnlock(state);
    vformat(formatBuffer_, sizeof(formatBuffer_), format, args);
    log(formatBuffer_);
    formatBusy_ = 0;
}

void Console::replyV(const char* format, va_list args)
{
    char buffer[200];
    buffer[0] = '@';
    vformat(buffer + 1, sizeof(buffer) - 1, format, args);
    log(buffer);
}

void Console::reply(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    replyV(format, args);
    va_end(args);
}

// header line, payload, newline: one unit, never split by a log line. The copy takes the interrupt lock in small steps
// (a 3 KB payload under one lock would hold off the audio interrupt for 150 us), and a log line from an interrupt that
// arrives meanwhile is dropped (blockOpen_) so it cannot land in the middle of the payload.
bool Console::pushBlock(const char* header, const uint8_t* data, size_t length)
{
    const size_t headerLength = strlen(header);
    const uint8_t nl = '\n';
    uint32_t state = host_->irqLock();
    const bool lead = ring_.lastByte != '\n';
    if (ring_.freeBytes() < headerLength + 1 + length + 1 + (lead ? 1 : 0))
    {
        ring_.dropped += uint32_t(length);
        host_->irqUnlock(state);
        return false;
    }
    blockOpen_ = 1;
    if (lead)
    {
        pushLocked(&nl, 1);
    }
    pushLocked(reinterpret_cast<const uint8_t*>(header), headerLength);
    pushLocked(&nl, 1);
    host_->irqUnlock(state);

    for (size_t done = 0; done < length;)
    {
        const size_t n = length - done < 256 ? length - done : 256;
        state = host_->irqLock();
        pushLocked(data + done, n);
        host_->irqUnlock(state);
        done += n;
    }

    state = host_->irqLock();
    pushLocked(&nl, 1);
    blockOpen_ = 0;
    host_->irqUnlock(state);
    return true;
}

void Console::txPump()
{
    for (int i = 0; i < 16; i++)
    {
        size_t room = host_->txFree();
        if (room == 0 || ring_.used() == 0)
        {
            return;
        }
        uint8_t tmp[64];
        size_t n = ring_.peek(tmp, room < sizeof(tmp) ? room : sizeof(tmp));
        const size_t accepted = host_->txWrite(tmp, n);
        ring_.tail += uint32_t(accepted);
        if (accepted < n)
        {
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Console::update()
{
    if (!enabled_ || host_ == nullptr)
    {
        return;
    }
    now_ = host_->nowMs();
    pumpInput(now_);

    if (!host_->linkUp())
    {
        if (linkWasUp_)
        {
            linkWasUp_ = false;
            abortTask();
            ring_.clear();
            lineLength_ = 0;
            lineOverflow_ = false;
        }
        // a script that died with keys down must not leave them down
        if (heldButtons_ != 0 || heldPads_ != 0)
        {
            for (int i = 0; i < 64; i++)
            {
                if (((heldButtons_ >> i) & 1) != 0 && host_->injectButton(uint8_t(i), false))
                {
                    heldButtons_ &= ~(uint64_t(1) << i);
                }
                if (((heldPads_ >> i) & 1) != 0 && host_->injectPad(uint8_t(i), false))
                {
                    heldPads_ &= ~(uint64_t(1) << i);
                }
            }
        }
        return;
    }
    if (!linkWasUp_)
    {
        linkWasUp_ = true;
        ring_.clear();
        while (host_->rxByte() >= 0)
        {
            // text typed before we looked is not a command
        }
    }

    rxPump();
    runTask(now_);
    txPump();
}

void Console::rxPump()
{
    for (int budget = 0; budget < 1024; budget++)
    {
        if (task_ == kTaskPut)
        {
            const int b = host_->rxByte();
            if (b < 0)
            {
                return;
            }
            taskLastMs_ = now_;
            putByte(uint8_t(b));
            continue;
        }
        if (!needSpace(kReplyRoom))
        {
            return;
        }
        const int b = host_->rxByte();
        if (b < 0)
        {
            return;
        }
        if (b == '\r')
        {
            continue;
        }
        if (b == '\n')
        {
            if (lineOverflow_)
            {
                reply("err line too long");
            }
            else
            {
                line_[lineLength_] = 0;
                execLine(line_);
            }
            lineLength_ = 0;
            lineOverflow_ = false;
            continue;
        }
        if (lineLength_ + 1u < kLineMax)
        {
            line_[lineLength_++] = char(b);
        }
        else
        {
            lineOverflow_ = true;
        }
    }
}

bool Console::badPath(const char* path) const
{
    const size_t length = strlen(path);
    if (length == 0 || length >= sizeof(path_))
    {
        return true;
    }
    for (size_t i = 0; i < length; i++)
    {
        if (static_cast<unsigned char>(path[i]) < 32 || path[i] == '\\' || path[i] == ':')
        {
            return true;
        }
        if (path[i] == '.' && path[i + 1] == '.')
        {
            return true;
        }
    }
    return false;
}

bool Console::schedule(uint8_t kind, uint8_t id, bool down, uint32_t due)
{
    for (Scheduled& s : queue_)
    {
        if (s.used == 0)
        {
            s.used = 1;
            s.kind = kind;
            s.id = id;
            s.down = down ? 1 : 0;
            s.due = due;
            return true;
        }
    }
    return false;
}

void Console::pumpInput(uint32_t now)
{
    for (int round = 0; round < 8; round++)
    {
        int pick = -1;
        for (int i = 0; i < int(sizeof(queue_) / sizeof(queue_[0])); i++)
        {
            if (queue_[i].used != 0 && int32_t(now - queue_[i].due) >= 0 &&
                (pick < 0 || int32_t(queue_[pick].due - queue_[i].due) > 0))
            {
                pick = i;
            }
        }
        if (pick < 0)
        {
            return;
        }
        Scheduled& s = queue_[pick];
        const bool down = s.down != 0;
        const bool ok = s.kind == 0 ? host_->injectButton(s.id, down) : host_->injectPad(s.id, down);
        if (!ok)
        {
            return; // the driver's queue is full, try again next pass
        }
        uint64_t& held = s.kind == 0 ? heldButtons_ : heldPads_;
        if (down)
        {
            held |= uint64_t(1) << s.id;
        }
        else
        {
            held &= ~(uint64_t(1) << s.id);
        }
        lastInputMs_ = now;
        s.used = 0;
    }
}

void Console::abortTask()
{
    switch (task_)
    {
        case kTaskShot: host_->shotEnd(); break;
        case kTaskLs: host_->dirClose(); break;
        case kTaskGet:
            if (file_ != nullptr)
            {
                host_->fileClose(file_);
            }
            break;
        case kTaskPut:
            if (file_ != nullptr)
            {
                host_->fileClose(file_);
                host_->fileRemove(path_);
            }
            break;
        default: break;
    }
    file_ = nullptr;
    task_ = kTaskNone;
}

void Console::finishTask()
{
    task_ = kTaskNone;
    file_ = nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
void Console::execLine(char* line)
{
    char* argv[kMaxArgs];
    const int argc = tokenize(line, argv, kMaxArgs);
    if (argc == 0)
    {
        return;
    }
    const char* cmd = argv[0];

    // answered at any time
    if (strcmp(cmd, "ping") == 0)
    {
        reply("tracker-dev original %s proto=%d", host_->buildId(), kProtocol);
        return;
    }
    if (strcmp(cmd, "help") == 0)
    {
        reply("ok ping help cpu mem idle reboot key pad enc status shotcrc shot texts open play stop peek hash "
              "markers ls get put rm abort");
        return;
    }
    if (strcmp(cmd, "cpu") == 0)
    {
        cmdCpu(argc, argv);
        return;
    }
    if (strcmp(cmd, "mem") == 0)
    {
        reply("mem free=%lu", static_cast<unsigned long>(host_->freeRam()));
        return;
    }
    if (strcmp(cmd, "status") == 0)
    {
        cmdStatus();
        return;
    }
    if (strcmp(cmd, "markers") == 0)
    {
        cmdMarkers();
        return;
    }
    if (strcmp(cmd, "abort") == 0)
    {
        abortTask();
        reply("ok abort");
        return;
    }
    if (task_ != kTaskNone)
    {
        reply("err busy task=%d", int(task_));
        return;
    }

    if (strcmp(cmd, "key") == 0)
    {
        cmdKey(argc, argv, false);
    }
    else if (strcmp(cmd, "pad") == 0)
    {
        cmdKey(argc, argv, true);
    }
    else if (strcmp(cmd, "enc") == 0)
    {
        cmdEnc(argc, argv);
    }
    else if (strcmp(cmd, "idle") == 0)
    {
        task_ = kTaskIdle;
        taskStartMs_ = now_;
        idleSinceMs_ = 0;
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
        task_ = kTaskReboot;
        taskStartMs_ = now_;
        reply("ok reboot pending");
    }
    else if (strcmp(cmd, "shot") == 0)
    {
        cmdShot(argc, argv, false);
    }
    else if (strcmp(cmd, "shotcrc") == 0)
    {
        cmdShot(argc, argv, true);
    }
    else if (strcmp(cmd, "texts") == 0)
    {
        task_ = kTaskTexts;
        taskStartMs_ = taskLastMs_ = now_;
        cursor_ = 0;
        count_ = 0;
    }
    else if (strcmp(cmd, "open") == 0)
    {
        cmdOpen(argc, argv);
    }
    else if (strcmp(cmd, "play") == 0 || strcmp(cmd, "stop") == 0)
    {
        cmdPlay(argc, argv);
    }
    else if (strcmp(cmd, "peek") == 0)
    {
        cmdPeek(argc, argv);
    }
    else if (strcmp(cmd, "hash") == 0)
    {
        cmdHash(argc, argv);
    }
    else if (strcmp(cmd, "ls") == 0)
    {
        cmdLs(argc, argv);
    }
    else if (strcmp(cmd, "get") == 0)
    {
        cmdGet(argc, argv, false);
    }
    else if (strcmp(cmd, "put") == 0)
    {
        cmdPut(argc, argv);
    }
    else if (strcmp(cmd, "rm") == 0)
    {
        cmdRm(argc, argv);
    }
    else
    {
        reply("err unknown command: %.40s", cmd);
    }
}

void Console::cmdKey(int argc, char** argv, bool pad)
{
    if (argc < 3)
    {
        reply("err usage: %s <%s> press|release|tap [n]", pad ? "pad" : "key", pad ? "0..47" : "name|id");
        return;
    }
    long id = -1;
    if (isDigit(argv[1][0]))
    {
        if (!parseInt(argv[1], &id))
        {
            id = -1;
        }
    }
    else if (!pad)
    {
        id = buttonByName(argv[1]);
    }
    if (id < 0 || id >= (pad ? kPadCount : kButtonCount))
    {
        reply("err %s unknown: %.24s", pad ? "pad" : "key", argv[1]);
        return;
    }
    long n = 1;
    if (argc >= 4 && (!parseInt(argv[3], &n) || n < 1 || n > 10))
    {
        reply("err count 1..10");
        return;
    }
    const uint8_t kind = pad ? 1 : 0;
    const bool press = strcmp(argv[2], "press") == 0;
    const bool release = strcmp(argv[2], "release") == 0;
    const bool tap = strcmp(argv[2], "tap") == 0;
    if (!press && !release && !tap)
    {
        reply("err action press|release|tap");
        return;
    }
    int freeSlots = 0;
    for (const Scheduled& s : queue_)
    {
        freeSlots += s.used == 0 ? 1 : 0;
    }
    if (freeSlots < (tap ? 2 * int(n) : 1))
    {
        reply("err busy input queue full");
        return;
    }
    if (press || release)
    {
        schedule(kind, uint8_t(id), press, now_);
    }
    else
    {
        uint32_t t = now_;
        for (long i = 0; i < n; i++)
        {
            schedule(kind, uint8_t(id), true, t);
            schedule(kind, uint8_t(id), false, t + kTapHoldMs);
            t += kTapHoldMs + kTapGapMs;
        }
    }
    pumpInput(now_);
    reply("ok %s %ld %s", pad ? "pad" : "key", id, argv[2]);
}

void Console::cmdEnc(int argc, char** argv)
{
    long delta = 0;
    if (argc < 2 || !parseInt(argv[1], &delta) || delta < -200 || delta > 200 || delta == 0)
    {
        reply("err usage: enc <-200..200>");
        return;
    }
    host_->injectEncoder(int(delta));
    lastInputMs_ = now_;
    reply("ok enc %ld", delta);
}

void Console::cmdCpu(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "reset") == 0)
    {
        host_->cpuReset();
        cpuResetMs_ = now_ | 1;
        reply("ok cpu reset");
        return;
    }
    // the profiler divides by the number of blocks since the reset: wait for a few
    if (cpuResetMs_ != 0 && now_ - cpuResetMs_ < 25)
    {
        reply("cpu none");
        return;
    }
    CpuUsage u;
    host_->cpu(u);
    reply("cpu min=%d max=%d avg=%d last=%d tmax=%d block=2902", u.minimum, u.maximum, u.average, u.last,
          u.tempMaximum);
}

void Console::cmdStatus()
{
    const int page = host_->page();
    const char* name = host_->pageName(page);
    reply("status page=%d name=%s play=%d fm=%d dirty=%d idle=%d up=%lu drop=%lu", page,
          name != nullptr ? name : "?", host_->playing() ? 1 : 0, host_->fileManagerStatus(),
          host_->projectDirty() ? 1 : 0, host_->uiIdle() ? 1 : 0, static_cast<unsigned long>(now_ / 1000),
          static_cast<unsigned long>(ring_.dropped));
}

void Console::cmdMarkers()
{
    Markers m;
    memset(&m, 0, sizeof(m));
    host_->markers(m);
    char reason[sizeof(m.safeReason)];
    copyText(reason, sizeof(reason), m.safeReason);
    for (char* p = reason; *p != 0; p++)
    {
        if (*p == ' ')
        {
            *p = '_';
        }
    }
    reply("markers srs=%04lx fault=%d pc=%08lx lr=%08lx psr=%08lx cfsr=%08lx safe=%d loading=%d reason=%s",
          static_cast<unsigned long>(m.resetStatus), m.faultValid ? 1 : 0, static_cast<unsigned long>(m.faultPc),
          static_cast<unsigned long>(m.faultLr), static_cast<unsigned long>(m.faultPsr),
          static_cast<unsigned long>(m.faultCfsr), m.safeMode ? 1 : 0, m.pluginLoading ? 1 : 0,
          reason[0] != 0 ? reason : "-");
}

void Console::cmdPlay(int argc, char** argv)
{
    if (strcmp(argv[0], "stop") == 0)
    {
        host_->stop();
        reply("ok stop");
        return;
    }
    long position = -1;
    if (argc < 2 || (strcmp(argv[1], "song") != 0 && strcmp(argv[1], "pattern") != 0) ||
        (argc >= 3 && (!parseInt(argv[2], &position) || position < 0 || position > 255)))
    {
        reply("err usage: play song|pattern [position]");
        return;
    }
    if (host_->fileManagerStatus() != 0)
    {
        reply("err play busy");
        return;
    }
    if (host_->play(strcmp(argv[1], "song") == 0, int(position)))
    {
        reply("ok play %s", argv[1]);
    }
    else
    {
        reply("err play refused");
    }
}

void Console::cmdPeek(int argc, char** argv)
{
    if (argc < 2)
    {
        reply("peek names: %s", host_->peekNames());
        return;
    }
    long value = 0;
    if (host_->peek(argv[1], &value))
    {
        reply("peek %.32s %ld", argv[1], value);
    }
    else
    {
        reply("err peek unknown: %.32s", argv[1]);
    }
}

void Console::cmdHash(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "project") == 0)
    {
        uint32_t size = 0;
        const uint32_t crc = host_->hashProject(&size);
        reply("hash project %08lx %lu", static_cast<unsigned long>(crc), static_cast<unsigned long>(size));
    }
    else if (argc >= 3 && strcmp(argv[1], "file") == 0)
    {
        cmdGet(argc - 1, argv + 1, true);
    }
    else
    {
        reply("err usage: hash project | hash file <path>");
    }
}

void Console::cmdShot(int argc, char** argv, bool crcOnly)
{
    const bool force = argc >= 2 && strcmp(argv[1], "force") == 0;
    if (host_->playing() && !force)
    {
        reply("err playing (add force to risk the sample streaming)");
        return;
    }
    if (!host_->shotBegin())
    {
        reply("err shot unavailable");
        return;
    }
    task_ = kTaskShot;
    taskStartMs_ = taskLastMs_ = now_;
    shotCrcOnly_ = crcOnly ? 1 : 0;
    shotHalf_ = 0;
    shotRow_ = 0;
    shotHaveRow_ = 0;
    shotNeedSnapshot_ = 1;
    shotCrc_ = 0;
    shotBytes_ = 0;
    memset(tileCrc_, 0, sizeof(tileCrc_));
    memset(host_->scratch() + kRowBytes / 4, 0, kRowBytes); // the row above the first is black
    if (!crcOnly)
    {
        reply("shot %d %d rle1", kShotW, kShotH);
    }
}

void Console::cmdOpen(int argc, char** argv)
{
    if (argc < 3 || badPath(argv[1]) || strlen(argv[2]) >= sizeof(name_) || argv[2][0] == 0)
    {
        reply("err usage: open <path> <name> [force]");
        return;
    }
    const bool force = argc >= 4 && strcmp(argv[3], "force") == 0;
    if (host_->playing())
    {
        reply("err open: playing, stop first");
        return;
    }
    if (host_->fileManagerStatus() != 0)
    {
        reply("err open: file manager busy");
        return;
    }
    if (host_->projectDirty() && !force)
    {
        reply("err open: the project has unsaved changes (add force to discard them)");
        return;
    }
    copyText(path_, sizeof(path_), argv[1]);
    copyText(name_, sizeof(name_), argv[2]);
    if (!host_->openProject(path_, name_))
    {
        reply("err open refused");
        return;
    }
    task_ = kTaskOpen;
    taskStartMs_ = taskLastMs_ = now_;
}

void Console::cmdLs(int argc, char** argv)
{
    const char* path = argc >= 2 ? argv[1] : "/";
    if (badPath(path))
    {
        reply("err bad path");
        return;
    }
    if (!host_->dirOpen(path))
    {
        reply("err ls: cannot open");
        return;
    }
    task_ = kTaskLs;
    taskStartMs_ = taskLastMs_ = now_;
    count_ = 0;
}

void Console::cmdGet(int argc, char** argv, bool hashOnly)
{
    if (argc < 2 || badPath(argv[1]))
    {
        reply("err usage: get <path>");
        return;
    }
    const long size = host_->fileSize(argv[1]);
    if (size < 0)
    {
        reply("err %s: no such file", hashOnly ? "hash" : "get");
        return;
    }
    file_ = host_->fileOpenRead(argv[1]);
    if (file_ == nullptr)
    {
        reply("err %s: cannot open", hashOnly ? "hash" : "get");
        return;
    }
    task_ = kTaskGet;
    taskStartMs_ = taskLastMs_ = now_;
    hashOnly_ = hashOnly ? 1 : 0;
    fileSize_ = uint32_t(size);
    fileDone_ = 0;
    fileCrc_ = 0;
    if (!hashOnly)
    {
        reply("get %lu", static_cast<unsigned long>(fileSize_));
    }
}

void Console::cmdPut(int argc, char** argv)
{
    long size = 0;
    uint32_t crc = 0;
    if (argc < 4 || badPath(argv[1]) || !parseInt(argv[2], &size) || size < 0 || size > 0x3FFFFFF ||
        !parseU32(argv[3], &crc))
    {
        reply("err usage: put <path> <size> <crc32 as 0x...>");
        return;
    }
    if (host_->playing() || host_->fileManagerStatus() != 0)
    {
        reply("err put refused while playing or loading");
        return;
    }
    copyText(path_, sizeof(path_), argv[1]);
    file_ = host_->fileOpenWrite(path_);
    if (file_ == nullptr)
    {
        reply("err put: cannot create");
        return;
    }
    task_ = kTaskPut;
    taskStartMs_ = taskLastMs_ = now_;
    fileSize_ = uint32_t(size);
    fileWantCrc_ = crc;
    fileDone_ = 0;
    fileCrc_ = 0;
    putFill_ = 0;
    reply("ready");
    if (size == 0)
    {
        putFinish();
    }
}

void Console::cmdRm(int argc, char** argv)
{
    if (argc < 2 || badPath(argv[1]) || strcmp(argv[1], "/") == 0)
    {
        reply("err usage: rm <path>");
        return;
    }
    if (host_->playing())
    {
        reply("err rm refused while playing");
        return;
    }
    if (host_->fileSize(argv[1]) < 0)
    {
        reply("err rm: no such file");
        return;
    }
    reply(host_->fileRemove(argv[1]) ? "ok rm" : "err rm failed");
}

// ---------------------------------------------------------------------------------------------------------------------
void Console::runTask(uint32_t now)
{
    switch (task_)
    {
        case kTaskNone: break;
        case kTaskShot: stepShot(now); break;
        case kTaskTexts: stepTexts(); break;
        case kTaskLs: stepLs(); break;
        case kTaskGet: stepGet(); break;
        case kTaskPut: stepPut(now); break;
        case kTaskOpen: stepOpen(now); break;
        case kTaskIdle: stepIdle(now); break;
        case kTaskReboot:
            if (now - taskStartMs_ >= kRebootDelayMs)
            {
                txPump();
                host_->resetMcu();
                task_ = kTaskNone;
            }
            break;
    }
}

// Idle: the injected input has been handled (queues empty, 100 ms since the last event), the file manager is not working and
// the display has nothing queued. A page that never stops redrawing (song, playing) is idle after 600 ms without input.
void Console::stepIdle(uint32_t now)
{
    bool pending = host_->inputPending();
    for (const Scheduled& s : queue_)
    {
        pending = pending || s.used != 0;
    }
    const uint32_t sinceInput = now - lastInputMs_;
    const bool settled = !pending && host_->fileManagerStatus() == 0 && sinceInput >= 100 &&
                         (host_->uiIdle() || sinceInput >= 600);
    if (settled)
    {
        if (idleSinceMs_ == 0)
        {
            idleSinceMs_ = now | 1;
        }
        else if (now - idleSinceMs_ >= kIdleSettleMs)
        {
            reply("idle");
            finishTask();
        }
    }
    else
    {
        idleSinceMs_ = 0;
        if (now - taskStartMs_ > kIdleTimeoutMs)
        {
            reply("err idle timeout");
            finishTask();
        }
    }
}

void Console::stepOpen(uint32_t now)
{
    int code = 0;
    const int state = host_->openState(&code);
    if (state == 1)
    {
        reply("ok open");
        finishTask();
    }
    else if (state == 2)
    {
        reply("err open fm=%d", code);
        finishTask();
    }
    else if (now - taskStartMs_ > kOpenTimeoutMs)
    {
        reply("err open timeout");
        finishTask();
    }
}

void Console::stepTexts()
{
    TextItem item;
    while (needSpace(200))
    {
        if (!host_->nextText(&cursor_, item))
        {
            reply("ok texts %d", count_);
            finishTask();
            return;
        }
        reply("t %d %d %d %d %d %s", item.id, item.x, item.y, item.w, item.h, item.text);
        count_++;
        taskLastMs_ = now_;
    }
    if (now_ - taskLastMs_ > kTaskTimeoutMs)
    {
        abortTask();
    }
}

void Console::stepLs()
{
    DirEntry entry;
    while (needSpace(200))
    {
        if (!host_->dirNext(entry))
        {
            host_->dirClose();
            reply("ok ls %d", count_);
            finishTask();
            return;
        }
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
        {
            continue;
        }
        reply("%c %lu %s", entry.isDir ? 'd' : 'f', static_cast<unsigned long>(entry.size), entry.name);
        count_++;
        taskLastMs_ = now_;
    }
    if (now_ - taskLastMs_ > kTaskTimeoutMs)
    {
        abortTask();
    }
}

void Console::stepGet()
{
    uint8_t* chunk = reinterpret_cast<uint8_t*>(host_->scratch());
    for (int i = 0; i < 16; i++)
    {
        if (!hashOnly_ && !needSpace(kChunk + 64))
        {
            if (now_ - taskLastMs_ > kTaskTimeoutMs)
            {
                abortTask();
            }
            return;
        }
        const int n = host_->fileRead(file_, chunk, kChunk);
        if (n < 0)
        {
            host_->fileClose(file_);
            file_ = nullptr;
            reply("err %s read failed", hashOnly_ ? "hash" : "get");
            finishTask();
            return;
        }
        if (n == 0)
        {
            host_->fileClose(file_);
            file_ = nullptr;
            if (hashOnly_)
            {
                reply("hash file %08lx %lu", static_cast<unsigned long>(fileCrc_),
                      static_cast<unsigned long>(fileDone_));
            }
            else
            {
                reply("end %lu %08lx", static_cast<unsigned long>(fileDone_), static_cast<unsigned long>(fileCrc_));
            }
            finishTask();
            return;
        }
        fileCrc_ = crc32(fileCrc_, chunk, size_t(n));
        fileDone_ += uint32_t(n);
        taskLastMs_ = now_;
        if (!hashOnly_)
        {
            char header[24];
            format(header, sizeof(header), "@blk %d", n);
            pushBlock(header, chunk, size_t(n));
        }
    }
}

void Console::putByte(uint8_t b)
{
    uint8_t* chunk = reinterpret_cast<uint8_t*>(host_->scratch());
    chunk[putFill_++] = b;
    if (putFill_ == kChunk || fileDone_ + putFill_ == fileSize_)
    {
        const int want = putFill_;
        const int wrote = host_->fileWrite(file_, chunk, putFill_);
        fileCrc_ = crc32(fileCrc_, chunk, putFill_);
        fileDone_ += putFill_;
        putFill_ = 0;
        if (wrote != want)
        {
            host_->fileClose(file_);
            host_->fileRemove(path_);
            file_ = nullptr;
            finishTask();
            reply("err put write failed");
            return;
        }
        if (fileDone_ == fileSize_)
        {
            putFinish();
        }
    }
}

void Console::putFinish()
{
    host_->fileClose(file_);
    file_ = nullptr;
    finishTask();
    if (fileCrc_ == fileWantCrc_)
    {
        reply("ok put %lu", static_cast<unsigned long>(fileDone_));
    }
    else
    {
        host_->fileRemove(path_);
        reply("err put crc %08lx", static_cast<unsigned long>(fileCrc_));
    }
}

void Console::stepPut(uint32_t now)
{
    if (now - taskLastMs_ > kTaskTimeoutMs)
    {
        abortTask();
        reply("err put timeout");
    }
}

void Console::stepShot(uint32_t now)
{
    uint32_t* const words = host_->scratch();
    uint32_t* const cur = words;
    uint32_t* const prev = words + kRowBytes / 4;
    uint8_t* const encoded = reinterpret_cast<uint8_t*>(words + 2 * (kRowBytes / 4));
    uint8_t* const curBytes = reinterpret_cast<uint8_t*>(cur);
    constexpr int kWidth = kRowBytes / 4;

    for (int budget = 6; budget > 0; budget--)
    {
        if (shotHalf_ >= 2)
        {
            host_->shotEnd();
            if (shotCrcOnly_ != 0)
            {
                reply("ok shotcrc");
            }
            else
            {
                reply("ok shot crc=%08lx bytes=%lu rows=%d", static_cast<unsigned long>(shotCrc_),
                      static_cast<unsigned long>(shotBytes_), kShotH);
            }
            finishTask();
            return;
        }
        if (shotNeedSnapshot_ != 0)
        {
            host_->shotSnapshot(shotHalf_);
            shotNeedSnapshot_ = 0;
            shotRow_ = 0;
            shotHaveRow_ = 0;
            budget = 1;
        }
        if (shotHaveRow_ == 0)
        {
            host_->shotRow(shotHalf_, shotRow_, curBytes);
            shotHaveRow_ = 1;
        }
        const int y = shotHalf_ * kShotHalfRows + shotRow_;
        if (shotCrcOnly_ != 0)
        {
            const bool tileRowDone = (y % kTileH) == kTileH - 1;
            if (tileRowDone && !needSpace(160))
            {
                break;
            }
            uint8_t packed[kTileW * 3];
            for (int tx = 0; tx < kTilesX; tx++)
            {
                for (int k = 0; k < kTileW; k++)
                {
                    const uint32_t w = cur[tx * kTileW + k];
                    packed[3 * k] = uint8_t(w);
                    packed[3 * k + 1] = uint8_t(w >> 8);
                    packed[3 * k + 2] = uint8_t(w >> 16);
                }
                tileCrc_[tx] = crc32(tileCrc_[tx], packed, sizeof(packed));
            }
            if (tileRowDone)
            {
                reply("shotcrc %d %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", y / kTileH,
                      static_cast<unsigned long>(tileCrc_[0]), static_cast<unsigned long>(tileCrc_[1]),
                      static_cast<unsigned long>(tileCrc_[2]), static_cast<unsigned long>(tileCrc_[3]),
                      static_cast<unsigned long>(tileCrc_[4]), static_cast<unsigned long>(tileCrc_[5]),
                      static_cast<unsigned long>(tileCrc_[6]), static_cast<unsigned long>(tileCrc_[7]));
                memset(tileCrc_, 0, sizeof(tileCrc_));
            }
        }
        else
        {
            // a row equal to the one above is a header with length 0 and no body
            const size_t need = memcmp(cur, prev, kRowBytes) == 0 ? 0 : rleSize(cur, prev, kWidth);
            if (!needSpace(uint32_t(need + 48)))
            {
                break;
            }
            if (need != 0)
            {
                rleEncode(encoded, cur, prev, kWidth);
            }
            char header[32];
            format(header, sizeof(header), "@row %d %u", y, unsigned(need));
            pushBlock(header, encoded, need);
            uint8_t packed[kTileW * 3];
            for (int x = 0; x < kWidth; x += kTileW)
            {
                for (int k = 0; k < kTileW; k++)
                {
                    const uint32_t w = cur[x + k];
                    packed[3 * k] = uint8_t(w);
                    packed[3 * k + 1] = uint8_t(w >> 8);
                    packed[3 * k + 2] = uint8_t(w >> 16);
                }
                shotCrc_ = crc32(shotCrc_, packed, sizeof(packed));
            }
            shotBytes_ += uint32_t(need);
        }
        memcpy(prev, cur, kRowBytes);
        shotHaveRow_ = 0;
        taskLastMs_ = now;
        if (++shotRow_ >= kShotHalfRows)
        {
            shotHalf_++;
            shotNeedSnapshot_ = 1;
        }
    }
    if (now - taskLastMs_ > kTaskTimeoutMs)
    {
        abortTask();
        reply("err shot timeout");
    }
}
} // namespace usbtest

#endif /* ENABLE_DEV_CONSOLE */
