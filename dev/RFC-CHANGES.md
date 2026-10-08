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

State: drafted 2026-10-06 (S6.5), not sent. PR: none. The main part is in bukka's `io_hooks_poc`
since `bdfa5fa7a12` (2026-10-07), in the core from `async-core-io-2026-10-08-2`: `pcntl_signal()` and
`pcntl_sigprocmask()` leave a number a `SignalHandle` watches blocked, and `exec()` and friends start
the child with `php_io_poll_signal_child_mask()` (`signal/031`, `032` changed, DECISIONS 2026-10-08).
Still open: the `PHPAPI` to watch a handle's set without a Context, the record of the extension's own
block for the child mask, and the count of handles per number.

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

The scheduler RFC's core has the hook in the first form: `zend_sigaction()` asks
`zend_async_sigaction_fn` (`ZEND_ASYNC_SIGACTION`, `Zend/zend_signal.c` of true-async/php-src
`true-async`) whether the reactor owns the number, and leaves its own handler out when it does;
TrueAsync answers from `libuv_zend_sigaction()` (`libuv_reactor.c:1566`). The same hook over a
`SignalHandle` would close this one. It would also fix the children: the block the extension takes
again is in no record of the core's, so `php_io_poll_signal_child_mask()` leaves it to a
`proc_open()` child started while the watch lives (seen 2026-10-07, S6.9); a `PHPAPI` that records
such a block with the handle would do as well. The same record would let the extension leave a number
blocked while another `SignalHandle` of the thread still watches it (a script's own `Context`); today
its unblock at the watch's end cannot see the core's count of handles per number.

Waits for it: `async_signal_reblock()` in `src/os_signal.c` and its call in `reactor_poll()`
(`src/reactor.c`); the Context of `async_signal_registry_t`; `signal/032` records the delivery lost
to the handler before the next poll.

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

## 7. IO hooks: a failed run() frees what the provider delivered

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: a cancellation that lands after the queue completed an op (an accepted descriptor, an address
list, a reaped status, a taken signal) leaves the result with the provider. When `run()` returns
FAILURE, `php_io_run_ex()` overwrites the result with Cancelled (`main/io/php_io_hooks.c:995-1000`)
and frees nothing: the descriptor and the list leak. So the provider returns SUCCESS with the result
and the cancellation pending (`dev/plans/S6.md` 3.3, step 5), and the caller sees its call succeed
before the exception.

Request: on FAILURE after a delivered Done, `php_io_run_ex()` releases the result by op type (close
the accepted descriptor, `freeaddrinfo()` the list), as TrueAsync's wrapper does (F
`main/network_async.c:1572-1580`); then a provider can return FAILURE for every cancelled wait.

Waits for it: the Done branch at the end of `io_provider_run()` (`src/io_provider.c`),
`io_provider/009`.

## 8. IO hooks: unfreeze the streams of a bailed-out coroutine

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: a coroutine parked in `run()` when a fatal error unwinds the request leaves its stream frozen
(`PHP_STREAM_FLAG_IN_USE`) and `FG(io_ops_in_flight)` raised until RSHUTDOWN
(`main/io/php_io_hooks.c:472-498`), since its `php_io_frame_end()` never runs. Shutdown functions
then get "Concurrent access to a stream" on that stream, and `pcntl_fork()` refuses.

Request: a `PHPAPI` the scheduler calls after it has unwound every coroutine of a bailout: reset
`FG(io_ops_in_flight)` and unfreeze the streams whose op is not in `FG(io_orphans)` (a Ring op still
writing into a stream buffer keeps its stream frozen).

Waits for it: `io_provider/007` records today's behaviour (the shutdown function's read throws).

## 9. pcntl: block signals around the queue only, not around the handlers

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: `pcntl_signal_dispatch()` blocks every signal of the thread and the fiber switch while its PHP
handlers run (`ext/pcntl/pcntl.c:1411-1420`). A handler that suspends lets other coroutines run with
signals blocked; and pcntl's restore of the old mask unblocks a number a `SignalHandle` blocked
meanwhile (5 is the same family).

Request: pcntl blocks signals only while it takes entries off its queue, and runs the handlers with
the mask it found. The open question "A pcntl handler that waits" of `dev/PLAN.md` decides whether
this or a refusal of the suspend is wanted.

Waits for it: the open question in `dev/PLAN.md`; `async_signal_reblock()` (`src/os_signal.c`).

