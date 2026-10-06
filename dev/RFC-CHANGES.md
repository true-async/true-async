# RFC changes

Requests to bukka's RFCs (IO hooks, Poll API additions, Ring), one topic per pull request, oldest
first. Each entry gives what the extension needs, the request, the code that waits for it, and the
state. Threads cannot reach php/php-src: Edmond sends each request himself.

## 1. Poll API additions: a C API for the notification descriptor pair

State: drafted 2026-10-06 (S4.5), not sent. PR: none.

Need: the reactor wakes from another thread (`dev/plans/S4.md` 3.6). The core builds the right
descriptor pair for `Io\Poll\NotifyHandle` (an eventfd, a pipe, a loopback socket pair on Windows,
`ext/standard/io_poll.c:693-725`) and raises it with the thread-safe `php_poll_notify()`, but only
behind the object: its class entry is static, its draining is the PHP method `clear()`, and the
handle dies with the request while a thread may still hold a trigger. So the extension keeps a pair
of its own per thread and mirrors that code: open, raise, drain, close.

Request: export the pair behind `NotifyHandle`, for example
`PHPAPI zend_result php_poll_notify_pair_open(php_poll_notify_pair *pair)`,
`php_poll_notify_pair_raise()` (thread-safe and async-signal-safe, as `php_poll_notify()`),
`php_poll_notify_pair_drain()` and `php_poll_notify_pair_close()`, with `NotifyHandle` built on them.
Alternative: a thread-safe `wake` entry in `php_io_queue_ops`, answered by the queue's wait as a
completion of its own, which would leave the pair out of the reactor.

Waits for it: the wake pair in `src/reactor.c`.
