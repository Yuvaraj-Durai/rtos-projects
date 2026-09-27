# 05 — Event groups

This demo uses event groups in three ways: waiting for **all** of several conditions
(system start-up), waiting for **any** of several conditions (fault alarms), and a
**rendezvous barrier** that keeps a set of worker tasks in step.

## RTOS concept

An **event group** is a word of event bits (24 usable bits with 32-bit ticks). Tasks
set and clear bits, and a task can block until a chosen set of bits matches. The match
can be "all of them" or "any of them", and the waiter can clear the matched bits on
exit. `xEventGroupSync()` sets a task's own bit and waits for the others in one atomic
step, which is how you build a barrier with no race between the two.

## How the demo works

| Part | Tasks (priority) | Kernel calls |
|------|------------------|--------------|
| A. Wait for ALL | `network` (2), `sensor` (2), `storage` (2); `Ctrl` (4) waits | `xEventGroupSetBits`, `xEventGroupWaitBits(..., clearOnExit=pdFALSE, waitForAll=pdTRUE, ...)` |
| B. Wait for ANY | `Alarm` (3); `Ctrl` raises the faults | `xEventGroupWaitBits(..., clearOnExit=pdTRUE, waitForAll=pdFALSE, ...)`, `xEventGroupGetBits` |
| C. Barrier | 4 × `Worker` (priorities 1, 2, 1, 2) | `xEventGroupSync` on a second event group |

**Part A.** The subsystems come up after 20 ms (network), 30 ms (storage) and 45 ms
(sensor), and each one sets its READY bit (bits 0–2). `Ctrl` first waits for all three
with a 10 ms timeout, which is expected to expire. It then waits up to 200 ms and must
wake with all three bits set, between 45 and 60 ms after the start.

**Part B.** `Alarm` waits for any of OVERTEMP (bit 4), UNDERVOLT (bit 5) and COMMS
(bit 6). `Ctrl` raises OVERTEMP, then UNDERVOLT | COMMS together in a single call, then
COMMS alone, 5 ms apart. The alarm task must wake exactly three times and see `0x10`,
`0x60` and `0x40`. Because it clears on exit, the fault bits are 0 afterwards, while the
READY bits from part A are left alone.

**Part C.** Each worker runs 5 phases of uneven length (1–6 ms). At the end of each
phase it calls `xEventGroupSync(barrier, 1 << id, 0x0F, portMAX_DELAY)`. When a worker
starts phase k+1, it checks that all 4 workers have finished phase k. Any worker that
runs ahead counts as a violation.

## Build and run

```sh
cmake -S projects/05-event-groups -B build/05
cmake --build build/05
./build/05/event_groups_demo
(cd build/05 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target event_groups_demo` and
`(cd build && ctest -R p05 --output-on-failure)`.

## Demo output

```
05-event-groups (FreeRTOS POSIX simulator)
[     0] part A: waiting for ALL of net|sensor|storage
[    21]   network ready
[    31]   storage ready
[    46]   sensor ready
[    46] part A: all ready after 45 ms (slowest subsystem boots in 45 ms)
[    46] part B: alarm task waits for ANY fault bit
[    51]   alarm task woke: faults=0x10
[    56]   alarm task woke: faults=0x60
[    61]   alarm task woke: faults=0x40
[    61] part C: 4 workers x 5 phases with xEventGroupSync barrier
[    85] part C: phase completion counts = 4 4 4 4 4, violations = 0
checks: 15, failures: 0
RESULT: PASS
```

## What to look for

- Part A wakes exactly when the *last* subsystem (sensor, 45 ms) is ready, not at the
  first one.
- `faults=0x60`: two bits set in one `xEventGroupSetBits()` call wake the waiter once,
  and it sees both bits.
- `violations = 0`: no worker started a phase before every worker had finished the
  previous one, even though phase lengths differ from worker to worker.

## Key takeaways

- An event group lets one task wait on a *combination* of conditions. A queue or
  semaphore cannot do that on its own.
- Use `clearOnExit` for events that should be consumed once, and leave it off for state
  flags such as READY.
- Build barriers with `xEventGroupSync()`, not with separate set and wait calls. The
  sync call is atomic, so a worker cannot miss the moment the barrier opens.
- Setting bits wakes *every* task whose condition now matches. Setting bits from an ISR
  (`xEventGroupSetBitsFromISR`) is deferred to the timer service task, because the
  wake-up count is not bounded.
