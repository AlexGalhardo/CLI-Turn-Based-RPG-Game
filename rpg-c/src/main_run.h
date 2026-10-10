// run(): flags → simulator or TUI. It writes to string buffers instead of stdout/stderr so tests can call it.
#ifndef RPG_MAIN_RUN_H
#define RPG_MAIN_RUN_H

#include "domain/base.h"

// `args` excludes the program name. Returns the process exit code.
int run(int arg_count, const char *const *args, StrBuf *out, StrBuf *err);

#endif
