# 07 — Direct-to-task notifications

Task notifications are FreeRTOS's lightest synchronisation primitive. This demo uses
them as a binary semaphore, a counting semaphore, event bits, a mailbox, and several
independent channels (indexed notifications). It then compares the round-trip cost
with a real semaphore.

## RTOS concept

Every task has an array of notification slots in its TCB. Each slot is a 32-bit value
plus a pending state, and there are `configTASK_NOTIFICATION_ARRAY_ENTRIES = 3` slots
here. Notifying a task changes its value directly, and no separate kernel object is
allocated. The price is that a notification has exactly **one receiver**, the task
that owns it, and only that task can block on it.

## How the demo works

`Ctrl` runs at priority 2 and does most experiments on its own notification value, with
zero block time:

| # | Used as | Calls | Check |
|---|---------|-------|-------|
| 1 | Binary semaphore | 4 × `xTaskNotifyGive`, then `ulTaskNotifyTake(pdTRUE, 0)` | returns 4 and clears the value, and the next take returns 0 |
| 2 | Counting semaphore | 4 × give, then `ulTaskNotifyTake(pdFALSE, 0)` in a loop | succeeds 4 times, decrementing each time |
| 3 | Event bits | `xTaskNotify(0x01, eSetBits)`, `xTaskNotify(0x08, eSetBits)`, `xTaskNotifyWait(0, 0xFFFFFFFF, ...)` | receives `0x09`, and nothing is pending afterwards |
| 4 | Mailbox | notifies an idle task `Rx` (1): `eSetValueWithoutOverwrite` 111, then 222, then `xTaskNotifyAndQuery(333, eSetValueWithOverwrite)` | 111 accepted, 222 refused (`pdFAIL`), the query returns the previous value 111, and the final value is 333 (`ulTaskNotifyValueClear`) |
| 5 | Indexed channels | 2 × `xTaskNotifyGiveIndexed(.., 1)` | index 0 is untouched, and index 1 counts 2 |

**Benchmark.** An `Echo` task (priority 3, above `Ctrl`) answers 20000 pings, first with
two binary semaphores and then with notifications. Each give therefore causes a context
switch. The time is host wall-clock time from `clock_gettime`, it is informational only,
and it is not checked.

## Build and run

```sh
cmake -S projects/07-task-notifications -B build/07
cmake --build build/07
./build/07/task_notifications
(cd build/07 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target task_notifications` and
`(cd build && ctest -R p07 --output-on-failure)`.

## Demo output

```
07-task-notifications (FreeRTOS POSIX simulator)
[     1] 1: 4 gives, ulTaskNotifyTake(clear=pdTRUE) returned 4 then 0
[     1] 2: 4 gives, ulTaskNotifyTake(clear=pdFALSE) succeeded 4 times
[     1] 3: eSetBits 0x01 then 0x08 -> received 0x09
[     1] 4: mailbox: 111 accepted, 222 refused, 333 overwrote -> 333
[     1] 5: index 1 received 2 gives, index 0 received none
[   363] bench: 20000 round trips  semaphore 9.75 us/rt  notification 9.33 us/rt  (host timing, dominated by pthread switching; FreeRTOS quotes up to 45% faster on MCUs)
checks: 10, failures: 0
RESULT: PASS
```

The benchmark figures depend on the host and vary between runs.

## What to look for

- Parts 1 and 2 use the same gives. Only the `xClearCountOnExit` argument of
  `ulTaskNotifyTake` decides between binary and counting behaviour.
- In part 4, `eSetValueWithoutOverwrite` refuses to replace an unread value, so it acts
  as a one-slot mailbox. `eSetValueWithOverwrite` always replaces it, so the reader
  sees the latest value.
- In part 5 the two indices are independent, so one task can wait on "RX done" and "TX
  done" on separate slots.
- The benchmark difference is small on Linux. Each context switch there is a pthread
  signal and wake-up, which costs far more than the kernel bookkeeping a notification
  saves. On a Cortex-M the saving is much larger.

## Key takeaways

- Reach for a notification first when exactly one known task is the receiver. It needs
  no allocation, is faster, and uses less RAM than a semaphore, queue or event group.
- Do not use a notification when several tasks may wait, when the sender does not know
  the receiver's handle, or when you need to buffer more than one 32-bit value.
- Kernel libraries such as stream buffers use notification index 0 internally. Using
  index 1 or higher for your own channels avoids interference.
