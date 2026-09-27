# RTOS Projects (FreeRTOS)

[![CI](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)

This repo holds 12 FreeRTOS projects in C. Each one demonstrates one core real-time OS
concept: scheduling and priorities, queues, semaphores and mutexes, priority
inheritance, event groups, software timers, task notifications, stream and message
buffers, deferred interrupt handling, and watchdog supervision. The last two are small
applications that combine several of these: a sensor-processing pipeline and a UART
command-line interface.

Every demo checks its own results. It runs a fixed scenario, compares what happened
with what should happen (ordering, timing to the tick, data integrity), prints
`RESULT: PASS` or `RESULT: FAIL`, and then ends the scheduler. Projects 11 and 12 also
have host unit tests for their RTOS-independent code. CI builds everything and runs
all the tests on every push.

> **Status:** everything here runs on the **FreeRTOS POSIX simulator port**
> (`portable/ThirdParty/GCC/Posix`) on Linux, **not on microcontroller hardware**.
> Each task is a pthread, the tick is a host timer signal at 1 kHz, and the
> "interrupts" (UART RX, ADC, GPIO, hardware watchdog) are simulated from the FreeRTOS
> tick hook. Scheduling behaviour (priorities, preemption, blocking, time slicing,
> tick-accurate timing) matches a single-core MCU. Wall-clock benchmarks, stack
> high-water marks and ISR latency in microseconds do not carry over to real hardware.

## Projects

| #  | Project | What it shows |
|----|---------|---------------|
| 01 | [Task priorities](projects/01-task-priorities) | Preemptive fixed-priority scheduling, preemption inside `xTaskCreate`/`vTaskPrioritySet`, time slicing, suspend/resume/delete |
| 02 | [Producer/consumer queue](projects/02-producer-consumer-queue) | 3 producers → 2 consumers through a bounded queue: exactly-once delivery, FIFO order, back-pressure, `xQueueSendToFront`, poison pills |
| 03 | [Binary & counting semaphores](projects/03-binary-counting-semaphores) | Lost events with a binary semaphore vs latched events with a counting one, a resource pool of 3 channels, take timeouts |
| 04 | [Priority inversion](projects/04-priority-inversion) | The Mars Pathfinder scenario: unbounded inversion with a semaphore (126 ticks) vs priority inheritance with a mutex (25 ticks) |
| 05 | [Event groups](projects/05-event-groups) | Wait-for-all start-up, wait-for-any alarms with clear-on-exit, an `xEventGroupSync` rendezvous barrier |
| 06 | [Software timers](projects/06-software-timers) | Auto-reload and one-shot timers, a timer-reset debouncer, period change, shared callback with timer ID, `xTimerPendFunctionCall` |
| 07 | [Task notifications](projects/07-task-notifications) | Notifications as binary/counting semaphore, event bits, mailbox and indexed channels, plus a round-trip benchmark against a semaphore |
| 08 | [Stream & message buffers](projects/08-stream-message-buffers) | 64 KiB CRC-checked byte stream, trigger level, 500 framed variable-length messages, oversized-message handling |
| 09 | [Deferred interrupt processing](projects/09-deferred-interrupt-processing) | Simulated UART/ADC/GPIO ISRs → queue, notification and pended function; how handler priority sets latency under a CPU hog |
| 10 | [Watchdog health monitor](projects/10-watchdog-health-monitor) | Per-task check-ins and deadlines, a monitor task, task restart, escalation to a hardware-watchdog reset |
| 11 | [Sensor data pipeline](projects/11-sensor-data-pipeline) | Acquire → Filter → Log tasks joined by queues, drift-free `vTaskDelayUntil` sampling, Q15 filters, bit-exact check against an offline run, unit tests |
| 12 | [CLI task](projects/12-cli-task) | UART-RX ISR → stream buffer → CLI task, line editing, command table, mutex-protected parameters, task/heap introspection, unit tests |

Each project's README explains the concept, the tasks and priorities, the kernel
objects used, the real demo output, and what to look for in it.

## Repository layout

```
CMakeLists.txt            top-level build: adds every projects/NN-*/ in one tree
cmake/
    rtos_common.cmake     fetches FreeRTOS-Kernel (V11.1.0), rtos_add_freertos_app(),
                          rtos_add_demo_test(), rtos_add_unit_test(), warning flags
    run_demo.cmake        ctest driver: runs a demo, requires exit 0 AND an output regex
common/
    freertos/             shared FreeRTOSConfig.h, weak hook defaults, demo_util.h
                          (task-safe printf, DEMO_CHECK, RESULT: PASS/FAIL)
    test/minitest.h       tiny unit-test framework for host tests
projects/NN-<name>/
    CMakeLists.txt        builds the demo (plus unit tests, where there are any)
    src/                  main.c and any RTOS-independent modules
    tests/                host unit tests / stdin session (projects 11, 12)
    README.md             concept, design, output, takeaways
scripts/run_all.sh        configure + build + ctest in one command
```

## Building and running

You need a C compiler, CMake 3.16 or newer, and git. The first configure downloads
FreeRTOS-Kernel V11.1.0 with CMake `FetchContent`. Tested on Ubuntu with GCC.

```sh
# everything: configure, build, run all 14 tests (12 demos + 2 unit-test suites)
cmake -S . -B build
cmake --build build -j
(cd build && ctest --output-on-failure)

# or the same three steps in one go
scripts/run_all.sh

# run one demo from the full build
./build/projects/04-priority-inversion/priority_inversion
(cd build && ctest -R p04 --output-on-failure)

# build one project on its own
cmake -S projects/06-software-timers -B build/06
cmake --build build/06
./build/06/software_timers
```

For offline builds, point CMake at a local kernel checkout:
`cmake -S . -B build -DFREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel`.

The ctest summary should read `100% tests passed, 0 tests failed out of 14`.

## Conventions

- One FreeRTOS configuration (`common/freertos/FreeRTOSConfig.h`) is shared by every
  project: 1 kHz tick (1 tick = 1 ms), preemption and time slicing on, 10 priorities,
  and the timer service task at the top priority (9). A project can override options
  with its own `app_config.h`.
- Each demo links a private copy of the kernel built for the POSIX port with `heap_4`.
  Kernel sources compile with warnings silenced. Our own code uses
  `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror`.
- Tasks print only through `demo_printf` / `demo_log`. These wrap stdio in a critical
  section, so a task preempted while holding glibc's stdout lock cannot hang the
  simulator.
- A demo passes only if it exits with status 0 *and* its output matches the expected
  pattern (by default `RESULT: PASS`). A timeout in `run_demo.cmake` turns a hung demo
  into a failure.
- Demos are deterministic: fixed iteration counts, seeded pseudo-random data, and
  tick-based timing. Numbers that depend on the host (spin counts, µs benchmarks) are
  printed for information and are not checked.
- Algorithms that do not need the RTOS (filters, CLI parser) live in their own `.c`
  files with no FreeRTOS includes, and they are unit tested on the host.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Yuvaraj Durai.
