/*
 * Default FreeRTOS hook implementations shared by every simulator demo.
 * All are weak so an individual project can override them.
 */
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

__attribute__((weak)) void vAssertCalled(const char *file, unsigned long line)
{
    fprintf(stderr, "ASSERT FAILED: %s:%lu\n", file, line);
    fflush(NULL);
    exit(2);
}

__attribute__((weak)) void vApplicationMallocFailedHook(void)
{
    fprintf(stderr, "ASSERT FAILED: FreeRTOS heap exhausted\n");
    fflush(NULL);
    exit(3);
}

__attribute__((weak)) void vApplicationTickHook(void)
{
}
