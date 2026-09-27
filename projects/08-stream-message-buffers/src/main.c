/*
 * 08 - Stream buffers and message buffers
 *
 * Both are single-writer / single-reader byte FIFOs optimised for passing
 * data from one task (or ISR) to one other task, with no per-item copy of a
 * fixed-size structure like queues.
 *
 *  A. Stream buffer: a writer sends a 64 KiB pseudo-random byte stream in
 *     irregular chunk sizes (1..97 bytes). The reader uses a trigger level
 *     of 32 bytes and reads in 50-byte gulps. We compare CRC-32 and length on
 *     both sides. Boundaries between writes are NOT preserved.
 *
 *  B. Message buffer: a writer sends 500 variable-length messages (1..60
 *     bytes). Each read returns exactly one whole message, so framing IS
 *     preserved. Each message carries its sequence number and length.
 *
 *  C. A message larger than the reader's buffer is left in place
 *     (xMessageBufferReceive returns 0) instead of being truncated.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "stream_buffer.h"
#include "task.h"
#include "demo_util.h"

#define STREAM_TOTAL   (64 * 1024)
#define STREAM_SIZE    256
#define TRIGGER_LEVEL  32
#define NUM_MESSAGES   500
#define MSGBUF_SIZE    256

static StreamBufferHandle_t sb;
static MessageBufferHandle_t mb;
static TaskHandle_t controller;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1103515245u + 12345u; return (*s >> 16) & 0x7fff; }

/* ---------------- A: stream ---------------------------------------------- */
static uint32_t tx_crc, rx_crc;
static size_t tx_len, rx_len;
static int rx_reads, blocked_reads, short_reads;

static void stream_writer(void *arg)
{
    (void)arg;
    uint32_t seed = 42;
    uint8_t chunk[97];
    while (tx_len < STREAM_TOTAL) {
        size_t n = 1 + lcg(&seed) % sizeof chunk;
        if (n > STREAM_TOTAL - tx_len) n = STREAM_TOTAL - tx_len;
        for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)lcg(&seed);
        size_t sent = 0;
        while (sent < n) sent += xStreamBufferSend(sb, chunk + sent, n - sent, portMAX_DELAY);
        tx_crc = crc32_update(tx_crc, chunk, n);
        tx_len += n;
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void stream_reader(void *arg)
{
    (void)arg;
    uint8_t buf[50];
    while (rx_len < STREAM_TOTAL) {
        size_t avail_before = xStreamBufferBytesAvailable(sb);
        size_t n = xStreamBufferReceive(sb, buf, sizeof buf, pdMS_TO_TICKS(50));
        if (n == 0) continue;
        /* A reader that had to block is only woken once >= TRIGGER_LEVEL
         * bytes are present (or on timeout at the very end of the stream).
         * A non-blocking read simply returns whatever is there. */
        if (avail_before == 0) {
            blocked_reads++;
            if (n < TRIGGER_LEVEL) short_reads++;
        }
        rx_reads++;
        rx_crc = crc32_update(rx_crc, buf, n);
        rx_len += n;
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

/* ---------------- B: messages -------------------------------------------- */
typedef struct { uint16_t seq; uint8_t len; uint8_t data[60]; } wire_msg_t;
static int msgs_ok, msgs_bad;

static void msg_writer(void *arg)
{
    (void)arg;
    uint32_t seed = 7;
    wire_msg_t m;
    for (uint16_t s = 0; s < NUM_MESSAGES; s++) {
        m.seq = s;
        m.len = (uint8_t)(1 + lcg(&seed) % 60);
        for (int i = 0; i < m.len; i++) m.data[i] = (uint8_t)(s + i);
        size_t bytes = offsetof(wire_msg_t, data) + m.len;
        xMessageBufferSend(mb, &m, bytes, portMAX_DELAY);
        if (s % 50 == 0) vTaskDelay(1);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void msg_reader(void *arg)
{
    (void)arg;
    wire_msg_t m;
    for (int expect = 0; expect < NUM_MESSAGES; expect++) {
        size_t n = xMessageBufferReceive(mb, &m, sizeof m, portMAX_DELAY);
        int ok = (m.seq == expect) && (n == offsetof(wire_msg_t, data) + m.len);
        for (int i = 0; ok && i < m.len; i++) ok = (m.data[i] == (uint8_t)(m.seq + i));
        if (ok) msgs_ok++; else msgs_bad++;
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    TaskHandle_t a, b;

    sb = xStreamBufferCreate(STREAM_SIZE, TRIGGER_LEVEL);
    xTaskCreate(stream_reader, "SRead", configMINIMAL_STACK_SIZE * 2, NULL, 2, &b);
    xTaskCreate(stream_writer, "SWrite", configMINIMAL_STACK_SIZE * 2, NULL, 1, &a);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    demo_log("A: stream  tx %zu bytes crc=%08lx | rx %zu bytes crc=%08lx in %d reads",
             tx_len, (unsigned long)tx_crc, rx_len, (unsigned long)rx_crc, rx_reads);
    demo_log("A: reader blocked %d times; woken with < %d bytes %d times", blocked_reads,
             TRIGGER_LEVEL, short_reads);
    DEMO_CHECK(tx_len == STREAM_TOTAL && rx_len == STREAM_TOTAL, "byte count");
    DEMO_CHECK(tx_crc == rx_crc, "CRC-32 matches");
    DEMO_CHECK(blocked_reads > 0 && short_reads <= 1, "blocked reader woke below trigger level %d times", short_reads);
    vTaskDelete(a); vTaskDelete(b);
    vStreamBufferDelete(sb);

    mb = xMessageBufferCreate(MSGBUF_SIZE);
    xTaskCreate(msg_reader, "MRead", configMINIMAL_STACK_SIZE * 2, NULL, 2, &b);
    xTaskCreate(msg_writer, "MWrite", configMINIMAL_STACK_SIZE * 2, NULL, 1, &a);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    demo_log("B: message buffer: %d/%d messages intact, %d corrupted/misframed", msgs_ok, NUM_MESSAGES, msgs_bad);
    DEMO_CHECK(msgs_ok == NUM_MESSAGES && msgs_bad == 0, "framing preserved");
    vTaskDelete(a); vTaskDelete(b);

    /* C: too-big message stays in the buffer */
    uint8_t big[40] = { 0 }, small[16];
    xMessageBufferSend(mb, big, sizeof big, 0);
    size_t got = xMessageBufferReceive(mb, small, sizeof small, 0);
    size_t still = xMessageBufferNextLengthBytes(mb);
    demo_log("C: 40-byte message into 16-byte buffer -> received %zu, next message length %zu", got, still);
    DEMO_CHECK(got == 0 && still == sizeof big, "oversized message not truncated");
    vMessageBufferDelete(mb);

    vTaskEndScheduler();
}

int main(void)
{
    printf("08-stream-message-buffers (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL);
    vTaskStartScheduler();
    return demo_report();
}
