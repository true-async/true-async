# Question: does the event need methods at all? (2026-10-02)

Edmond doubts that the event needs a methods table. Analysis so far (reading, not run):

- In the fork the waker drives the events it waits on, without knowing their type: `start` at suspend
  (reference scheduler.c:1147), `stop` at wake, owns references and releases them (`dispose`), `replay` for a
  finished awaitable (async_API.c:609, scheduler.c:1138), `info` in the deadlock dump (scheduler.c:712, 733).
- Implementations: add/del are plain vector push/remove for every type except Future (late subscribe fires at
  once, future.c:527-530) and remote Future (sets `observed`, future.c:2288-2300, read only in its dispose
  :2322-2331). start/stop do real work only for reactor events (libuv); channel, iterator etc. `return true`.
  replay: Future, scope (coroutine directly). info: debug only. notify_handler: one user (timeout, async.c:1675).
- Already agreed: add/del are plain functions (the "has a subscriber" fact becomes a flag bit, read by remote
  Future's dispose); notify_handler goes (the timeout becomes a callback).

Proposed model: the waker keeps PASSIVE edges `{event, callback}` (needed for unsubscription on await_any,
timeouts, cancellation, and for the S7 wait-graph collector, DECISIONS 2026-10-01); it neither owns nor starts
events. Whoever creates an event for a wait starts it and releases it; every awaitable reachable from generic
code is a PHP object, so generic release is OBJ_RELEASE (object handlers give the per-type behaviour);
internal events without an object are awaited only by C code that knows their type. A completed awaitable keeps
its result in a known place, so a generic subscribe-to-completed delivers it at once (no replay method). info is
replaced by the class name. If this holds, the event header is `flags` + the waiters vector, no methods.
