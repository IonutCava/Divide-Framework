# Networking

<!-- Copyright (c) 2026 DIVIDE-Studio -->

## Scope and ownership

- `Source/Networking/Client.cpp` and `Headers/Client.h`: client context, context
  thread, connection ownership and heartbeat scheduling.
- `Source/Networking/Connection.cpp`: asynchronous socket reads and queued writes.
- `Source/UnitTests/Test-Platform/NetworkingTests.cpp`: loopback heartbeat tests.

## Invariants and safe changes

Heartbeat resets and sends run on the client's Asio context. Timer cancellation
and generation checks prevent obsolete waits from rearming the heartbeat.
Disconnect marks the client stopping, stops and joins its context thread, then
cancels the timer before releasing the connection.

Tests that exercise client sends must initialize the platform and localization
services explicitly, then destroy clients before clearing localization.
CTest runs each case in a separate process, so fixtures must own their setup.
Use loopback fixtures, asynchronous reads and bounded deadlines; avoid fixed
sleep-based timing assumptions.

## Known limitations

`Client::connect()` does not restart a stopped context or reset stopping/startup
state. Do not assume reconnecting a disconnected client is supported; a reconnect
change must cover context, connection and heartbeat lifecycle together.
