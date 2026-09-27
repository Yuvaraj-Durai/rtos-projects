/*
 * 03 - Binary and counting semaphores
 *
 * Part A  Event signalling: binary vs counting semaphore.
 *         A high-rate "event source" signals 5 events while the (lower
 *         priority) handler is busy. A binary semaphore saturates at 1, so
 *         4 events are lost; a counting semaphore latches all 5.
 *
 * Part B  Resource pool: a counting semaphore initialised to 3 guards a pool
 *         of 3 identical "DMA channels" used by 6 worker tasks. We measure the
 *         number of simultaneous users and check it never exceeds 3 (and
 *         does reach 3), and that each channel is owned by one task at a time.
 *
 * Part C  Timeouts: xSemaphoreTake() with a finite block time returns pdFALSE
 *         after (at least) that many ticks when nobody gives.
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "demo_util.h"

#define POOL_SIZE    3
#define NUM_WORKERS  6
#define JOBS_EACH    15

static TaskHandle_t controller;

/* ---------------- Part A ------------------------------------------------- */
static SemaphoreHandle_t evt_sem;
static volatile int handled;

static void handler_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(evt_sem, pdMS_TO_TICKS(30)) == pdTRUE) {
            handled++;
        } else {
            xTaskNotifyGive(controller); /* idle for 30 ms: burst is over */
            vTaskSuspend(NULL);
        }
    }
}

static int run_signalling(SemaphoreHandle_t s, const char *label)
{
    evt_sem = s;
    handled = 0;
    TaskHandle_t h;
    xTaskCreate(handler_task, "Handler", configMINIMAL_STACK_SIZE, NULL, 1, &h);
    /* We (priority 4) give 5 times in a row: the handler cannot run between
     * gives, which models 5 interrupts arriving while the handler is busy. */
    for (int i = 0; i < 5; i++) xSemaphoreGive(s);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelete(h);
    demo_log("part A: %-8s semaphore: 5 events given, %d handled", label, handled);
    return handled;
}

/* ---------------- Part B ------------------------------------------------- */
static SemaphoreHandle_t pool_sem;      /* counts free channels */
static SemaphoreHandle_t pool_lock;     /* protects channel_owner[] */
static TaskHandle_t channel_owner[POOL_SIZE];
static volatile int in_use, max_in_use, jobs_done, ownership_errors;
static int uses_per_channel[POOL_SIZE];

static int channel_acquire(void)
{
    xSemaphoreTake(pool_sem, portMAX_DELAY);  /* a channel is guaranteed free */
    xSemaphoreTake(pool_lock, portMAX_DELAY); /* now find which one */
    int ch = -1;
    for (int i = 0; i < POOL_SIZE; i++) {
        if (channel_owner[i] == NULL) { ch = i; channel_owner[i] = xTaskGetCurrentTaskHandle(); break; }
    }
    in_use++;
    if (in_use > max_in_use) max_in_use = in_use;
    xSemaphoreGive(pool_lock);
    return ch;
}

static void channel_release(int ch)
{
    xSemaphoreTake(pool_lock, portMAX_DELAY);
    if (channel_owner[ch] != xTaskGetCurrentTaskHandle()) ownership_errors++;
    channel_owner[ch] = NULL;
    in_use--;
    xSemaphoreGive(pool_lock);
    xSemaphoreGive(pool_sem);
}

static void worker_task(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (int j = 0; j < JOBS_EACH; j++) {
        int ch = channel_acquire();
        if (ch < 0) { ownership_errors++; continue; }
        uses_per_channel[ch]++;
        vTaskDelay(pdMS_TO_TICKS(2 + (id + j) % 3)); /* "transfer" in progress */
        channel_release(ch);
        taskENTER_CRITICAL();
        jobs_done++;
        taskEXIT_CRITICAL();
        vTaskDelay(1);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();

    SemaphoreHandle_t bin = xSemaphoreCreateBinary();
    SemaphoreHandle_t cnt = xSemaphoreCreateCounting(10, 0);
    int hb = run_signalling(bin, "binary");
    int hc = run_signalling(cnt, "counting");
    DEMO_CHECK(hb == 1, "binary semaphore should collapse 5 gives into 1 (got %d)", hb);
    DEMO_CHECK(hc == 5, "counting semaphore should latch all 5 events (got %d)", hc);
    DEMO_CHECK(xSemaphoreGive(bin) == pdTRUE && xSemaphoreGive(bin) == pdFALSE,
               "second give on a full binary semaphore fails");
    vSemaphoreDelete(bin);
    vSemaphoreDelete(cnt);

    pool_sem = xSemaphoreCreateCounting(POOL_SIZE, POOL_SIZE);
    pool_lock = xSemaphoreCreateMutex();
    demo_log("part B: %d workers share a pool of %d channels, %d jobs each",
             NUM_WORKERS, POOL_SIZE, JOBS_EACH);
    TaskHandle_t w[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        char name[configMAX_TASK_NAME_LEN];
        snprintf(name, sizeof name, "Worker%d", i);
        xTaskCreate(worker_task, name, configMINIMAL_STACK_SIZE, (void *)(uintptr_t)i,
                    1 + (i % 3), &w[i]);
    }
    for (int i = 0; i < NUM_WORKERS; i++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    for (int i = 0; i < NUM_WORKERS; i++) vTaskDelete(w[i]);

    demo_log("part B: jobs=%d, max concurrent users=%d, channel uses=[%d %d %d], "
             "free count now=%lu", jobs_done, max_in_use, uses_per_channel[0],
             uses_per_channel[1], uses_per_channel[2], (unsigned long)uxSemaphoreGetCount(pool_sem));
    DEMO_CHECK(jobs_done == NUM_WORKERS * JOBS_EACH, "all jobs completed");
    DEMO_CHECK(max_in_use <= POOL_SIZE, "pool over-subscribed (%d)", max_in_use);
    DEMO_CHECK(max_in_use == POOL_SIZE, "pool should be fully used at some point");
    DEMO_CHECK(ownership_errors == 0, "channel ownership errors: %d", ownership_errors);
    DEMO_CHECK(uxSemaphoreGetCount(pool_sem) == POOL_SIZE, "all channels returned");

    /* Part C: timeout. */
    SemaphoreHandle_t never = xSemaphoreCreateBinary();
    TickType_t t0 = xTaskGetTickCount();
    BaseType_t r = xSemaphoreTake(never, pdMS_TO_TICKS(25));
    TickType_t waited = xTaskGetTickCount() - t0;
    demo_log("part C: take with 25 ms timeout returned %ld after %lu ticks", (long)r, (unsigned long)waited);
    DEMO_CHECK(r == pdFALSE && waited >= pdMS_TO_TICKS(25), "timeout honoured");
    vSemaphoreDelete(never);

    vTaskEndScheduler();
}

int main(void)
{
    printf("03-binary-counting-semaphores (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL);
    vTaskStartScheduler();
    return demo_report();
}
