/**
 * selftest.h - forecast + Pacific-time test vectors, runnable on the board
 * (send "SELFTEST" over USB). The same vectors live in tools/test_reference.py.
 */
#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool selftest_run(void (*reply)(const char *line));

#ifdef __cplusplus
}
#endif

#endif /* SELFTEST_H */
