# 02 — Producer/consumer with a FreeRTOS queue

Three producers at different priorities and rates feed one bounded queue, and two
consumers drain it. The demo checks exactly-once delivery, per-producer FIFO order,
checksum integrity and back-pressure. It also shows `xQueueSendToFront` for urgent
messages and "poison pill" shutdown.

## RTOS concept

A FreeRTOS **queue** is a thread-safe, bounded FIFO. It **copies items by value**, so a
sender can reuse its local buffer as soon as `xQueueSend()` returns. Senders block while
the queue is full and receivers block while it is empty, each up to a timeout. This
blocking gives you flow control (back-pressure) for free.

## How the demo works

| Task | Priority | Role |
|------|----------|------|
| `Ctrl` | 4 | Runs part A, creates the others, waits for completion, verifies |
| `Prod0` | 1 | Sends 300 messages in bursts (never delays) |
| `Prod1` | 3 | Sends 300 messages, `vTaskDelay(1)` every 4th message |
| `Prod2` | 2 | Same pacing as `Prod1` |
| `Cons0`, `Cons1` | 2 | `xQueueReceive(portMAX_DELAY)`, record the message, `vTaskDelay(1)` every 16th sequence number to simulate processing |

Kernel objects:

- `dataQ`: a queue of length 8 holding `msg_t` (kind, producer id, sequence number,
  payload, send timestamp). It is added to the queue registry with
  `vQueueAddToRegistry` so a kernel-aware debugger can show its name.
- Direct-to-task notifications (`xTaskNotifyGive` / `ulTaskNotifyTake`) that let each
  producer and consumer tell `Ctrl` it has finished.

**Part A** fills the queue with 4 data messages, `xQueuePeek`s the oldest one, then
pushes a `MSG_URGENT` with `xQueueSendToFront`. The next `xQueueReceive` must return the
urgent message. `xQueueReset` then empties the queue.

**Part B** runs the 3-producer / 2-consumer pipeline. Each producer sends with a 100 ms
timeout, so a stuck consumer would show up as a failed check instead of a hang. When all
producers have finished, `Ctrl` sends one `MSG_STOP` per consumer (the poison pills) and
waits for both consumers to exit.

The checks: 900 messages received, each `(producer, seq)` seen exactly once,
per-producer sequence numbers strictly increasing at each consumer, sent checksum equal
to received checksum, the queue found full at least once, and both consumers doing
work.

## Build and run

```sh
cmake -S projects/02-producer-consumer-queue -B build/02
cmake --build build/02
./build/02/producer_consumer
(cd build/02 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target producer_consumer` and
`(cd build && ctest -R p02 --output-on-failure)`.

## Demo output

```
02-producer-consumer-queue (FreeRTOS POSIX simulator)
[     0] part A: xQueueSendToFront overtakes 4 queued messages - ok
[     0] part B: 3 producers x 300 msgs, 2 consumers, queue length 8
[    75] producers finished; sending 2 poison pills
[    75] consumer0 got 234, consumer1 got 666 (total 900)
[    75] checksum sent=b9dda639 recv=b9dda639
[    75] queue-full events=78, max depth=8/8, max latency=2 ticks
checks: 10, failures: 0
RESULT: PASS
```

The split between the two consumers and the number of queue-full events can change a
little from run to run. The total, the checksum and the PASS result do not.

## What to look for

- `total 900` and matching checksums: no message was lost, duplicated or corrupted.
- `queue-full events=78, max depth=8/8`: the queue really filled up, so producers were
  held back by blocking sends instead of dropping data.
- `max latency=2 ticks`: with only 8 slots, no message waits long in the queue.
- The consumers split the work unevenly. Both are at priority 2, and which one wakes
  depends on which is blocked when data arrives, but FIFO order per producer still
  holds.

## Key takeaways

- A queue gives you mutual exclusion, blocking, and flow control in one object. You do
  not need a separate mutex around the buffer.
- Items are copied, so keep them small. For large data, queue a pointer or use a
  stream or message buffer (see project 08).
- With several consumers, FIFO order holds *per consumer*, not globally across
  consumers.
- `xQueueSendToFront` is a simple way to let urgent control messages skip queued data.
- Poison-pill messages shut consumers down cleanly, with no forced `vTaskDelete` while
  a task holds data.
