#ifndef NUMOS_PROGRAM_VERSION_H
#define NUMOS_PROGRAM_VERSION_H

#include "syscalls.h"

#ifndef NUMOS_VERSION_STRING
#define NUMOS_VERSION_STRING "v0.0.0"
#endif

static size_t numos_program_version_strlen(const char *text) {
    size_t len = 0;
    if (!text) return 0;
    while (text[len]) len++;
    return len;
}

static int numos_program_version_streq(const char *lhs, const char *rhs) {
    if (!lhs || !rhs) return 0;
    while (*lhs && *rhs) {
        if (*lhs != *rhs) return 0;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static int numos_is_version_flag(const char *arg) {
    if (!arg) return 0;
    return numos_program_version_streq(arg, "-v") ||
           numos_program_version_streq(arg, "--version");
}

static void numos_print_program_version(const char *program_name) {
    static const char fallback_name[] = "program";

    if (!program_name || !program_name[0]) program_name = fallback_name;
    sys_write(FD_STDOUT, program_name, numos_program_version_strlen(program_name));
    sys_write(FD_STDOUT, " ", 1);
    sys_write(FD_STDOUT, NUMOS_VERSION_STRING,
              numos_program_version_strlen(NUMOS_VERSION_STRING));
    sys_write(FD_STDOUT, "\n", 1);
}

#endif