## 10. IO hooks: exec() and friends close through a WaitPid op

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: `exec()`, `system()`, `passthru()` and `shell_exec()` open the child with libc `popen()`
(`ext/standard/exec.c:123-125, 514-516`); their reads park through `php_io_read()`, but the stream
has no `child_pid`, so its close is `pclose()`, which blocks the thread in `waitpid()` (review M9,
11.2.7). TrueAsync waits for that child with a process event.

Request: open these children with `php_stream_popen()` (or record the pid), so the close is the
WaitPid op a provider parks on, as `proc_close()` already is.

Waits for it: nothing listed fails; a coroutine that `exec()`s a slow child stalls the thread at the
close.

## 11. IO hooks: a Flock op

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: `flock()` without `LOCK_NB` blocks the thread (review M10). Under a scheduler that is a
deadlock: the holder parks on a timer and never runs again to unlock. `ext/session/mod_files.c`
does the same for two coroutines with one session id.

Request: a Flock op: a work op on the Ring, and `LOCK_NB` with a Timer backoff through
`php_io_sleep()` as the fallback for a readiness-only queue (review M10).

Waits for it: `io/081`, `084` (`core:11` in `tests/lists/S6.txt`).

## 12. IO hooks: per-direction exclusion and close as cancel

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: a coroutine parked on a stream freezes the whole stream (review B1), so a second user throws
"Concurrent access to a stream": a writer beside a parked reader (full duplex), `fclose()` or
`proc_close()` of a stream another coroutine waits on, several acceptors on one listener. TrueAsync
allows all three.

Request: the three parts of review B1: a read side and a write side held separately at the stream
API, contention handed to the provider as a wait op, and `fclose()` completing every op pinned on
the stream as closed, with the descriptor's `close()` and the free at the last unpin.

Waits for it: `io/096`, `098`, `exec/025` (`core:12` in `tests/lists/S6.txt`).

## 13. IO hooks: an except set on the Ring

State: drafted 2026-10-07 (S6.8), not sent. PR: none.

Need: `stream_select()` and `socket_select()` give the members of the except set `PHP_POLL_PRI`
(`ext/standard/streamsfuncs.c:789, 808`, `ext/sockets/sockets.c:1405`). The Ring refuses PRI and
completes that member Unsupported, and the core then runs the whole ANY on its own Poll queue on the
thread (`php_io_any_member_unsupported()`, `main/io/php_io_hooks.c:917-936, 1001-1007`): the select
blocks every coroutine of the thread for its whole timeout.

Request: a PRI form on the Ring, or an Unsupported member answered by the core alone while the
provider waits for the others.

Waits for it: `io_provider/024` (`core:13` in `tests/lists/S6.txt`).

## 14. IO hooks: a readiness under a cancellation runs no syscall

State: drafted 2026-10-07 (S6.8), not sent. PR: none.

Need: `php_io_run_cancelled()` (`main/io/php_io_hooks.c:1094-1113`) lets a Done POLL through under a
pending exception, so a provider that returns SUCCESS for it sends the caller on to its syscall: a
pipe `fwrite()` writes its bytes while it throws (`php_io_file_op`, `:1642-1660`), and
`php_socket_wait_retry()` retries `send()` (`ext/sockets/sockets.c:677-682`). A Ready under the same
exception stops there. Our provider answers such a Done with FAILURE (S6.8); another provider would
not know to.

Request: `php_io_run_cancelled()` treats a Done POLL as cancelled, as its comment says ("nothing
more runs").

Waits for it: nothing listed; `io_provider/020` checks our provider's answer.

## 15. Scheduler API: the gc_new_coroutine slot takes a priority

State: done in `async-core` `6f30767dd7c` and `50cd33b0eec` (2026-10-07, Edmond agreed in the GC
thread); the RFC text is not changed yet. PR: none.

Need: a coroutine that fills the root buffer awaits the collector's run with its stack parked in the
middle of an opcode (`zend_gc_collect_cycles()`, `Zend/zend_gc.c`), and so does every coroutine that
finds the buffer full before the run starts. The slot also creates the destructor iterators of the
run and of the shutdown passes, which must keep their place in the queue, so the scheduler could not
tell the run apart and queued it at the tail: 100 000 coroutines passed `vm.max_map_count`.

Request: `zend_async_gc_new_coroutine_t` takes a `zend_coroutine_priority` (TrueAsync's enum,
`ZEND_COROUTINE_NORMAL`, `ZEND_COROUTINE_HI_PRIORITY`): HI for the run, NORMAL for the iterators; the
slot's comment asks a scheduler to run HI first. `ZEND_ASYNC_API_VERSION` 3. With it: the await
slot's caller holds a reference to the awaited coroutine and the slot takes none, and an internal
entry may set the coroutine's result (the GC run's count, which each waiter reads from the run it
awaited).

