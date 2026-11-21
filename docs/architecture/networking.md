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
console and application-time services. Platform initialization only sets up paths
and system information; `platformInitRunListener::LocalizationScope` also calls
`Locale::Init()` and clears localization after all clients are destroyed.
Networking tests opt into this shared helper after their explicit platform-init
call; unrelated tests do not acquire localization implicitly.
CTest runs discovered cases in separate
processes: initialization in another test does not carry over. Keep loopback
fixtures independent of external servers and exercise cancellation and
per-client startup when changing heartbeat behavior.
The fixture receives the expected packet count with asynchronous exact-length
header/body reads while advancing the client context. A shared one-second
deadline bounds the receive, and a timed-out read is cancelled and its completion
drained before local buffers are released. Do not assume queued writes or TCP
delivery complete within a fixed short delay, or that a readable header implies
the entire body has arrived.

## Known limitations

`Client::connect()` does not restart a stopped context or reset stopping/startup
state. Do not assume reconnecting a disconnected client is supported; a reconnect
change must cover context, connection and heartbeat lifecycle together.
