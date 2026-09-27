# 03 — Binary and counting semaphores

Three small experiments show the two jobs a semaphore does, signalling events and
guarding a pool of resources, plus how a blocking take times out.

## RTOS concept

A **binary semaphore** has a count that can only be 0 or 1. A **counting semaphore**
counts up to a maximum. Both are signalling objects with no owner, which is how they
differ from a mutex (see project 04). The consequence:

- Give a binary semaphore 5 times before anyone takes it and 4 of the events are lost.
- A counting semaphore remembers all 5.
- A counting semaphore set to N guards a pool of N identical resources.

## How the demo works

**Part A — event signalling.** `Ctrl` (priority 4) creates a `Handler` task at priority 1
and then calls `xSemaphoreGive()` 5 times in a row. The handler has lower priority, so
it cannot run between the gives. This models 5 interrupts arriving while the handler is
busy. The handler counts successful takes. When a take has timed out after 30 ms, it
notifies `Ctrl`. The experiment runs once with `xSemaphoreCreateBinary()` and once with
`xSemaphoreCreateCounting(10, 0)`.

**Part B — resource pool.** `pool_sem = xSemaphoreCreateCounting(3, 3)` counts free "DMA
channels". Six `Worker` tasks (priorities 1, 2, 3, 1, 2, 3) each run 15 jobs:

1. Take `pool_sem`, which guarantees that *some* channel is free.
2. Take the `pool_lock` **mutex** to find out *which* channel is free and claim it.
3. Hold the channel for 2–4 ms (`vTaskDelay`), then release it under the mutex and give
   `pool_sem`.

The demo tracks how many channels are in use at once, how many times each channel is
used, and whether the task that releases a channel is the one that owns it.

**Part C — timeout.** `xSemaphoreTake()` on a semaphore that nobody gives, with a 25 ms
block time, must return `pdFALSE` after at least 25 ticks.

Kernel objects: binary semaphore, counting semaphores, a mutex, and task notifications
(`xTaskNotifyGive` / `ulTaskNotifyTake`) for completion signalling.

## Build and run

```sh
cmake -S projects/03-binary-counting-semaphores -B build/03
cmake --build build/03
./build/03/semaphores
(cd build/03 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target semaphores` and
`(cd build && ctest -R p03 --output-on-failure)`.

## Demo output

```
03-binary-counting-semaphores (FreeRTOS POSIX simulator)
[    31] part A: binary   semaphore: 5 events given, 1 handled
[    61] part A: counting semaphore: 5 events given, 5 handled
[    61] part B: 6 workers share a pool of 3 channels, 15 jobs each
[   166] part B: jobs=90, max concurrent users=3, channel uses=[32 31 27], free count now=3
[   191] part C: take with 25 ms timeout returned 0 after 25 ticks
checks: 9, failures: 0
RESULT: PASS
```

The per-channel usage split can vary slightly between runs.

## What to look for

- `binary ... 1 handled` vs `counting ... 5 handled`: this is the whole difference
  between the two types when events arrive faster than they are handled.
- `max concurrent users=3`: the pool was fully used, and never more than 3 at once.
- `free count now=3`: every channel was returned. `uxSemaphoreGetCount()` shows the
  semaphore's count.
- `returned 0 after 25 ticks`: the timeout is honoured to the tick.

## Key takeaways

- Use a binary semaphore (or better, a task notification, see project 07) when you only
  need to know that something happened. Use a counting semaphore when you must count
  every event or guard N resources.
- A counting semaphore limits *how many* tasks use a pool, not *which* item each one
  gets. You still need a mutex, or another lock-free method, to hand out specific
  items.
- Always choose a finite timeout on purpose. `portMAX_DELAY` hides deadlocks, while a
  timeout lets the code detect them and recover.
