/*
 * Tiny helpers shared by the FreeRTOS simulator demos.
 *
 *   demo_printf(fmt, ...)      printf that is safe to call from tasks
 *   demo_log(fmt, ...)         same, prefixed with the current tick count
 *   DEMO_CHECK(cond, fmt, ...) record an expectation; prints on failure
 *   demo_report()              print "RESULT: PASS/FAIL" and return exit code
 *
 * Why a wrapper around printf? On the POSIX port each task is a pthread and a
 * context switch happens inside the SIGALRM tick handler. If a task were
 * preempted while holding glibc's stdout lock, the next task to call printf
 * would block on a *host* mutex that FreeRTOS knows nothing about and the
 * simulation would hang. Printing inside a critical section (which masks the
 * tick signal on this port) avoids that.
 */
#ifndef DEMO_UTIL_H
#define DEMO_UTIL_H

#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

static volatile int demo_failures;
static volatile int demo_checks;

static inline int demo_in_scheduler(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

__attribute__((format(printf, 1, 0), unused))
static void demo_vprintf(const char *fmt, va_list ap)
{
    int crit = demo_in_scheduler();
    if (crit) taskENTER_CRITICAL();
    vprintf(fmt, ap);
    fflush(stdout);
    if (crit) taskEXIT_CRITICAL();
}

__attribute__((format(printf, 1, 2), unused))
static void demo_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    demo_vprintf(fmt, ap);
    va_end(ap);
}

__attribute__((format(printf, 1, 2), unused))
static void demo_log(const char *fmt, ...)
{
    va_list ap;
    int crit = demo_in_scheduler();
    if (crit) taskENTER_CRITICAL();
    printf("[%6lu] ", (unsigned long)xTaskGetTickCount());
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (crit) taskEXIT_CRITICAL();
}

__attribute__((format(printf, 3, 4), unused))
static void demo_check_failed(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    int crit = demo_in_scheduler();
    if (crit) taskENTER_CRITICAL();
    demo_failures++;
    printf("CHECK FAILED (%s:%d): ", file, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (crit) taskEXIT_CRITICAL();
}

#define DEMO_CHECK(cond, ...)                                              \
    do {                                                                   \
        demo_checks++;                                                     \
        if (!(cond)) demo_check_failed(__FILE__, __LINE__, __VA_ARGS__);   \
    } while (0)

__attribute__((unused))
static int demo_report(void)
{
    int ok = (demo_failures == 0 && demo_checks > 0);
    printf("checks: %d, failures: %d\n", demo_checks, demo_failures);
    printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    fflush(stdout);
    return ok ? 0 : 1;
}

#endif /* DEMO_UTIL_H */
