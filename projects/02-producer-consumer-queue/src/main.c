/*
 * 02 - Producer/consumer with FreeRTOS queues
 *
 * Three producers with different priorities and rates push fixed-size
 * messages into one bounded queue that two consumers drain. The queue copies
 * messages by value, so producers can reuse their local buffers.
 *
 * Verified at the end:
 *   - every message is delivered exactly once (per-producer bitmap)
 *   - per-producer FIFO order is preserved for each consumer
 *   - payload checksum sent == checksum received
 *   - the queue actually became full (producers experienced back-pressure)
 *   - an urgent message sent with xQueueSendToFront overtakes queued data
 *   - consumers shut down cleanly via "poison pill" messages
 */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "demo_util.h"

#define NUM_PRODUCERS   3
#define NUM_CONSUMERS   2
#define MSGS_PER_PROD   300
#define QUEUE_LEN       8

typedef enum { MSG_DATA, MSG_URGENT, MSG_STOP } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    uint8_t    producer;
    uint16_t   seq;
    uint32_t   payload;
    TickType_t sent_at;
} msg_t;

static QueueHandle_t q;
static TaskHandle_t  controller;

/* Bookkeeping (each consumer writes only when holding the CPU; a critical
 * section protects the shared counters). */
static uint8_t  seen[NUM_PRODUCERS][MSGS_PER_PROD];
static int      last_seq[NUM_CONSUMERS][NUM_PRODUCERS];
static uint32_t sum_sent, sum_recv;
static int      recv_count[NUM_CONSUMERS];
static int      order_errors, dup_errors;
static int      full_events;       /* producer found the queue full */
static int      max_fill;         /* deepest queue level seen by a producer */
static TickType_t max_latency;

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

