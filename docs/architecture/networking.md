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

Tests exercising actual client sends must call
`platformInitRunListener::PlatformInit()` before the send path accesses locale,
console and application-time services. CTest runs discovered cases in separate
processes: initialization in another test does not carry over. Keep loopback
fixtures independent of external servers and exercise cancellation and
per-client startup when changing heartbeat behavior.

## Known limitations

`Client::connect()` does not restart a stopped context or reset stopping/startup
state. Do not assume reconnecting a disconnected client is supported; a reconnect
change must cover context, connection and heartbeat lifecycle together.
