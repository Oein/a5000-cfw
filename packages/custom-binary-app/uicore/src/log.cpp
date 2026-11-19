#include "uicore/log.h"
#include "uicore/platform.h"
#include <stdio.h>
#include <stdarg.h>

namespace uicore {

static Log *g_log = 0;
void log_set(Log *l) { g_log = l; }

void logf(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_log) g_log->line(buf);
}

} // namespace uicore
