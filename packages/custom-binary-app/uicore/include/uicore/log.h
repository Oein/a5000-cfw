// Core logging routes to the platform Log interface. Set once in App::init.
#pragma once
namespace uicore {
class Log;
void log_set(Log *l);
void logf(const char *fmt, ...);   // printf-style, newline handled by the sink
}