Waits for it: nothing; the pinned core carries it (`gc/025`).

## 16. Scheduler API: replacing a context's string-key value destroys the old value in place

State: fixed on `async-core` `b7c70909437` (2026-10-08, pushed on Edmond's word; it updates
php/php-src#22561), pinned in `async-core-io-2026-10-08` `662dfe91919`; found in S9.10 (the Critic).
The same commit fixes `zend_async_internal_context_set()` and `zend_async_context_destroy()`, which
released the store while the coroutine still pointed at it. Tested through the bridge
ext-scheduler-hook (`/mnt/project-files/s9/rfc16/`): php-src has no class to reach the userland
context. PR: none.

Need: `zend_async_context_entry_set()` replaces a string key's value with `zend_hash_update()`
(`Zend/zend_async_API.c:240`), which runs the old value's destructor while the bucket still holds the
old value and writes the new one after (`Zend/zend_hash.c:864-873`). A destructor that writes to the
same context corrupts the heap: `set()` calls of other keys that grow the table (eight fill the
initial eight-slot table that holds only the key) make the write land in the freed bucket array, and
the new array keeps a pointer to the freed old value; an `unset()` of the key frees the dying value a
second time. A destructor of an element of an array value that reads the key reads an array part-way
through being freed. The object-key path copies the new value in first and releases the old one after
(`:244-258`); TrueAsync's `context.c:77` has the string-key bug too.

Request: the string-key path replaces as the object-key path does: find the bucket, copy the new value
in, then release the old one; add a new entry only when the key is absent.

Waits for it: nothing; S9.11's `Context::set()` and its own test `context/017` pass on the pinned
core.

## 17. Scheduler API: no userland context for a coroutine whose object is being freed

State: not sent; found in S9.11 (the Critic). PR: none.

Need: a finished coroutine is still `ZEND_ASYNC_CURRENT_COROUTINE` while its object's `free_obj` runs
PHP code (a WeakMap value's destructor, in `zend_object_std_dtor`), and the engine frees the object
after `free_obj` whatever its refcount (`Zend/zend_objects_API.c`, `zend_objects_store_del`).
`zend_async_context_get()` called then (`ZEND_ASYNC_CONTEXT_SET(NULL, ...)` from a C extension) finds
`context` already released and mints a new one into a coroutine that is never released again: the
context and its values leak until the request's end. The extension refuses this window for
`coroutine_context()` and `current_coroutine()` (`context/020`); a C caller goes to the core directly.

Request: `zend_async_context_get()` returns NULL when the coroutine's object has `IS_OBJ_FREE_CALLED`,
as for a missing provider.

Waits for it: nothing in this repository.

## 18. IO hooks: overlapped proc_open() pipes on Windows

State: on `io-hooks-fixes` `01f279ee8d2` (2026-10-08, S6.10), in the core from
`async-core-io-2026-10-08-2`; PR text for bukka in
`/mnt/project-files/notes/s6-10/io-hooks-overlapped-pipes-pr.md`, Edmond opens it. Needs ior with
`ior_release_handle()` (true-async/ior `release-handle`, PR text for libior/ior in
`/mnt/project-files/notes/s6-10/ior-drop-foreign-packets-pr.md`). PR: none.

Need: an anonymous pipe takes no overlapped I/O, so on Windows a coroutine reading a `proc_open()`
pipe blocks the thread and `stream_select()` on pipes only polls (`dev/plans/S6.md` section 9).

Request: `php_io_overlapped_pipes`, set at MINIT by the extension that installs the provider, makes
the parent's end of each `'pipe'` descriptor an overlapped named pipe (libuv's `uv_spawn()` layout);
its reads, writes and select readiness go to the provider as ops with `PHP_IO_OP_F_PIPE`. A new queue
op `release()`, called through `php_io_queues_release()` right before `CreateProcessW()`, takes each
pipe passed to the child off the queue's completion port, so the child's own overlapped I/O does not
post there.

Waits for it: the switch in `PHP_MINIT_FUNCTION(true_async)` (`src/true_async.c`) and the drain of a
select's cancelled pipe poll in `io_provider_run()` (`src/io_provider.c`); `io_provider/028`-`035`.
