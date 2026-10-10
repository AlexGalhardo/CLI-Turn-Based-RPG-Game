// The C implementation of the CLI Turn-Based RPG. Everything except printing lives in run() (main_run.c).
#include "main_run.h"

#include <stdio.h>

int main(int argc, char **argv) {
	StrBuf out = {0};
	StrBuf err = {0};
	int code = run(argc - 1, (const char *const *)(argv + 1), &out, &err);
	if (out.length > 0) {
		fputs(out.data, stdout);
	}
	if (err.length > 0) {
		fputs(err.data, stderr);
	}
	sb_free(&out);
	sb_free(&err);
	return code;
}
