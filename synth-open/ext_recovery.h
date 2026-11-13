// Recovery console of the image (FEATURE_EXT_HOOKS): the few lines the unit answers on the USB serial port while no extension module
// runs (refused, crashed, none, Shift held, or still inside the key window). The full console is the module,
// so before this a refused module left the port silent and the unit could only be recovered by hand with the card. Now `ping`
// answers like the console does (the same first line, so the console ping works), `module` says why the module is not running and
// `reset` restarts the unit. Everything else gets one @err line, never silence. Pure text in and out, tools/ext_recovery_test.cpp
// runs this code on the Mac, the image calls it from ext_update (ext_module.cpp).
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ext
{
constexpr int kRecoveryProto = 1;   // the console's kProtocol (the console, UsbTestCore.h)

// One input line, assembled byte by byte. A line longer than the buffer is cut, and still ends at its newline.
struct RecoveryLine
{
    char buf[48];
    uint8_t n = 0;
    // True when c ended a line, which is then in buf.
    bool feed(int c)
    {
        if (c == '\r') return false;
        if (c == '\n')
        {
            buf[n] = 0;
            n = 0;
            return true;
        }
        if (n < sizeof(buf) - 1) buf[n++] = char(c);
        return false;
    }
};

// The reply to one line, '\n' ended, into out. Returns its length, 0 for a blank line (no reply). *restart is set by `reset`.
// build = the image's build id, status = what the loader says (ext::statusText or a boot phase), detail = more of the same
// (" | ..." or empty).
inline int recoveryReply(const char* line, const char* build, const char* status, const char* detail, char* out, int cap,
                         bool* restart)
{
    while (*line == ' ') line++;
    if (!*line) return 0;
    char cmd[24];
    int k = 0;
    while (line[k] && line[k] != ' ' && k < int(sizeof(cmd)) - 1)
    {
        cmd[k] = line[k];
        k++;
    }
    cmd[k] = 0;
    int n;
    if (strcmp(cmd, "ping") == 0)
        n = snprintf(out, cap, "@tracker-dev original %s proto=%d recovery module: %s\n", build, kRecoveryProto, status);
    else if (strcmp(cmd, "module") == 0)
        n = snprintf(out, cap, "@ok module %s%s\n", status, detail);
    else if (strcmp(cmd, "help") == 0)
        n = snprintf(out, cap, "@ok recovery console, no module runs: ping help module reset\n");
    else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "reboot") == 0)
    {
        *restart = true;
        n = snprintf(out, cap, "@ok reset pending\n");
    }
    else
        n = snprintf(out, cap, "@err recovery console, no module runs (%s): %s\n", status, cmd);
    if (n < 0) return 0;
    if (n >= cap)
    {   // cut, still one line
        n = cap - 1;
        out[n - 1] = '\n';
    }
    return n;
}
}   // namespace ext
