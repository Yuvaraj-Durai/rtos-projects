/*
 * 01 - Task creation & priorities (FreeRTOS, POSIX simulator port)
 *
 * A controller task runs five experiments and records an execution trace.
 * Every experiment ends with DEMO_CHECKs so the run is self-verifying:
 *
 *  1. Creation order vs. priority: tasks created Low, Mid, High while the
 *     controller holds the CPU start running High -> Mid -> Low.
 *  2. Immediate preemption: creating a task with a higher priority than the
 *     creator makes it run before xTaskCreate() returns.
 *  3. Time slicing: two equal-priority CPU-bound tasks share the CPU.
 *  4. vTaskPrioritySet(): raising a ready task above the caller preempts.
 *  5. vTaskSuspend()/vTaskResume()/vTaskDelete() lifecycle and eTaskGetState().
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "demo_util.h"

#define PRIO_CONTROLLER (tskIDLE_PRIORITY + 5)
#define MAX_TRACE 64

static char trace[MAX_TRACE];
static volatile int trace_len;

static void trace_add(char c)
{
    taskENTER_CRITICAL();
    if (trace_len < MAX_TRACE - 1) {
        trace[trace_len++] = c;
        trace[trace_len] = '\0';
    }
    taskEXIT_CRITICAL();
}

static void trace_reset(void)
{
    taskENTER_CRITICAL();
    trace_len = 0;
    trace[0] = '\0';
    taskEXIT_CRITICAL();
}

/* Records its tag once, then blocks forever (until deleted). */
static void one_shot_task(void *arg)
{
    trace_add((char)(uintptr_t)arg);
    vTaskSuspend(NULL);
}

/* ---- experiment 3: time slicing ----------------------------------------- */
static volatile uint32_t spin_count[2];
static volatile int spin_stop;

static void spinner_task(void *arg)
{
    int id = (int)(uintptr_t)arg;
    char tag = id ? 'b' : 'a';
    while (!spin_stop) {
        spin_count[id]++;
        /* Append our tag only when we (re)gain the CPU, so the trace length
         * equals the number of time slices observed. */
        taskENTER_CRITICAL();
        if ((trace_len == 0 || trace[trace_len - 1] != tag) && trace_len < MAX_TRACE - 1) {
            trace[trace_len++] = tag;
            trace[trace_len] = '\0';
        }
        taskEXIT_CRITICAL();
    }
    vTaskSuspend(NULL);
}

/* ---- experiment 4: priority change -------------------------------------- */
static void bumped_task(void *arg)
{
    (void)arg;
    for (;;) {
        trace_add('B');
        vTaskSuspend(NULL);
    }
}

static void controller_task(void *arg)
{
    TaskHandle_t h[3];
    (void)arg;

    /* 1. Create L, M, H without yielding: nothing runs until we block. */
    demo_log("exp1: create Low(1), Mid(2), High(3) from controller(5)");
    trace_reset();
    xTaskCreate(one_shot_task, "Low", configMINIMAL_STACK_SIZE, (void *)'L', 1, &h[0]);
    xTaskCreate(one_shot_task, "Mid", configMINIMAL_STACK_SIZE, (void *)'M', 2, &h[1]);
    xTaskCreate(one_shot_task, "High", configMINIMAL_STACK_SIZE, (void *)'H', 3, &h[2]);
    DEMO_CHECK(trace_len == 0, "lower-priority tasks must not run yet (trace=%s)", trace);
    vTaskDelay(pdMS_TO_TICKS(10));
    demo_log("exp1: trace = %s", trace);
    DEMO_CHECK(strcmp(trace, "HML") == 0, "expected HML, got %s", trace);
    for (int i = 0; i < 3; i++) vTaskDelete(h[i]);

    /* 2. Create a task with higher priority than ourselves. */
    trace_reset();
    trace_add('c');
    TaskHandle_t hp;
    xTaskCreate(one_shot_task, "Urgent", configMINIMAL_STACK_SIZE, (void *)'U',
                PRIO_CONTROLLER + 1, &hp);
    trace_add('c');
    demo_log("exp2: trace = %s (U ran inside xTaskCreate)", trace);
    DEMO_CHECK(strcmp(trace, "cUc") == 0, "expected cUc, got %s", trace);
    vTaskDelete(hp);

    /* 3. Two CPU-bound tasks at the same priority. */
    trace_reset();
    spin_stop = 0;
    TaskHandle_t sa, sb;
    xTaskCreate(spinner_task, "SpinA", configMINIMAL_STACK_SIZE, (void *)0, 2, &sa);
    xTaskCreate(spinner_task, "SpinB", configMINIMAL_STACK_SIZE, (void *)1, 2, &sb);
    vTaskDelay(pdMS_TO_TICKS(50));
    spin_stop = 1;
    vTaskDelay(pdMS_TO_TICKS(5));
    int switches = trace_len;
    demo_log("exp3: A=%lu B=%lu iterations, %d slices observed",
             (unsigned long)spin_count[0], (unsigned long)spin_count[1], switches);
    DEMO_CHECK(spin_count[0] > 0 && spin_count[1] > 0, "both spinners must run");
    DEMO_CHECK(switches >= 4, "expected round-robin time slices, saw %d", switches);
    vTaskDelete(sa);
    vTaskDelete(sb);

    /* 4. Priority change of a ready task. */
    trace_reset();
    TaskHandle_t hb;
    xTaskCreate(bumped_task, "Bumped", configMINIMAL_STACK_SIZE, NULL, 1, &hb);
    trace_add('c');
    DEMO_CHECK(uxTaskPriorityGet(hb) == 1, "initial priority 1");
    vTaskPrioritySet(hb, PRIO_CONTROLLER + 1); /* preempts us right here */
    trace_add('c');
    demo_log("exp4: trace = %s", trace);
    DEMO_CHECK(strcmp(trace, "cBc") == 0, "expected cBc, got %s", trace);
    DEMO_CHECK(uxTaskPriorityGet(hb) == PRIO_CONTROLLER + 1, "priority updated");

    /* 5. Lifecycle: bumped task is now suspended by itself. */
    DEMO_CHECK(eTaskGetState(hb) == eSuspended, "state should be eSuspended");
    vTaskResume(hb); /* higher priority -> runs immediately, suspends again */
    DEMO_CHECK(strcmp(trace, "cBcB") == 0, "expected cBcB, got %s", trace);
    vTaskPrioritySet(hb, 1);
    vTaskResume(hb);
    DEMO_CHECK(eTaskGetState(hb) == eReady, "resumed low-priority task is ready");
    vTaskDelete(hb);
    demo_log("exp5: suspend/resume/delete ok, tasks now: %lu",
             (unsigned long)uxTaskGetNumberOfTasks());

    vTaskEndScheduler();
}

int main(void)
{
    printf("01-task-priorities: FreeRTOS %s on the POSIX simulator\n", tskKERNEL_VERSION_NUMBER);
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, PRIO_CONTROLLER, NULL);
    vTaskStartScheduler();
    return demo_report();
}
