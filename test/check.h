/* The host tests' check counter: CHECK(cond) records a failure with its line, check_report() prints the totals. */
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static int check_report(const char *name)
{
    printf("%s: %d checks, %d failures\n", name, checks, fails);
    return fails ? 1 : 0;
}

#endif
