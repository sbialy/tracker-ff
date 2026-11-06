#ifndef __LOG_H__
#define __LOG_H__

#include "log.h"
#include "DevConsole.h"
#include <stdio.h>

void _debuglog(const char* msg)
{
    DevConsole_Printf("%s\n", msg);
}

void _debuglogf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    DevConsole_Vprintf(format, args);
    va_end(args);
    DevConsole_Printf("\n");
}

#endif
