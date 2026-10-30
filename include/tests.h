// Boot-time self tests. `make test` boots the kernel once per test, passing the
// test's name on the kernel command line as test=<name>.

#ifndef TESTS_H
#define TESTS_H

#include <stdbool.h>

#define TEST_EXIT_DONE  0x10
#define TEST_EXIT_PANIC 0x11

void tests_init(const char *command_line);
bool test_mode_active();
void tests_run();

#endif
