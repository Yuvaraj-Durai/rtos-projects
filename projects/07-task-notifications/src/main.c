/*
 * 07 - Direct-to-task notifications
 *
 * Every task owns an array of notification values (here 3 entries). A
 * notification is a 32-bit value + a pending flag stored in the TCB, so no
 * separate kernel object is allocated. This demo uses it five ways:
 *
 *  1. Binary semaphore  : ulTaskNotifyTake(pdTRUE)  - collapses N gives
 *  2. Counting semaphore: ulTaskNotifyTake(pdFALSE) - counts gives
 *  3. Event bits        : xTaskNotify(eSetBits) + xTaskNotifyWait()
 *  4. Mailbox           : eSetValueWithoutOverwrite vs eSetValueWithOverwrite
 *  5. Indexed channels  : xTaskNotifyGiveIndexed(.., 1) does not disturb index 0
 *
 * Finally it times 20000 give/take round trips through a binary semaphore
 * and through a notification (host wall-clock, informational only).
 */
#include <time.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "demo_util.h"

static TaskHandle_t receiver_h, controller_h;

static void idle_receiver(void *arg)
{
    (void)arg;
    vTaskSuspend(NULL);
    for (;;) vTaskSuspend(NULL);
}

/* ---- round-trip benchmark ------------------------------------------------ */
#define ROUNDS 20000
static SemaphoreHandle_t ping, pong;
static volatile int bench_mode; /* 0 = semaphore, 1 = notification */

static void echo_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (bench_mode == 0) {
            xSemaphoreTake(ping, portMAX_DELAY);
            xSemaphoreGive(pong);
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            xTaskNotifyGive(controller_h);
        }
    }
}

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void controller_task(void *arg)
{
    (void)arg;
    controller_h = xTaskGetCurrentTaskHandle();
    uint32_t v;

    /* 1 + 2: give to ourselves; take semantics decide binary vs counting */
    for (int i = 0; i < 4; i++) xTaskNotifyGive(controller_h);
    v = ulTaskNotifyTake(pdTRUE, 0);
    DEMO_CHECK(v == 4 && ulTaskNotifyTake(pdTRUE, 0) == 0, "binary-style take clears the count");
    demo_log("1: 4 gives, ulTaskNotifyTake(clear=pdTRUE) returned %lu then 0", (unsigned long)v);

    for (int i = 0; i < 4; i++) xTaskNotifyGive(controller_h);
    int takes = 0;
    while (ulTaskNotifyTake(pdFALSE, 0) > 0) takes++;
    DEMO_CHECK(takes == 4, "counting-style take decrements (got %d takes)", takes);
    demo_log("2: 4 gives, ulTaskNotifyTake(clear=pdFALSE) succeeded %d times", takes);

    /* 3: event bits */
    xTaskNotify(controller_h, 0x01, eSetBits);
    xTaskNotify(controller_h, 0x08, eSetBits);
    xTaskNotifyWait(0, 0xFFFFFFFFu, &v, 0); /* clear all bits on exit */
    uint32_t bits = v;
    DEMO_CHECK(bits == 0x09, "bits OR together (0x%lx)", (unsigned long)bits);
    DEMO_CHECK(xTaskNotifyWait(0, 0, &v, 0) == pdFALSE, "nothing pending after clear");
    demo_log("3: eSetBits 0x01 then 0x08 -> received 0x%02lx", (unsigned long)bits);

    /* 4: mailbox */
    xTaskCreate(idle_receiver, "Rx", configMINIMAL_STACK_SIZE, NULL, 1, &receiver_h);
    BaseType_t r1 = xTaskNotify(receiver_h, 111, eSetValueWithoutOverwrite);
    BaseType_t r2 = xTaskNotify(receiver_h, 222, eSetValueWithoutOverwrite); /* refused */
    uint32_t prev;
    xTaskNotifyAndQuery(receiver_h, 333, eSetValueWithOverwrite, &prev);        /* forced */
    DEMO_CHECK(r1 == pdPASS && r2 == pdFAIL, "without-overwrite refuses when pending");
    DEMO_CHECK(prev == 111, "query returns previous value");
    uint32_t mailbox = ulTaskNotifyValueClear(receiver_h, 0);
    DEMO_CHECK(mailbox == 333, "overwrite replaced the value (%lu)", (unsigned long)mailbox);
    demo_log("4: mailbox: 111 accepted, 222 refused, 333 overwrote -> %lu", (unsigned long)mailbox);

    /* 5: indexed notifications */
    xTaskNotifyStateClear(controller_h);
    xTaskNotifyGiveIndexed(controller_h, 1);
    xTaskNotifyGiveIndexed(controller_h, 1);
    DEMO_CHECK(ulTaskNotifyTake(pdTRUE, 0) == 0, "index 0 untouched");
    uint32_t i1 = ulTaskNotifyTakeIndexed(1, pdTRUE, 0);
    DEMO_CHECK(i1 == 2, "index 1 counted 2 gives (got %lu)", (unsigned long)i1);
    demo_log("5: index 1 received %lu gives, index 0 received none", (unsigned long)i1);
    vTaskDelete(receiver_h);

    /* benchmark (priority of echo task > ours so every give switches task) */
    ping = xSemaphoreCreateBinary();
    pong = xSemaphoreCreateBinary();
    TaskHandle_t echo;
    xTaskCreate(echo_task, "Echo", configMINIMAL_STACK_SIZE, NULL, 3, &echo);
    double t0 = now_us();
    for (int i = 0; i < ROUNDS; i++) { xSemaphoreGive(ping); xSemaphoreTake(pong, portMAX_DELAY); }
    double t_sem = now_us() - t0;
    bench_mode = 1;
    xSemaphoreGive(ping); /* let echo loop once in semaphore mode, then switch */
    xSemaphoreTake(pong, portMAX_DELAY);
    t0 = now_us();
    for (int i = 0; i < ROUNDS; i++) { xTaskNotifyGive(echo); ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    double t_ntf = now_us() - t0;
    demo_log("bench: %d round trips  semaphore %.2f us/rt  notification %.2f us/rt  (host timing, "
             "dominated by pthread switching; FreeRTOS quotes up to 45%% faster on MCUs)",
             ROUNDS, t_sem / ROUNDS, t_ntf / ROUNDS);
    DEMO_CHECK(t_sem > 0 && t_ntf > 0, "benchmark ran");
    vTaskDelete(echo);
    vSemaphoreDelete(ping);
    vSemaphoreDelete(pong);
    vTaskEndScheduler();
}

int main(void)
{
    printf("07-task-notifications (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    vTaskStartScheduler();
    return demo_report();
}
