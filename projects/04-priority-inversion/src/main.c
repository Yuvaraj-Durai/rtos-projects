/*
 * 04 - Priority inversion and the priority-inheritance fix
 *
 * Classic three-task scenario (Mars Pathfinder, 1997):
 *
 *   t=0   LOW  (prio 1) takes the shared lock and starts a 30-tick job
 *   t=5   HIGH (prio 3) wakes and blocks on the same lock
 *   t=10  MED  (prio 2) wakes and runs a 100-tick CPU-bound job that does
 *                       not need the lock
 *
 * Run 1 uses a *binary semaphore* as the lock. It has no owner, so the
 * kernel cannot help: MED preempts LOW, and HIGH ends up waiting for MED
 * too -> unbounded priority inversion (HIGH blocked ~125 ticks).
 *
 * Run 2 uses a *mutex*. When HIGH blocks, FreeRTOS temporarily raises LOW
 * to priority 3 (priority inheritance), so MED cannot preempt it; HIGH waits
 * only for the remainder of LOW's critical section (~25 ticks).
 *
 * "Work" is measured in ticks during which the task actually held the CPU,
 * so a preempted task makes no progress - just like on a real MCU.
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "demo_util.h"

#define PRIO_LOW   1
#define PRIO_MED   2
#define PRIO_HIGH  3
#define PRIO_CTRL  5

#define LOW_WORK    30
#define MED_WORK   100
#define HIGH_WAKE    5
#define MED_WAKE    10

static SemaphoreHandle_t lock;
static TaskHandle_t controller, low_h;
static TickType_t t_start;
static volatile TickType_t high_blocked_for, high_done_at, med_done_at, low_done_at;
static volatile UBaseType_t low_prio_while_high_waits;
static volatile int done_count;

/* Burn CPU for n ticks of *our own* execution time. A jump of more than one
 * tick means we were preempted, and that time does not count. */
static void cpu_work(int n)
{
    TickType_t last = xTaskGetTickCount();
    int done = 0;
    while (done < n) {
        TickType_t now = xTaskGetTickCount();
        if (now != last) {
            if (now - last == 1) done++;
            last = now;
        }
    }
}

static void finished(void)
{
    taskENTER_CRITICAL();
    done_count++;
    taskEXIT_CRITICAL();
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void low_task(void *arg)
{
    (void)arg;
    xSemaphoreTake(lock, portMAX_DELAY);
    cpu_work(LOW_WORK);
    low_done_at = xTaskGetTickCount() - t_start; /* time the lock is released */
    xSemaphoreGive(lock);
    finished();
}

static void med_task(void *arg)
{
    (void)arg;
    vTaskDelayUntil(&(TickType_t){ t_start }, MED_WAKE);
    cpu_work(MED_WORK);
    med_done_at = xTaskGetTickCount() - t_start;
    finished();
}

static void high_task(void *arg)
{
    (void)arg;
    vTaskDelayUntil(&(TickType_t){ t_start }, HIGH_WAKE);
    TickType_t t0 = xTaskGetTickCount();
    xSemaphoreTake(lock, portMAX_DELAY);
    high_blocked_for = xTaskGetTickCount() - t0;
    xSemaphoreGive(lock);
    high_done_at = xTaskGetTickCount() - t_start;
    finished();
}

/* Samples LOW's priority shortly after HIGH has blocked on the lock. */
static void probe_task(void *arg)
{
    (void)arg;
    vTaskDelayUntil(&(TickType_t){ t_start }, HIGH_WAKE + 2);
    low_prio_while_high_waits = uxTaskPriorityGet(low_h);
    vTaskSuspend(NULL);
}

static void run_scenario(int use_mutex)
{
    TaskHandle_t m, h, p;
    lock = use_mutex ? xSemaphoreCreateMutex() : xSemaphoreCreateBinary();
    if (!use_mutex) xSemaphoreGive(lock); /* binary semaphores start empty */
    done_count = 0;
    t_start = xTaskGetTickCount();
    xTaskCreate(low_task, "Low", configMINIMAL_STACK_SIZE, NULL, PRIO_LOW, &low_h);
    xTaskCreate(med_task, "Med", configMINIMAL_STACK_SIZE, NULL, PRIO_MED, &m);
    xTaskCreate(high_task, "High", configMINIMAL_STACK_SIZE, NULL, PRIO_HIGH, &h);
    xTaskCreate(probe_task, "Probe", configMINIMAL_STACK_SIZE, NULL, PRIO_CTRL - 1, &p);
    for (int i = 0; i < 3; i++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    vTaskDelete(low_h); vTaskDelete(m); vTaskDelete(h); vTaskDelete(p);
    vSemaphoreDelete(lock);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();

    run_scenario(0);
    TickType_t sem_block = high_blocked_for;
    demo_log("binary semaphore: HIGH blocked %3lu ticks | LOW prio while HIGH waits=%lu | "
             "MED done@%lu, LOW unlock@%lu, HIGH done@%lu",
             (unsigned long)sem_block, (unsigned long)low_prio_while_high_waits,
             (unsigned long)med_done_at, (unsigned long)low_done_at, (unsigned long)high_done_at);
    DEMO_CHECK(low_prio_while_high_waits == PRIO_LOW, "semaphore: no inheritance expected");
    DEMO_CHECK(sem_block >= MED_WORK, "semaphore: HIGH should be delayed by MED's whole job (%lu)",
               (unsigned long)sem_block);
    DEMO_CHECK(med_done_at < high_done_at, "semaphore: MED finishes before HIGH (inversion)");

    run_scenario(1);
    TickType_t mtx_block = high_blocked_for;
    demo_log("mutex (inherit):  HIGH blocked %3lu ticks | LOW prio while HIGH waits=%lu | "
             "LOW unlock@%lu, HIGH done@%lu, MED done@%lu",
             (unsigned long)mtx_block, (unsigned long)low_prio_while_high_waits,
             (unsigned long)low_done_at, (unsigned long)high_done_at, (unsigned long)med_done_at);
    DEMO_CHECK(low_prio_while_high_waits == PRIO_HIGH, "mutex: LOW should inherit HIGH's priority");
    DEMO_CHECK(mtx_block <= LOW_WORK, "mutex: HIGH blocked at most LOW's critical section (%lu)",
               (unsigned long)mtx_block);
    DEMO_CHECK(high_done_at < med_done_at, "mutex: HIGH finishes before MED");

    demo_log("priority inheritance cut HIGH's blocking time from %lu to %lu ticks",
             (unsigned long)sem_block, (unsigned long)mtx_block);
    vTaskEndScheduler();
}

int main(void)
{
    printf("04-priority-inversion (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, PRIO_CTRL, NULL);
    vTaskStartScheduler();
    return demo_report();
}
