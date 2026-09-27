# 08 — Stream buffers and message buffers

This demo moves a 64 KiB byte stream through a stream buffer and 500 variable-length
messages through a message buffer. It checks integrity with CRC-32 and sequence
numbers, and shows how a stream buffer's trigger level affects when the reader wakes.

## RTOS concept

Stream and message buffers are **single-writer, single-reader** byte FIFOs built for
one task (or ISR) passing data to one other task. There is no fixed item size as with
queues.

- A **stream buffer** carries a continuous stream of bytes, so the boundaries between
  writes are *not* kept. A blocked reader wakes when at least the **trigger level**
  bytes are present, or when its timeout expires.
- A **message buffer** is a stream buffer that stores a length header in front of each
  message. Each receive returns exactly one whole message, so framing *is* kept.

Both use task notifications internally to unblock the reader and writer, so they are
cheaper than a queue. The price is that they are only safe with one writer and one
reader, unless you add your own lock.

## How the demo works

`Ctrl` runs at priority 4. In every part the reader (priority 2) has a higher priority
than the writer (priority 1).

**A. Stream buffer** — `xStreamBufferCreate(256, 32)`.
- `SWrite` generates 65536 pseudo-random bytes in chunks of 1–97 bytes and loops on
  `xStreamBufferSend` until each chunk is fully written.
- `SRead` reads up to 50 bytes at a time with a 50 ms timeout. For every read that
  started with an empty buffer (a blocking read), it records whether it was woken with
  fewer than 32 bytes.
- Both sides keep a CRC-32 and a byte count.

**B. Message buffer** — `xMessageBufferCreate(256)`.
- `MWrite` sends 500 messages of `{seq, len, data[len]}` with `len` from 1 to 60.
- `MRead` checks that every receive returns the next sequence number, exactly
  `header + len` bytes, and the expected payload pattern.

**C. Oversized message.** A 40-byte message is written and then read into a 16-byte
buffer. `xMessageBufferReceive` returns 0 and leaves the message in place, and
`xMessageBufferNextLengthBytes` still reports 40.

## Build and run

```sh
cmake -S projects/08-stream-message-buffers -B build/08
cmake --build build/08
./build/08/stream_message_buffers
(cd build/08 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target stream_message_buffers` and
`(cd build && ctest -R p08 --output-on-failure)`.

## Demo output

```
08-stream-message-buffers (FreeRTOS POSIX simulator)
[    62] A: stream  tx 65536 bytes crc=7e2451a5 | rx 65536 bytes crc=7e2451a5 in 1751 reads
[    62] A: reader blocked 977 times; woken with < 32 bytes 1 times
[    72] B: message buffer: 500/500 messages intact, 0 corrupted/misframed
[    72] C: 40-byte message into 16-byte buffer -> received 0, next message length 40
checks: 5, failures: 0
RESULT: PASS
```

The read and block counts can vary slightly between runs. The byte counts and CRCs
cannot.

## What to look for

- Matching CRCs and 65536 bytes on both sides, even though the writer used random chunk
  sizes and the reader used fixed 50-byte reads. A stream buffer does not care about
  write boundaries.
- `woken with < 32 bytes 1 times`: a blocked reader is woken only once the trigger level
  is reached. The single exception is the tail of the stream, where fewer than 32 bytes
  remain and the reader's timeout ends the wait.
- `500/500 messages intact`: every receive from the message buffer returned exactly one
  message, with its original length.
- Part C: an oversized message is never truncated. Your code has to handle the
  "receive returned 0" case, for example by using a bigger buffer.

## Key takeaways

- Use a **stream buffer** for byte streams such as UART data, audio or log text, and a
  **message buffer** for variable-length packets.
- The trigger level trades latency for efficiency. A higher level means fewer wake-ups
  but more delay before the reader sees data.
- A message buffer stores a length header per message, so its capacity in messages is
  less than `size / payload`.
- Single writer and single reader only. With several writers, put a mutex around the
  send, or use a queue.