static void producer_task(void *arg)
{
    uint8_t id = (uint8_t)(uintptr_t)arg;
    uint32_t rng = 1234u + id;
    msg_t m = { .kind = MSG_DATA, .producer = id };

    for (uint16_t i = 0; i < MSGS_PER_PROD; i++) {
        m.seq = i;
        m.payload = lcg(&rng);
        taskENTER_CRITICAL();
        sum_sent += m.payload;
        if (uxQueueSpacesAvailable(q) == 0) full_events++;
        if ((int)uxQueueMessagesWaiting(q) > max_fill) max_fill = (int)uxQueueMessagesWaiting(q);
        taskEXIT_CRITICAL();
        m.sent_at = xTaskGetTickCount();
        /* Block (up to 100 ms) while the queue is full: back-pressure. */
        if (xQueueSend(q, &m, pdMS_TO_TICKS(100)) != pdPASS) {
            DEMO_CHECK(0, "producer %u timed out sending seq %u", id, i);
        }
        /* Producer 0 is bursty, others pace themselves. */
        if (id != 0 && (i % 4) == 0) vTaskDelay(1);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void consumer_task(void *arg)
{
    int cid = (int)(uintptr_t)arg;
    msg_t m;
    for (int p = 0; p < NUM_PRODUCERS; p++) last_seq[cid][p] = -1;

    for (;;) {
        xQueueReceive(q, &m, portMAX_DELAY);
        if (m.kind == MSG_STOP) break;
        if (m.kind == MSG_URGENT) continue; /* only used in part A */
        taskENTER_CRITICAL();
        if (seen[m.producer][m.seq]++) dup_errors++;
        if ((int)m.seq <= last_seq[cid][m.producer]) order_errors++;
        last_seq[cid][m.producer] = m.seq;
        sum_recv += m.payload;
        recv_count[cid]++;
        TickType_t lat = xTaskGetTickCount() - m.sent_at;
        if (lat > max_latency) max_latency = lat;
        taskEXIT_CRITICAL();
        /* Simulate processing time so the queue fills up. */
        if ((m.seq % 16) == 0) vTaskDelay(1);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    TaskHandle_t prod[NUM_PRODUCERS], cons[NUM_CONSUMERS];
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    q = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    vQueueAddToRegistry(q, "dataQ");
    configASSERT(q);

    /* Part A: xQueueSendToFront lets an urgent message jump the line.
     * Fill the queue half way with data, then push an urgent message to the
     * front; the consumer must see it first. */
    msg_t m = { .kind = MSG_DATA, .producer = 0 };
    for (int i = 0; i < 4; i++) { m.seq = (uint16_t)(MSGS_PER_PROD - 1 - i); xQueueSend(q, &m, 0); }
    msg_t peek;
    xQueuePeek(q, &peek, 0);
    DEMO_CHECK(peek.kind == MSG_DATA, "peek sees the oldest data message");
    msg_t urgent = { .kind = MSG_URGENT, .payload = 0 };
    xQueueSendToFront(q, &urgent, 0);
    xQueueReceive(q, &m, 0);
    DEMO_CHECK(m.kind == MSG_URGENT, "urgent message is received first");
    xQueueReset(q);
    DEMO_CHECK(uxQueueMessagesWaiting(q) == 0, "queue empty after reset");
    demo_log("part A: xQueueSendToFront overtakes 4 queued messages - ok");

    /* Part B: 3 producers -> 2 consumers through one bounded queue. */
    demo_log("part B: %d producers x %d msgs, %d consumers, queue length %d",
             NUM_PRODUCERS, MSGS_PER_PROD, NUM_CONSUMERS, QUEUE_LEN);
    for (int c = 0; c < NUM_CONSUMERS; c++)
        xTaskCreate(consumer_task, c ? "Cons1" : "Cons0", configMINIMAL_STACK_SIZE * 2,
                    (void *)(uintptr_t)c, 2, &cons[c]);
    static const UBaseType_t prod_prio[NUM_PRODUCERS] = { 1, 3, 2 };
    static const char *names[NUM_PRODUCERS] = { "Prod0", "Prod1", "Prod2" };
    for (int p = 0; p < NUM_PRODUCERS; p++)
        xTaskCreate(producer_task, names[p], configMINIMAL_STACK_SIZE * 2,
                    (void *)(uintptr_t)p, prod_prio[p], &prod[p]);

    for (int p = 0; p < NUM_PRODUCERS; p++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    demo_log("producers finished; sending %d poison pills", NUM_CONSUMERS);
    msg_t stop = { .kind = MSG_STOP };
    for (int c = 0; c < NUM_CONSUMERS; c++) xQueueSend(q, &stop, portMAX_DELAY);
    for (int c = 0; c < NUM_CONSUMERS; c++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    int missing = 0;
    for (int p = 0; p < NUM_PRODUCERS; p++)
        for (int i = 0; i < MSGS_PER_PROD; i++)
            if (seen[p][i] != 1) missing++;

    demo_log("consumer0 got %d, consumer1 got %d (total %d)", recv_count[0], recv_count[1],
             recv_count[0] + recv_count[1]);
    demo_log("checksum sent=%08lx recv=%08lx", (unsigned long)sum_sent, (unsigned long)sum_recv);
    demo_log("queue-full events=%d, max depth=%d/%d, max latency=%lu ticks",
             full_events, max_fill, QUEUE_LEN, (unsigned long)max_latency);

    DEMO_CHECK(recv_count[0] + recv_count[1] == NUM_PRODUCERS * MSGS_PER_PROD, "all messages received");
    DEMO_CHECK(missing == 0, "%d messages missing or duplicated", missing);
    DEMO_CHECK(dup_errors == 0, "duplicates: %d", dup_errors);
    DEMO_CHECK(order_errors == 0, "per-producer FIFO order violated %d times", order_errors);
    DEMO_CHECK(sum_sent == sum_recv, "checksum mismatch");
    DEMO_CHECK(full_events > 0, "expected back-pressure (queue full) at least once");
    DEMO_CHECK(recv_count[0] > 0 && recv_count[1] > 0, "both consumers did work");

    for (int p = 0; p < NUM_PRODUCERS; p++) vTaskDelete(prod[p]);
    for (int c = 0; c < NUM_CONSUMERS; c++) vTaskDelete(cons[c]);
    vQueueDelete(q);
    vTaskEndScheduler();
}

int main(void)
{
    printf("02-producer-consumer-queue (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL);
    vTaskStartScheduler();
    return demo_report();
}
