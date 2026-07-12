#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

static class DebugStateClass
{
public:
    DebugStateClass()
        : File(nullptr)
    {
        /* Windows doesn't attach to the console by default so printing to stderr does nothing. */
#if defined(_WIN32) && _WIN32_WINNT >= 0x0501
        /* Attach to the console that started us if any */
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            /* We attached successfully, lets redirect IO to the consoles handles if not already redirected */
            if (_fileno(stdout) == -2 || _get_osfhandle(fileno(stdout)) == -2) {
                freopen("CONOUT$", "w", stdout);
            }

            if (_fileno(stderr) == -2 || _get_osfhandle(fileno(stderr)) == -2) {
                freopen("CONOUT$", "w", stderr);
            }

            if (_fileno(stdin) == -2 || _get_osfhandle(fileno(stdin)) == -2) {
                freopen("CONIN$", "r", stdin);
            }
        }
#endif
    }

    ~DebugStateClass()
    {
        if (File != nullptr) {
            fclose(File);
        }

        File = nullptr;
    }

    FILE* File;
} DebugState;

/*
 * WO-009 'logtoggle:' -- runtime log on/off. Checked ONCE (function-local static),
 * so the runtime cost is a single boolean, never a per-write file probe. If the
 * literal lowercase flag file $HOME/Documents/vclog_off.txt exists at first check,
 * the common/ FILE loggers are suppressed. The 'logtoggle:' string below is the
 * greppable content-verification checkpoint; it is emitted once to stderr (which is
 * suppressed on sideloaded iOS) purely so the literal lands in BOTH game binaries.
 * Placed in common/ so Tiberian Dawn and Red Alert both inherit the switch.
 */
bool Vanilla_Log_Suppressed(void)
{
    static int suppressed = -1;
    if (suppressed < 0) {
        const char* home = getenv("HOME");
        char path[1200];
        snprintf(path, sizeof(path), "%s/Documents/vclog_off.txt", home != nullptr ? home : ".");
        FILE* f = fopen(path, "r");
        suppressed = (f != nullptr) ? 1 : 0;
        if (f != nullptr) {
            fclose(f);
        }
        fprintf(stderr, "logtoggle: common loggers %s\n", suppressed ? "SUPPRESSED (vclog_off.txt present)" : "active");
        fflush(stderr);
    }
    return suppressed == 1;
}

/**
 * Main log function, intended to be used from behind macros that pass in file and line details.
 */
void Debug_String_Log(unsigned level, const char* file, int line, const char* fmt, ...)
{
    static const char* levels[] = {"NONE", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    assert(level <= 6);

    /* WO-009: runtime kill-switch for the common/ engine logger (vcengine.txt + stderr). */
    if (Vanilla_Log_Suppressed()) {
        return;
    }

    /* If we have a file pointer set we are logging to a file */
    if (DebugState.File != nullptr) {
        va_list args;
        fprintf(DebugState.File, "%-5s %s:%d: ", levels[level], file, line);
        va_start(args, fmt);
        vfprintf(DebugState.File, fmt, args);
        fprintf(DebugState.File, "\n");
        va_end(args);
        fflush(DebugState.File);
    }

    /* Don't print file and line numbers to stderr to avoid clogging it up with too much info */
    va_list args;
    fprintf(stderr, "%-5s: ", levels[level]);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    fflush(stderr);
}

void Debug_String_File(const char* file)
{
    if (DebugState.File != nullptr) {
        fclose(DebugState.File);
    }

    DebugState.File = fopen(file, "w");
}
