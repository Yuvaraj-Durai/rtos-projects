/*
 * 06 - Software timers
 *
 * All timer callbacks execute in the FreeRTOS timer service (daemon) task,
 * so they must be short and must never block.
 *
 *  A. Auto-reload "heartbeat" timer (10 ms): count callbacks over 200 ms.
 *  B. One-shot timer (50 ms): fires exactly once.
 *  C. Debounce: a noisy button bounces 6 times, 4 ms apart. Every edge calls
 *     xTimerReset() on a 30 ms one-shot, so the "pressed" callback fires
 *     once, 30 ms after the *last* bounce.
 *  D. xTimerChangePeriod(): the heartbeat goes from 10 ms to 25 ms.
 *  E. Shared callback: 3 timers use one callback and pvTimerGetTimerID().
 *  F. xTimerPendFunctionCall(): defer a function to the daemon task.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "demo_util.h"

static volatile int hb_count, oneshot_count, debounce_count, pend_value;
static volatile TickType_t oneshot_at, debounce_at;
static volatile int shared_hits[3];
static volatile int pend_in_daemon;

static void heartbeat_cb(TimerHandle_t t) { (void)t; hb_count++; }
static void oneshot_cb(TimerHandle_t t) { (void)t; oneshot_count++; oneshot_at = xTaskGetTickCount(); }
static void debounce_cb(TimerHandle_t t) { (void)t; debounce_count++; debounce_at = xTaskGetTickCount(); }
static void shared_cb(TimerHandle_t t)
{
    uintptr_t id = (uintptr_t)pvTimerGetTimerID(t);
    shared_hits[id]++;
    if (shared_hits[id] == (int)(id + 1)) xTimerStop(t, 0); /* timer i fires i+1 times */
}
static void pended_fn(void *p1, uint32_t p2)
{
    pend_in_daemon = (xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle());
    pend_value = (int)(uintptr_t)p1 + (int)p2;
}

static void controller_task(void *arg)
{
    (void)arg;
    /* A */
    TimerHandle_t hb = xTimerCreate("heartbeat", pdMS_TO_TICKS(10), pdTRUE, NULL, heartbeat_cb);
    TickType_t t0 = xTaskGetTickCount();
    xTimerStart(hb, 0);
    vTaskDelayUntil(&t0, pdMS_TO_TICKS(205));
    int a = hb_count;
    demo_log("A: auto-reload 10 ms timer fired %d times in 205 ms", a);
    DEMO_CHECK(a == 20, "expected 20 callbacks, got %d", a);

    /* B (heartbeat still running in the background) */
    TimerHandle_t os = xTimerCreate("oneshot", pdMS_TO_TICKS(50), pdFALSE, NULL, oneshot_cb);
    TickType_t tb = xTaskGetTickCount();
    xTimerStart(os, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    demo_log("B: one-shot fired %d time(s), %lu ms after start", oneshot_count,
             (unsigned long)(oneshot_at - tb));
    DEMO_CHECK(oneshot_count == 1, "one-shot must fire exactly once");
    DEMO_CHECK(oneshot_at - tb == pdMS_TO_TICKS(50), "one-shot period");
    DEMO_CHECK(xTimerIsTimerActive(os) == pdFALSE, "one-shot is dormant after firing");

    /* C */
    TimerHandle_t db = xTimerCreate("debounce", pdMS_TO_TICKS(30), pdFALSE, NULL, debounce_cb);
    TickType_t last_edge = 0;
    for (int i = 0; i < 6; i++) {
        last_edge = xTaskGetTickCount();
        xTimerReset(db, 0);            /* restarts the 30 ms window */
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    demo_log("C: 6 bounces -> debounced callback fired %d time(s), %lu ms after last edge",
             debounce_count, (unsigned long)(debounce_at - last_edge));
    DEMO_CHECK(debounce_count == 1, "debounce should produce one event");
    DEMO_CHECK(debounce_at - last_edge == pdMS_TO_TICKS(30), "fires 30 ms after last bounce");

    /* D */
    xTimerChangePeriod(hb, pdMS_TO_TICKS(25), 0);
    int before = hb_count;
    TickType_t td = xTaskGetTickCount();
    vTaskDelayUntil(&td, pdMS_TO_TICKS(251));
    int d = hb_count - before;
    demo_log("D: after xTimerChangePeriod(25 ms): %d callbacks in 251 ms", d);
    DEMO_CHECK(d == 10, "expected 10 callbacks at 25 ms, got %d", d);
    xTimerStop(hb, 0);

    /* E */
    TimerHandle_t sh[3];
    for (uintptr_t i = 0; i < 3; i++) {
        sh[i] = xTimerCreate("shared", pdMS_TO_TICKS(5 + 5 * i), pdTRUE, (void *)i, shared_cb);
        xTimerStart(sh[i], 0);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    demo_log("E: shared callback hits by timer ID: [%d %d %d]", shared_hits[0], shared_hits[1], shared_hits[2]);
    DEMO_CHECK(shared_hits[0] == 1 && shared_hits[1] == 2 && shared_hits[2] == 3,
               "each timer stops itself after id+1 callbacks");

    /* F */
    xTimerPendFunctionCall(pended_fn, (void *)40, 2, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    demo_log("F: pended function ran in daemon task=%d, result=%d", pend_in_daemon, pend_value);
    DEMO_CHECK(pend_in_daemon && pend_value == 42, "pended function call");

    xTimerDelete(hb, 0); xTimerDelete(os, 0); xTimerDelete(db, 0);
    for (int i = 0; i < 3; i++) xTimerDelete(sh[i], 0);
    vTaskDelay(2);
    vTaskEndScheduler();
}

int main(void)
{
    printf("06-software-timers (FreeRTOS POSIX simulator)\n");
    /* Controller runs below the timer daemon (configMAX_PRIORITIES-1). */
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    vTaskStartScheduler();
    return demo_report();
}
