# RFC changes

Requests to the RFCs the core is built from (bukka's IO hooks, Poll API additions, Ring; the
scheduler RFC), one topic per pull request, oldest first. Each entry gives what the extension needs,
the request, the code that waits for it, and the state. Threads cannot reach php/php-src: Edmond
sends each request himself.

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

## 2. IO hooks: ECANCELED under a pending exception is not a stream failure

State: branch `io-hooks-fixes` (true-async/php-src `189b408d583`, on bukka's `608927ebe09`), PR text
handed to Edmond 2026-10-06. PR: none yet.

Need: a plain-wrapper read or write, or a socket read, whose op was abandoned for an exception (the
provider's `run()` failed, or `php_io_run_ex()` refused it under a pending exception) returns
`ECANCELED`, and the wrappers reported it as a failure: a notice on top of the exception and eof on a
stream that is not at its end, so the next `fgets()` on a pipe returned false.

Request: no notice and no eof for `ECANCELED` while `EG(exception)` is set, as the socket write path
and the TLS wrapper already do; `ECANCELED` without an exception stays an error. Two tests in
`ext/standard/tests/streams/hooks/`.

Waits for it: nothing; the pinned core carries the branch (`async-core-io-2026-10-06`).

## 3. IO hooks: stream_set_timeout() on a pipe

State: drafted 2026-10-06 (S6.3), not sent. PR: none.

Need: TrueAsync honours `stream_set_timeout()` on a `proc_open()` pipe for a read in a coroutine
(`io/039`, `040`, `042`, `043`). The plain wrapper has no `PHP_STREAM_OPTION_READ_TIMEOUT` case, so the call
returns false, as in vanilla PHP; a provider sees only the op's deadline, which the wrapper builds
without a timeout.

Request: a read timeout on a pipe stream that holds with and without a provider: store it in the
plain wrapper and, when set, wait with `php_io_poll(stream, fd, PHP_POLL_READ, &deadline)` before the
read, so the core's synchronous queue honours it too; `timed_out` in the metadata. A feature, not a
fix: vanilla PHP has no such option.

Waits for it: `io/039`, `040`, `042`, `043` (`--XFAIL--` naming S8).

## 4. IO hooks: stream_set_timeout() on stream_socket_recvfrom() and stream_socket_sendto()

State: drafted 2026-10-06 (S6.4), not sent. PR: none.

Need: TrueAsync honours a socket stream's timeout in `stream_socket_recvfrom()` and
`stream_socket_sendto()` and sets `timed_out` in the metadata (F `main/streams/xp_socket.c:368-392`,
called before `recvfrom()` and `sendto()`). The pinned core gives these transport calls an infinite
deadline on a blocking stream (`sock_xport_deadline()`, `main/streams/xp_socket.c:252-261`), as
vanilla PHP's blocking `recvfrom()` does, so a UDP receive without a peer never returns, with or
without a provider.

Request: `sock_xport_deadline()` builds the deadline from `sock->timeout`, as `php_sockop_read()` and
`php_sockop_write()` do (`tv_sec == -1` infinite), and `STREAM_XPORT_OP_RECV` and
`STREAM_XPORT_OP_SEND` set `sock->timeout_event` on `ETIMEDOUT`. A feature, not a fix: vanilla PHP
ignores the timeout there.

Waits for it: `stream/030` (`--XFAIL--` naming S8).

## 5. Poll API additions: zend_sigaction() leaves a SignalHandle's signals blocked

State: drafted 2026-10-06 (S6.5), not sent. PR: none.

Need: `Async\signal()` takes a signal through a SIGWAIT op on a number an `Io\Poll\SignalHandle`
blocks while a `Context` watches it (`dev/plans/S6.md` section 8). `zend_sigaction()` unblocks the
number it installs a handler for (`Zend/zend_signal.c:258-263`), so a `pcntl_signal()` after
`Async\signal()` lets the next delivery go to the handler alone, and the handle's record of blocked
numbers (`php_io_poll_signals_blocked_by_handles`, `ext/standard/io_poll.c:997-998`) no longer
matches the mask. pcntl's request shutdown and `pcntl_sigprocmask()` do the same. The extension
blocks the watched numbers again before every poll of its queue.

Request: `zend_sigaction()` (and pcntl's mask restore) leaves alone a number that a live
`SignalHandle` blocked, for example through a hook `ext/standard` sets, or a `PHPAPI` query
`php_io_poll_signal_blocked_by_handle(int signo)` that `zend_sigaction()` consults. Second, smaller:
a `PHPAPI` to block and unblock a handle's set without an `Io\Poll\Context`
(`php_io_poll_signal_handle_watch()` and `_unwatch()`, the `added`/`removed` ops of the handle), so a
provider needs no Context it never waits on.

Waits for it: `async_signal_reblock()` in `src/os_signal.c` and its call in `reactor_poll()`
(`src/reactor.c`); the Context of `async_signal_registry_t`.

## 6. Async core: a driver error under a pending cancellation keeps the cancellation

State: drafted 2026-10-07 (S6.6), not sent. PR: none.

Need: a cancellation that lands while a driver waits on IO (`pdo_mysql` connecting, here) makes the
provider fail the op with the `AsyncCancellation` pending; the driver then reports its own error and
PDO throws a `PDOException` over it, so `catch (AsyncCancellation)` in the coroutine no longer
catches the cancellation.

Request: an exception thrown while a cancellation is pending leaves the cancellation on top and
attaches the new one to it, as TrueAsync's fork does for PDO (`ext/pdo/pdo_dbh.c:40-89`,
`pdo_keep_pending_cancellation()`); either in PDO's error path or once in the core's exception
throwing, which would cover every extension. Edmond's call which.

Waits for it: `tests/pdo_mysql/029-cancel_during_connect.phpt` (`core:6` in `tests/lists/S6.txt`).

