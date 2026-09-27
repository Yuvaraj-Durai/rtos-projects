# 12 — Command-line interface task

An interactive command-line interface (CLI) running in its own FreeRTOS task. It is fed
byte by byte from a simulated UART receive interrupt through a stream buffer. The
commands drive simulated hardware (an LED), a mutex-protected parameter store, and
FreeRTOS introspection (task list, heap usage, uptime). The CLI core has no RTOS or
stdio dependency, and it is unit tested on the host.

## RTOS concept

- **ISR → task byte streaming** with `xStreamBufferSendFromISR()`, the natural way to
  handle UART receive.
- **One task owns the console.** Only the CLI task parses input and writes output, so
  command handlers run in ordinary task context.
- **Shared state protected by a mutex**, so other tasks could safely read the
  parameters.
- **Run-time introspection** with `uxTaskGetSystemState()`, `xPortGetFreeHeapSize()`,
  `xPortGetMinimumEverFreeHeapSize()` and `xTaskGetTickCount()`.

## How the demo works

- `main()` reads all of stdin (up to 4 KiB) into a buffer before starting the
  scheduler. This is the data that will "arrive on the UART".
- **UART RX ISR:** the tick hook sends one byte per tick into `rx_stream`
  (`xStreamBufferCreate(64, 1)`, trigger level 1).
- **`CLI` task (priority 2):** blocks in `xStreamBufferReceive()` (up to 16 bytes, 200 ms
  timeout) and feeds each byte to `cli_feed()`. It stops on the `exit` command, or when
  the input is used up and the buffer is empty (end of file), and then calls
  `vTaskEndScheduler()`.
- **`cli.c`** is an allocation-free line editor and dispatcher:
  - backspace/DEL editing, CR ignored, LF ends a line;
  - overlong lines (over 63 characters) are rejected, not truncated;
  - in-place tokenising with `"double quotes"` for grouping;
  - a static command table `{name, help, min_args, max_args, handler}` with argument
    count checks and usage messages.
- **Commands:** `help`, `echo`, `led [on|off|toggle]`, `set <param> <value>`,
  `get [param]`, `sum`, `uptime`, `tasks`, `heap`, `exit`. The parameters are `gain`,
  `offset` and `rate`, and access goes through `xSemaphoreTake/Give(params_lock)`.

After the scheduler stops, `main()` checks that at least one command ran and that the
session ended with `exit`.

The ctest entry pipes [`tests/session.txt`](tests/session.txt) into the program. It
requires exit status 0 and output that matches, **in session order**: `LED is ON`,
`sum = 60`, the unknown command `reboot`, `gain = 7`, the typo `lef`, then `> led off`
followed by `LED is OFF`, then `bye` and `RESULT: PASS`. The `> led off` line comes from
the input `lex<DEL>d off`, so it shows that DEL editing works end to end. The last line
of the session comes after `exit`, so it is never executed.

## Build and run

```sh
cmake -S projects/12-cli-task -B build/12
cmake --build build/12
./build/12/cli_task < projects/12-cli-task/tests/session.txt
./build/12/cli_task                   # interactive: type commands, then Ctrl-D
./build/12/p12_test_cli               # host unit tests for cli.c
(cd build/12 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target cli_task p12_test_cli` and
`(cd build && ctest -R p12 --output-on-failure)`.

`main()` reads stdin until end of file before the scheduler starts. In interactive mode,
nothing runs until you press Ctrl-D, and then the commands you typed are replayed
through the simulated UART.

## Demo output

```
FreeRTOS CLI ready. Type 'help'.
> help
  help     - list commands
  echo     <words...>
  led      [on|off|toggle]
  set      <param> <value>
  get      [param]
  sum      <n1> [n2...]
  uptime   - ms since boot
  tasks    - list FreeRTOS tasks
  heap     - FreeRTOS heap usage
  exit     - stop the simulator
> echo hello "from the CLI task"
hello from the CLI task
> led
LED is OFF
> led on
LED is ON
> led toggle
LED is OFF
> led toggle
LED is ON
> sum 10 20 30
sum = 60
> reboot
unknown command 'reboot' (try 'help')
> set gain 7
gain := 7
> set nope 1
set: bad parameter or value
> get gain
gain = 7
> get
gain = 7
offset = 0
rate = 100
> lef off
unknown command 'lef' (try 'help')
> led blink
led: expected on|off|toggle
> led off
LED is OFF
> uptime
uptime 159 ms
> tasks
name       prio state
CLI           2 run
IDLE          0 ready
Tmr Svc       9 block
> heap
heap free 1032424 bytes, minimum ever 1032424 bytes
> exit
bye
commands executed: 17, errors: 4, exit command: yes
checks: 2, failures: 0
RESULT: PASS
```

Unit tests:

```
[ RUN  ] tokenize_spaces_and_quotes
[ RUN  ] tokenize_errors
[ RUN  ] dispatch_and_arg_count
[ RUN  ] unknown_and_failing_commands
[ RUN  ] line_editing
[ RUN  ] overlong_line_is_rejected_not_truncated
[ RUN  ] help_lists_all
7 tests, 25 checks, 0 failures
```

## What to look for

- `uptime 159 ms`: the session up to and including the `uptime` line is exactly 159
  bytes, and the simulated UART delivers one byte per 1 ms tick.
- `tasks` shows the CLI task as `run` (it is the caller), the idle task `ready`, and the
  timer service task at priority 9 `block`ed.
- `errors: 4` counts `reboot` (unknown), `set nope` (bad parameter), `lef` (unknown) and
  `led blink` (handler error). `commands executed: 17` counts commands that were
  dispatched to a handler, including the two whose handler returned an error.
- The `> led off` echo for the input `lex<DEL>d off`: the line editor applied the DEL
  before the command was dispatched.

## Key takeaways

- Keep the CLI core free of the RTOS and I/O (it only has a write callback), so it is
  portable and unit testable. The RTOS glue is only the tick hook, one task and `main()`.
- A stream buffer with trigger level 1 is a good fit for a UART receive path: the ISR
  pushes bytes, and one task consumes them.
- Guard shared parameters with a mutex, even when only the CLI writes them today,
  because other tasks read them.
- Introspection commands (`tasks`, `heap`) cost little and are very useful when
  debugging on real hardware. On an MCU, `xPortGetMinimumEverFreeHeapSize()` is the
  number to watch.

### Test fix note

This demo's ctest used to fail even though the program printed `RESULT: PASS`. The
`PASS_REGEX` in `CMakeLists.txt` required `> led off` / `LED is OFF` to appear *before*
`sum = 60`. In `session.txt`, though, the backspace-edited `led off` comes near the end
of the session, after `sum`, `reboot` and `set gain`. CMake's `MATCHES` needs the
patterns in order, so it could never match. The regex now follows the session order.
It also checks the `lef` typo and the `bye` from `exit`.
