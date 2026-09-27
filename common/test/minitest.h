/*
 * minitest.h - a deliberately tiny single-header unit test helper.
 *
 *   TEST(name) { CHECK(expr); CHECK_EQ_INT(a, b); CHECK_NEAR(a, b, eps); }
 *   int main(void) { RUN(name); ...; return minitest_summary(); }
 */
#ifndef MINITEST_H
#define MINITEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int mt_failures, mt_checks, mt_tests;

#define TEST(name) static void name(void)
#define RUN(name) do { mt_tests++; printf("[ RUN  ] %s\n", #name); name(); } while (0)

#define CHECK(expr) do { mt_checks++; if (!(expr)) { mt_failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define CHECK_EQ_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    mt_checks++; if (_a != _b) { mt_failures++; \
    printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_NEAR(a, b, eps) do { double _a = (double)(a), _b = (double)(b); \
    mt_checks++; if (fabs(_a - _b) > (eps)) { mt_failures++; \
    printf("  FAIL %s:%d: %s ~= %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); mt_checks++; \
    if (strcmp(_a, _b) != 0) { mt_failures++; \
    printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); } } while (0)

static inline int minitest_summary(void)
{
    printf("%d tests, %d checks, %d failures\n", mt_tests, mt_checks, mt_failures);
    return mt_failures ? 1 : 0;
}

#endif /* MINITEST_H */
