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

Waits for it: `io/039`, `040`, `042`, `043` (`--XFAIL--` naming S8.6).

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

Waits for it: `stream/030` (`--XFAIL--` naming S8.6).

## 5. Poll API additions: zend_sigaction() leaves a SignalHandle's signals blocked

State: drafted 2026-10-06 (S6.5), not sent. PR: none. The main part is in bukka's `io_hooks_poc`
since `bdfa5fa7a12` (2026-10-07), in the core from `async-core-io-2026-10-08-2`: `pcntl_signal()` and
`pcntl_sigprocmask()` leave a number a `SignalHandle` watches blocked, and `exec()` and friends start
the child with `php_io_poll_signal_child_mask()` (`signal/031`, `032` changed, DECISIONS 2026-10-08).
Closed by 19 (S10.3a): the record of the extension's own block for the child mask, and the count of
handles per number. Superseded by 20 on 2026-10-09 (S10.5), which carries what was left: the hook in
`zend_sigaction()` and the `PHPAPI` to watch a handle's set without a Context. Not to be sent.

Need: `Async\signal()` takes a signal through a SIGWAIT op on a number an `Io\Poll\SignalHandle`
blocks while a `Context` watches it (`dev/plans/S6.md` section 8). `zend_sigaction()` unblocks the
number it installs a handler for (`Zend/zend_signal.c:260-263`), so a `pcntl_signal()` after
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
`SignalHandle` would close this one. The record of the block the extension takes again, for the
children and the count of handles per number, is 19.

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
(`PHP_STREAM_FLAG_IN_USE`) and `FG(io_ops_in_flight)` raised until RSHUTDOWN, since its
`php_io_frame_end()` never runs. Shutdown functions then get "Concurrent access to a stream" on that
stream, and `pcntl_fork()` refuses. The request shutdown's own unfreeze walks the resource lists
(`main/io/php_io_hooks.c:514-521`, `560-564`), and mysqlnd takes its stream off both lists
(`ext/mysqlnd/mysqlnd_vio.c`, `mysqlnd_fixup_regular_list`), so a mysqli connection is never
unfrozen. S8.2 (review B2): on the Poll queue a fatal error while a mysqli query is parked leaves
the connection frozen: `$mysqli->close()` in a shutdown function throws "Concurrent access to a MySQL
connection", the free at the request's end prints "Fatal error: Concurrent access to a stream in
Unknown" and the connection leaks (LeakSanitizer). On the Ring the close works: the stream is not
busy while its op is an orphan, and the free drains the orphan first.

Request: the core keeps the set of streams frozen or pinned by an op in progress: its open frames,
and the members `stream_select()` and `socket_select()` freeze around their `php_io_run()` without a
frame (`ext/standard/streamsfuncs.c:836-842`). A `PHPAPI` the scheduler calls after it has unwound
every coroutine of a bailout resets `FG(io_ops_in_flight)` and, for each stream of that set whose op
is not in `FG(io_orphans)`, unfreezes it and drops the sides and pins of entry 12 that the abandoned
frames held, so a close deferred to the last unpin runs (a Ring op still writing into a stream buffer
keeps its stream until its completion arrives). The request shutdown's unfreeze walks the same set
instead of the resource lists.

Waits for it: `io_provider/007` records today's behaviour (the shutdown function's read throws);
`mysqli/013` (`core:8` in `tests/lists/S8.txt`).

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

State: drafted 2026-10-07 (S6.7), not sent. Done on Unix by bukka's `bdfa5fa7a12` (in the core from
`async-core-io-2026-10-08-2`), noticed on 2026-10-09 (S10.5). The exec family now opens its child
with `php_stream_popen()`, which records the child's pid, and the close waits through
`php_io_waitpid()` (`ext/standard/exec.c:130`, `523`; `main/streams/plain_wrapper.c:471-536`,
`794-806`). The Windows part is 21. PR: none.

Need (before `bdfa5fa7a12`): `exec()`, `system()`, `passthru()` and `shell_exec()` open the child with libc `popen()`
(`ext/standard/exec.c:123-125, 514-516`); their reads park through `php_io_read()`, but the stream
has no `child_pid`, so its close is `pclose()`, which blocks the thread in `waitpid()` (review M9,
11.2.7). TrueAsync waits for that child with a process event.

Request: open these children with `php_stream_popen()` (or record the pid), so the close is the
WaitPid op a provider parks on, as `proc_close()` already is.

Waits for it (before `bdfa5fa7a12`): nothing listed fails; a coroutine that `exec()`s a slow child
stalls the thread at the close.

## 11. IO hooks: a stream lock that waits sleeps through the provider's timer

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: `flock()` without `LOCK_NB` blocks the thread (review M10). Under a scheduler that is a
deadlock: the holder parks on a timer and never runs again to unlock. `ext/session/mod_files.c`
does the same for two coroutines with one session id.

Request: as TrueAsync does (its `main/streams/plain_wrapper.c:1298-1390`, `:1473-1487`): a plain-file
stream lock that can wait (no `LOCK_NB`, not `LOCK_UN`) takes the async path only while hooks are active
(`php_io_hooks_active()`): it tries `LOCK_NB` and, while the lock is busy, sleeps through `php_io_sleep()`
and tries again, the sleep doubling from 1 ms to 100 ms and then starting over at 1 ms, since a waiter
that stays at the longest sleep keeps losing the lock to newer ones. The stream is pinned while it
sleeps; a close during the sleep ends the wait without a lock, since the number may name another file
by then; a cancellation during the sleep ends it without a lock too. Without hooks the lock blocks as
today, so a process without async still queues in the kernel. The path is `php_stream_lock()`, so
`flock()`, `file_put_contents(..., LOCK_EX)` and `SplFileObject::flock()` share it;
`ext/session/mod_files.c:207` calls libc `flock()` itself and stays out, as in TrueAsync. A blocking
work op on the Ring is not asked for: waiters beyond the pool take every thread and stall the file IO
queued behind them (`io/084`; TrueAsync's reason, php-async#221), and a cancelled waiter's op would be
orphaned and take the lock after the cancel, so the next locker waits for a coroutine that is gone
(`io_provider/075`); inferred from how the core orphans a cancelled Ring op (review B2, B3 rows), not run.

Waits for it: `io/081`, `084` (`core:11` in `tests/lists/S6.txt`); `io_provider/072`, a holder parked
on a timer on the Poll queue, and `075`, a cancelled waiter (`core:11` in `tests/lists/S8.txt`). S8.4
measured the hang on both queues, on debug and ASAN.

## 12. IO hooks: per-direction exclusion and close as cancel

State: drafted 2026-10-07 (S6.7), not sent. PR: none.

Need: a coroutine parked on a stream freezes the whole stream (review B1), so a second user throws
"Concurrent access to a stream": a writer beside a parked reader (full duplex), `fclose()` or
`stream_socket_shutdown()` of a stream another coroutine waits on, several acceptors on one listener,
a read of a member of a parked `stream_select()`, two writers on one pipe, even `feof()` and
`stream_get_meta_data()`. `proc_close()` does not throw: its resource destructor skips a busy pipe
(`main/streams/streams.c:1801-1807`), so the pipe stays open and the parked read ends only when the
child closes its end. TrueAsync allows all of these. S8.2 measured each on the Ring and on the Poll
queue (review section 9).

Request: the three parts of review B1: a read side and a write side held separately at the stream
API, contention handed to the provider as a wait op, and `fclose()` completing every op pinned on
the stream as closed. A read completed as closed returns false with `eof` set and no notice from
either wrapper (the plain wrapper's read prints one for any other errno,
`main/streams/plain_wrapper.c:718-738`). Only the free waits for the last unpin: the closer waits until
the pinned ops have settled (at once on the Poll queue, on the Ring until their cancel completions
arrive) and runs the descriptor's `close()` in its own context, so `pclose()` and `proc_close()` still
return the child's status (`FG(pclose_ret)`, `ext/standard/file.c:885-892`). A close from a resource
destructor (`zend_list_close()` from `proc_close()`, the resource lists' free at the request's end)
is the same close. `stream_socket_shutdown()` takes neither side and does not wait; a read parked on
the shut-down direction completes with end of file. `feof()` and `stream_get_meta_data()` take no
side. `stream_socket_accept()` takes no side: several accepts park on one listener. The freezes
outside the stream API pin their members and take no side: `stream_select()`
(`stream_array_freeze()`, `ext/standard/streamsfuncs.c:778`) and `socket_select()`
(`php_socket_select_freeze()`, `ext/sockets/sockets.c:1295`) neither wait for a parked op nor make
one wait; the plain wrapper hands out its descriptor only when no op is left
(`main/streams/plain_wrapper.c:211`). `php_io_stream_busy()` keeps meaning "an op is parked on the
stream"; the close paths stop calling it (`main/streams/streams.c:309`, `1803`), and its mysqli callers
keep it (`mysqlnd_conn_is_busy()`, `ext/mysqlnd/mysqlnd_connection.c:2342`, used at
`ext/mysqli/mysqli_api.c:279`, `1479` and `ext/mysqli/mysqli_nonapi.c:143`): `$mysqli->close()` beside a
parked query stays refused ("Concurrent access to a MySQL connection"), since the parked query reads
the connection's `MY_MYSQL` after it resumes and a close frees it. Until entry 30 lands, a TLS stream
with an op parked inside an `SSL_*` call keeps every other user refused (both sides, `feof()`, whose
check runs `SSL_peek()`, and the close): its BIO reads one wait state per stream, which each `SSL_*`
call sets and clears (`ext/openssl/xp_ssl.c:441-456`, `3288`, `3335`, `3693-3697`). Accepts on a TLS
listener follow this entry: the handshake runs on the new client's state (`:3525-3553`).

Waits for it: `io/096`, `098`, `exec/025` (`core:12` in `tests/lists/S6.txt`); `io_provider/042`-`059`
(`core:12` in `tests/lists/S8.txt`); `io_provider/078` once entry 30 lands too (`core:30`).

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

State: on `io-hooks-fixes` `424116620a7` (2026-10-08, S6.10), in the core from
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

## 19. IO hooks: the script's unblock of a watched signal at the last removal

State: on `io-hooks-fixes` `2a74924668c` (2026-10-08, S10.3a), in the core from
`async-core-io-2026-10-08-4`; PR branch `signal-unblock-at-removal` `c44eccf72a` on bukka's
`566a6833eb5`, PR text in `/mnt/project-files/notes/signal-unblock-at-removal-pr.md`, Edmond opens
it. PR: none.

Need: bukka's `bdfa5fa7a12` keeps a watched number blocked under `pcntl_sigprocmask()` and
`pcntl_signal()` but drops the script's unblock, so a number the script blocked before the watch
stays blocked after it, and a `pcntl_signal()` handler for it never runs (`dev/plans/S10.md`
section 4 (b)). The extension's own record of the numbers it blocked again unblocked a number another
`SignalHandle` still watched (section 4 (c)).

Request: `php_io_poll_signal_unblock_at_removal()` records the script's unblock for the last removal,
`php_io_poll_signal_keep_blocked_at_removal()` takes it back on a later block, and
`php_io_poll_signal_reblocked()` records an unblock by `zend_sigaction()` outside pcntl that no
script request takes back. `$old` stays the real mask. Two questions go with it: an unblock
followed by a save-and-restore, and a block during the watch of a number the handle blocked itself.

Waits for it: `async_signal_reblock()` in `src/os_signal.c`; `signal/031`, `034`, `035`.

## 20. Zend signals: a hook that keeps a SignalHandle's numbers blocked

State: drafted 2026-10-09 (S10.5), not sent. PR: none. Replaces what is left of 5 (`dev/plans/S10.md`
section 4, Edmond 2026-10-08).

Need: a `SignalHandle` owns a number by keeping it blocked, and every `zend_sigaction()` unblocks
the number it installs a handler for (`Zend/zend_signal.c:260-263`). bukka's `bdfa5fa7a12` and 19
cover pcntl's calls, which block the number again or filter it out. Three unblocks stay uncovered
(`dev/plans/S10.md` section 4 (a)): the instant inside `pcntl_signal()` between `zend_sigaction()`'s
unblock and pcntl's block; pcntl's request shutdown (`ext/pcntl/pcntl.c:242-246`), harmless only
while the extension's RSHUTDOWN runs first; phpdbg and third-party callers of `zend_sigaction()`. A
delivery in such a window goes to the Zend handler and never reaches the handle.

Request: a hook in `Zend/zend_signal.c`, filled by `ext/standard`, that `zend_sigaction()` asks
before its unblock, so that a number a live `SignalHandle` blocks stays blocked. pcntl's own block in
`pcntl_signal()` and its filter in `pcntl_sigprocmask()` then use the same hook. Second,
optional part: a `PHPAPI` to block and unblock a handle's set without an `Io\Poll\Context`
(`php_io_poll_signal_handle_watch()` and `_unwatch()`, the `added` and `removed` ops of the handle),
so a provider keeps no Context it never waits on (section 4 (d)). The scheduler API gets no hook: a
second owner beside the handle's count would not cover a script's own `SignalHandle` (a departure from
TrueAsync's `zend_async_sigaction_fn`, Edmond 2026-10-08).

Waits for it: `async_signal_reblock()` in `src/os_signal.c` and its call in `reactor_poll()`
(`src/reactor.c`) go once the hook is in the core; the optional part removes the Context of
`async_signal_registry_t`.

## 21. IO hooks: exec() and friends on Windows through overlapped pipes and WaitPid

State: drafted 2026-10-09 (S10.5), not sent. PR: none. Needs 18.

Need: on Windows `exec()`, `system()`, `passthru()` and `shell_exec()` open the child with TSRM's
`popen_ex()`, an anonymous pipe (`TSRM/tsrm_win32.c:447-590`, `ext/standard/exec.c:122`, `516`). A
read spins on `PeekNamedPipe()` with `usleep(10)` for up to 3.2 million rounds, each a
waitable-timer sleep of at least the timer's resolution, and reads only what is there
(`main/streams/plain_wrapper.c:668-681`), so the thread blocks until the child writes or exits; the
close is TSRM's `pclose()`, which waits in `WaitForSingleObject(INFINITE)`
(`TSRM/tsrm_win32.c:594-612`). A coroutine that runs a slow command blocks the thread in each read
and at the close. On Unix this is done (10). TrueAsync runs these children through libuv's process
and pipe handles.

Request: with `php_io_overlapped_pipes` set (18), the exec family opens its child as `proc_open()`
does: the parent's end an overlapped named pipe whose reads are provider ops, the pipe released from
the queue before `CreateProcessW()`, the pid recorded in the stream, and the close a
`php_io_waitpid()`, which on Windows takes any pid (`main/php_io_hooks.h:360-363`).

Waits for it: `exec/004`, `005`, `008`, `009` skip on `*-win` in `tests/lists/S6.txt`.

## 22. IO hooks: a console read on Windows

State: drafted 2026-10-09 (S10.5), not sent. PR: none.

Need: on Windows a read of `STDIN` from a console is a plain `read()`
(`main/streams/plain_wrapper.c:704`), since a console handle takes neither overlapped I/O nor a
readiness wait through the completion port. `fgets(STDIN)` in one coroutine blocks the thread, and
every other coroutine stops until the user presses Enter. TrueAsync reads the console through
libuv's TTY handle, which reads on a Windows thread-pool thread (`QueueUserWorkItem()`,
`libuv/src/win/tty.c`).

Request: a console read op, a work op in ior that runs `ReadConsoleW()` (or `ReadFile()` on the console
handle) on its pool and completes on the queue, used by the plain wrapper for a console descriptor
when a provider is installed. A cancelled read ends as libuv ends one: an Enter key event written to
the console input releases the worker's `ReadConsoleW()` (`uv__cancel_read_console()`,
`libuv/src/win/tty.c:1116-1169`); it also submits what the user had typed of the line, which the
cancelled read then drops.

Waits for it: nothing listed; a console CLI script that reads input stalls its coroutines.

## 23. IO hooks: DNS in ext/sockets and php_pollfd_for_ms() waits in ftp and pgsql (review M9)

State: drafted 2026-10-09 (S10.5), not sent. PR: none.

Need: two kinds of wait outside the hooks block the thread inside a coroutine (review M9,
`dev/plans/S10.md` section 9). `ext/sockets` resolves names with libc `getaddrinfo()` and
`php_network_gethostbyname()` (`ext/sockets/sockaddr_conv.c:51`, `109`, `ext/sockets/sockets.c:3992`),
so `socket_connect()` to a host name and `socket_addrinfo_lookup()` block on DNS. `ext/ftp`,
`pg_socket_poll()`, `PDO::pgsqlGetNotify()` and `Pdo\Pgsql::getNotify()` (both
`pgsqlGetNotify_internal()`) wait with `php_pollfd_for_ms()` and a timeout (`ext/ftp/ftp.c:313`,
`1390`, `1413`, `1518`, `1820`, `ext/pgsql/pgsql.c:468`, `ext/pdo_pgsql/pgsql_driver.c:1292`), which
is plain `poll()` (`main/php_network.h:197-206`); `getNotify()` with a 30 s timeout stops every
coroutine for up to 30 s.

Request: `ext/sockets` resolves through `php_io_getaddrinfo()`; the callers in `ext/ftp`,
`pg_socket_poll()` and `pgsqlGetNotify_internal()` wait through `php_io_poll()` (or
`php_io_poll_tv()`). These are the waits on an idle connection; the query waits of both pgsql drivers
are a later php-src PR of ours (PLAN Fog). The helpers stay `poll()` (DECISIONS 2026-10-09), and
`php_poll2()` stays as well: it is the zero-timeout probe of the Ring's ANY and of `stream_select()`.
`mysqli_poll()` (`select()`, `ext/mysqlnd/mysqlnd_connection.c:2258`) is a separate request.

Waits for it: `socket/004` stays in `tests/lists/S10.excluded` (it needs the network and cannot show a
blocking resolution); no listed test covers ftp or pgsql.

## 24. Scheduler API: Fiber::resume() and Fiber::throw() refuse once async is off

State: done in `async-core` `1e81baa84bf` and `5990cf26276` (2026-10-09, S8.1a, pushed on Edmond's
word), in the core from `async-core-io-2026-10-09-2`. PR: none; it goes with php/php-src#22561.

Need: a bailout out of main's finish in the last from_main call leaves the coroutines parked to the
request's end (U6), an adopted fiber's included. `Fiber::resume()` or `Fiber::throw()` in an output
handler, which runs after `ZEND_ASYNC_DEACTIVATE`, then enqueues the fiber's coroutine and calls
`zend_fiber_await()` with no current coroutine (`Zend/zend_fibers.c:1554-1555`, `1591-1592`, `997`
in `async-core-io-2026-10-09-1`): an assertion failure in a debug build. A release build, read from
the code and not run, leaves the coroutine in a run queue nobody drains and throws the scheduler's
Error "There is no coroutine to suspend", not a FiberError. The review's scheduler item 2 asks for a guard in `ZEND_ASYNC_SUSPEND()`;
our scheduler refuses there already, and the fiber methods are the one core caller that reaches it.

The same holds in a session save handler and another extension's RSHUTDOWN, and under READY after
the scheduler's last pass (a user `__toString()` of the last uncaught print). Without a bailout the
fiber is closed by then and `resume()` throws "Cannot resume a fiber that is not suspended".

Request: in `Fiber::resume()` and `Fiber::throw()`, first in the branch of a fiber with a coroutine
(`fiber->coroutine != NULL`), after the check that the fiber is suspended and before the enqueue,
throw the FiberError "Cannot resume a fiber after the scheduler has stopped" while
`!ZEND_ASYNC_IS_ACTIVE`, and "Cannot switch fibers in current execution context" when the current
coroutine is missing or finished, which `zend_fiber_await()` needs. The fiber still reports
`isSuspended()`. A plain fiber stays untouched: after the deactivation every new fiber is one
(`zend_fiber_adopt()`), and output and session handlers may use it.

Also: the destructor of a fiber with a coroutine detached it and left it SUSPENDED; a destructor
that kept the object (a collected cycle) reached the legacy `resume()`, which switched to a context
the fiber never had (an assertion, a crash in a release build). The fiber is now DEAD at once, as a
plain fiber the destructor closes; its body's cancellation still runs later on the scheduler.

Waits for it: nothing; the pinned core carries it. `fiber/036`, `037` and `039` check it, `fiber/038`
that a plain fiber in an output handler keeps working.

## 25. Scheduler API: the lifetime and hand-off of a fiber run as a coroutine

State: done in `async-core` `b52706c85f5`, `8c0d98dd7e0` and `eb84ae62df1` (2026-10-09, S8.1b), in the
core from `async-core-io-2026-10-09-3`. PR: none; it goes with php/php-src#22561.

Need: three paths of a fiber run as a coroutine went wrong where a legacy fiber cannot. A `resume()`
from a second coroutine, while the first waited for the fiber's yield or had not yet taken it, took
the fiber over, and the first waited for good (`fiber/040`, `043`). A second `start()`, after the
starter was cancelled while the body waited in the queue, adopted the fiber again and ran the body
twice (`041`). A body waiting outside `Fiber::suspend()` kept `EG(active_fiber)` in its saved
context, and when the starter, the fiber's last holder, was cancelled, the destructor freed the
fiber under it: a heap-use-after-free in `Fiber::getCurrent()` (`042`). The transfer slot leaked a
value nobody took (`045`), a cancellation that ran the body after its yield woke the waiter a
second time with the error (`046`), and a body first run after its starter unwound linked its
backtrace to the dead frame (`047`).

Request: `resume()` and `throw()` mark the fiber RUNNING, as a legacy switch does. The yield moves
the waiting caller to a new field `handoff_coroutine`, kept until that caller has taken the value,
and `resume()`, `throw()`, `isSuspended()` and `isRunning()` count both fields as the legacy path
counts `caller`; a value the slot releases is detached first and released after the state change.
`start()` refuses a fiber that already has a coroutine. The destructor of a fiber
whose body runs keeps the object until the body ends (since S14.6 a flag the coroutine entry releases,
not a finish handler; entry 33), and the body's
`Fiber::suspend()` refuses as in a force-closed fiber (`044`). TrueAsync's `resume()` (its
`zend_fibers.c:1043-1078`) has none of these checks; read, not run.

Waits for it: nothing; the pinned core carries it.

## 26. IO hooks: socket_accept() returns a freed Socket when its operation throws

State: on `io-hooks-socket-accept-exception` `73c612a426d` (on bukka's `8e470c219fc`) and
`io-hooks-fixes` `9458268fe1e`, pushed 2026-10-12 (S8.3); PR text for bukka in
`/mnt/project-files/notes/s8-3/io-hooks-socket-accept-exception-pr.md`; Edmond opened it on
2026-10-12. In the core since `async-core-io-2026-10-12-1` `36050e31325` (S8.4a). PR:
https://github.com/bukka/php-src/pull/7.

Need: a coroutine cancelled while it waits in `socket_accept()` kills the process: the function
releases the new `Socket` and returns through `RETURN_THROWS()` with it still in `return_value`
(`ext/sockets/sockets.c:1710-1714`), and the VM releases it again, an assertion failure in
`zend_objects_store_del()` on debug and ASAN, on the Ring and on the Poll queue. A userland provider
whose `run()` throws shows it without an extension.

Request: clear `return_value` before `RETURN_THROWS()`, as `zend_builtin_functions.c:898-900` does;
test `ext/sockets/tests/socket_io_hooks_accept_throws.phpt`.

Waits for it: nothing: `io_provider/067`, `068` pass on the core that carries it.

## 27. IO hooks: an accept or a connect cancelled by an exception reports nothing else

State: requested 2026-10-12 (S8.3), no branch yet. PR: none.

Need: a coroutine cancelled in `stream_socket_accept()` gets the cancellation and also "Warning:
stream_socket_accept(): Accept failed: Operation canceled" (`ext/standard/streamsfuncs.c:332`); one
cancelled in `stream_socket_client()` gets "Warning: stream_socket_client(): Unable to connect to ...
(Operation canceled)" (`:183`), on the Ring and on the Poll queue. On a listener in the stream exception
mode (`'error_mode' => StreamErrorMode::Exception`) the accept's report is a StreamException, which
replaces the cancellation and keeps it only as its previous, so `catch (Async\AsyncCancellation)` misses
it and the coroutine ends failed. TrueAsync warns too, for an accept with the cancellation's message
(its `main/network_async.c:1264-1272`), for a connect with the errno's text (`:1387-1391`); read, not run.

Request: as entry 2 for reads and writes: `stream_socket_accept()`, `stream_socket_client()` and
`fsockopen()` (`ext/standard/fsock.c:121`) report nothing when an exception is pending after the
failed call, whatever errno says; without an exception the report stays.

Waits for it: `io_provider/065`, `066`, `071` (`core:27` in `tests/lists/S8.txt`). The connect half has
no test here (a pending connect needs a network that drops the SYN); the fix's own tests throw from a
userland provider's `run()` on the Connect, as `socket_io_hooks_accept_throws.phpt` does (S8.3b).

## 28. IO hooks: a late remove() of a closed descriptor clears the number's next registrations

State: on `io-hooks-curl-late-remove` `5cd8893ec1a` (on bukka's `8e470c219fc`; force-pushed on Edmond's
word, 2026-10-12 17:04 to put back bukka's two comments the fix had reworded, then to name the kept
exception `callback_exception` as TrueAsync's `curl_async.c` does) and `io-hooks-fixes` `b9bc37e11e3` with
`466bdd66486` (the same comments), pushed 2026-10-12 (S8.4a, S8.7), in the core since
`async-core-io-2026-10-12-1` `36050e31325`, the comments since `async-core-io-2026-10-12-3` `634cbbf94be`;
the rename is `1c8bb39b882` on `io-hooks-fixes-callback-exception`, for the next core update; PR text
for bukka in `/mnt/project-files/notes/s8-4a/io-hooks-curl-late-remove-pr.md`. PR: bukka/php-src#8 (Edmond,
2026-10-12).

Need: on the Poll queue, a stream that takes the number of a socket libcurl just closed has its record
freed under it. On `CURL_POLL_REMOVE` the curl callback only invalidates the socket's handle
(`ext/curl/interface.c:2535-2541`), libcurl closes the socket, and the registrations end later, in the
reconcile after libcurl returns (`:2590-2597`). A PHP callback libcurl runs in between (`073` uses the
debug callback) may open a descriptor, get the same number, and wait on it: the Poll queue finds the
record by descriptor (`main/io/php_io_queue_poll.c:544`) and the stream's registration shares curl's.
The late `remove()` clears the direction for both (`:563-583`) and, with nothing left, frees the record
(`:165-176`, `:192-193`); the stream's next wait reads it (`:336`). `io_provider/073`: a heap use after
free on ASAN with `USE_ZEND_ALLOC=0`, a hang on debug and on ASAN with ZendMM; the timed probe
`/mnt/project-files/s8/probes/s8.4/curl_socket_number_reused.php` gives a SIGSEGV on debug. The Ring
takes no Level registrations and is unaffected (`074`). Any provider that keys by descriptor (a userland
`Io\Hooks` provider) meets the same late `remove()`; the review's section 6 item 7.

Request: the curl callback ends a removed socket's registrations itself, at `CURL_POLL_REMOVE` and so
before libcurl closes the socket, through a `remove()` that cannot throw (a provider's exception there
is kept for the reconcile to report), as the core already does before a close (`main/network.c:1121`).
Not asked: per-registration counts in the Poll queue; it would fix one queue, and `remove()` also
clears the record's `ready` and `hup` (`:577-580`), which a count alone does not keep.

Waits for it: nothing: `io_provider/073` passes on the core that carries it.

## 29. IO hooks: mysqli_poll() waits through the provider

State: requested 2026-10-12 (S8.5), no branch yet. PR: none.

Need: `mysqli_poll()` waits with `select()` (`ext/mysqlnd/mysqlnd_connection.c:2258`), so a coroutine
that polls its async queries stops every coroutine of the thread for the whole timeout: with the
server's answer held until another coroutine acts, the query's lock wait runs out first (`mysqli/018`
prints "poll: 1", "one: 0" before "main: releasing"). Entry 23 left it for a separate request; TrueAsync has the same
`select()` (its `mysqlnd_connection.c:2272`), read, not run (review M9).

Request: `mysqlnd_poll()` waits through the provider on the connections' streams, an Any of reads
(`php_io_op_any()` and `php_io_run()`, as `stream_select()` does, `ext/standard/streamsfuncs.c:985-986`)
with the call's timeout, and keeps `select()` when no provider is installed. The error array adds no
member to the Any (an except set blocks the thread on the Ring, entry 13) and comes back empty, as
`select()` reports a connection without out-of-band data.

Waits for it: `mysqli/018` (`core:29` in `tests/lists/S8.txt`).

## 30. IO hooks: a TLS stream waits outside OpenSSL

State: requested 2026-10-12 (S8.5), no branch yet. PR: none.

Need: a TLS read under a provider parks inside `SSL_read()`: the BIO calls `php_io_recv()` and
`php_io_send()` (`ext/openssl/xp_ssl.c:441-444`). Today B1's freeze keeps a second coroutine out
(`io_provider/078` gets "Concurrent access to a stream"); with entry 12's per-direction sides a write
beside the parked read would call `SSL_write()` while `SSL_read()` is suspended on the same `SSL *`,
a re-entry OpenSSL does not support (review M7); inferred, not run. Certain from the code: the BIO's wait
state is one per stream (`deadline`, set and cleared by each call, `:3288`, `:3335`), so a write that
returns while a read is parked leaves the read's next BIO call without a deadline; it runs a plain
`recv()`, gets `EAGAIN`, and `SSL_read()` retries at once (`:300-306`): the thread spins until the
record's rest arrives, which in one thread may wait on a coroutine that cannot run. TrueAsync waits with
`php_pollfd_for()` after `SSL_ERROR_WANT_READ`/`WANT_WRITE`, outside any `SSL_*` call (its
`ext/openssl/xp_ssl.c:3060`, `3240`), so the write goes through; read, not run.

Request: the BIO sets its retry flag (`BIO_set_retry_read()`, `BIO_set_retry_write()`) instead of
waiting, and the stream waits through the provider for the direction OpenSSL asked for between `SSL_*`
calls; the state a call reads (`deadline`, `io_cancelled`, `last_status`) belongs to the call, not to
the stream. Entry 12 applies to TLS streams with this entry, not before. Without entry 12
nothing changes for scripts.

Waits for it: `io_provider/078` (`core:30` in `tests/lists/S8.txt`), which needs entry 12 too.

## 31. IO hooks: O_NONBLOCK only on descriptions PHP opened

State: requested 2026-10-12 (S8.5, from `dev/PLAN.md` request A, reproduced 2026-10-10). PR: none.

Need: once hooks are active, the plain wrapper sets `O_NONBLOCK` on any unseekable descriptor it
reads or writes (`main/streams/plain_wrapper.c:543-566`) and clears it only when the stream closes
(`:569-582`). On `php://stdin`, `stdout`, `stderr`, `php://fd` and the stdio constants that changes a
description other processes share: a child that inherits it reads `EAGAIN` (`fgets(STDIN);
passthru('cat')` makes cat exit 1; `io_provider/079` shows the flag in a grandchild) (review section 5,
"A TTY switched to non-blocking").

Request: `O_NONBLOCK` only on descriptions PHP opened itself (pipes of `proc_open()`, `popen()`,
FIFOs it opened); on the others, wait for readiness before a read and write as entry 32 says, cut
to `PIPE_BUF`. Any stream's descriptor handed to a child (a `proc_open()` descriptor spec,
`ext/standard/proc_open.c:1239`) becomes one of the others: today both casts keep the flag, the
plain wrapper's (`main/streams/plain_wrapper.c:985-1002`) and the socket's
(`main/streams/xp_socket.c:483-485`, the flag set at accept and connect, `main/network.c:1401-1421`), so `proc_open('cat', [0 => $pipe])`
after a coroutine read `$pipe` makes cat exit with `EAGAIN` (measured on debug, a probe).
On those descriptions the stream's own mode decides: a blocking stream waits even when the description
arrived non-blocking (today it reads with no wait, `:559-560`). `stream_set_blocking(false)` stays as it
is.

Waits for it: `io_provider/079` (`core:31` in `tests/lists/S8.txt`).

## 32. IO hooks: a pipe write tries the syscall first

State: requested 2026-10-12 (S8.5). PR: none.

Need: under a provider a pipe write waits for writability before its first `write()`, even on a pipe
with room (`php_io_file_op()`, `main/io/php_io_hooks.c:1669-1673`, `1699-1720`; the Ring and our
provider offer no `F_DIRECT_DATA`). Every write suspends, and the stream stays frozen meanwhile, so two
coroutines that log to one pipe collide: the second gets "Concurrent access to a stream" with the pipe
nearly empty, on the Ring and on the Poll queue (review section 5). TrueAsync writes at once and parks
only when the write could not complete (its `main/streams/plain_wrapper.c:558-563`); read, not run.

Request: every pipe, FIFO or TTY write (any descriptor that is not a regular file), the first
included and with `F_FILES` on or off, runs `write()` first and
waits for writability only on `EAGAIN`, as the socket ladder does; with `F_FILES` the offload op
comes only after `EAGAIN` (today it runs before any syscall, `:1671-1672`). On a description left
blocking (entry 31) the write first asks a zero-timeout `poll()` and writes at most `PIPE_BUF`, so two
coroutines logging to `STDERR` still meet no busy stream (`io_provider/081` shows the collision today).

Waits for it: `io_provider/080`, `081` (`core:32` in `tests/lists/S8.txt`).

## 33. Scheduler API: the core under a refusing scheduler, and the GC's automatic run during an unwind

State: done in `async-core` `a769c850eb1`..`6bfa8ed44de` (2026-10-12, S14.6), in the core from
`async-core-io-2026-10-12-2`. PR: none; it goes with php/php-src#22561.

Need: the core's refusal paths had no test, and the test scheduler could fail only the enqueue of a
CREATED coroutine. Fault tests of the wake, the cancel, the finish handler, the defer and the launch
found that `Fiber::suspend()` parked before waking its caller (a refused wake left its error to the
next coroutine), `start()` stored its finish handler unchecked, a body's error was released by a
refused wake, a fiber kept for its body through a finish handler was freed under the body when the
handler was refused, the GC's refused hand-off leaked its microtask, and refusal errors surfaced in
code that only dropped a reference. Reviews of those fixes found that an automatic GC run during an
unwind parks the frame with the exception pending: its throw position was overwritten meanwhile (the
frame's `catch` was missed), a cancellation replaced an `exit()`, and a cancelled await dropped the
frame's shielded temporaries from the root buffer.

Request: every refusal of `enqueue` returns false with an Error thrown; `suspend` is called with no
exception pending. Fibers wake the caller before parking, add the start handler before the enqueue,
keep the body's error and an exit on a refused wake, and release a fiber kept for its body at the
body's end (`ZEND_FIBER_FLAG_HELD_BY_BODY`). The GC sets its hand-off state after the hand-off
succeeds, clears refusal errors that belong to no caller, holds the exception and its throw position
across the automatic run without moving the frame to the throwing opline, and re-roots shielded
temporaries at the opline the shield used. Tests `ext/test_scheduler/tests/096`-`118`.

Waits for it: nothing; the pinned core carries it.

## 34. IO hooks: a stream given to curl and closed while `curl_exec()` waits

State: on `io-hooks-curl-closed-file` `b985d2dded3` (`f7fbc4335fe` on bukka's `8e470c219fc`, then two
commits shortening its comments on Edmond's word) and `io-hooks-fixes` `a51797d8645`, 2026-10-12 (S8.7),
in the core since `async-core-io-2026-10-12-3` `634cbbf94be`; the shortened comments are `c50c23baf28` on
`io-hooks-fixes-closed-file-comment`, in the core since `async-core-io-2026-10-12-4` `d06ab339297`; PR text for bukka in
`/mnt/project-files/notes/s8-7/io-hooks-curl-closed-file-pr.md`. PR: bukka/php-src#9 (Edmond, 2026-10-12).

Need: `CURLOPT_FILE`, `CURLOPT_INFILE`, `CURLOPT_WRITEHEADER` and `CURLOPT_STDERR` keep the stream's raw
`FILE*`, and `fclose()` frees it whatever the handle's reference. Upstream checks the streams
(`_php_curl_verify_handlers()`) when `curl_exec()` starts and after each PHP callback, as no other PHP
code runs inside `curl_easy_perform()`. Under hooks `curl_exec()` waits in `php_io_run()` and another fiber
may close the stream there: the next `fwrite()` in `curl_write()` used the freed `FILE*` (valgrind; a debug
build aborts in `munmap_chunk()`), an upload read it, and libcurl's verbose output went into it. Found in
TrueAsync's curl history (`/mnt/project-files/notes/s8-curl/trueasync-curl-history.md`, E1).

Request: `curl_write()`, `curl_write_header()` and `curl_read()` check the stream before using its
`FILE*`; a closed one fails the transfer (write error, read abort) without running PHP code inside
libcurl, and `curl_exec()` reports it with upstream's "resource has gone away" warning once it failed. A
check after each wait alone was not enough (the S8.7 Critic): a callback that suspends inside libcurl, or
a cookie `FILE*` of a socket stream, yields there too. `CURLOPT_STDERR` is written by
libcurl itself: after each wait, a cancelled one included, a closed stream is replaced by `stderr`
without running PHP code. Left: `STDERR` closed during a yield inside libcurl.

Waits for it: nothing; the pinned core carries it (`io_provider/082`; core tests
`curl_exec_io_hook_{file,infile,stderr}_closed_while_waiting`).

## 35. Scheduler API: the GC's automatic run does not park the coroutine that filled the buffer

State: done in `async-core` `b318dcd834c` (2026-10-12, S9.38, Edmond's decision), in the core from
`async-core-io-2026-10-12-4`; the RFC text in true-async/php-async-core-rfc says so. PR: none; it
goes with php/php-src#22561.

Need: a root added to a full buffer made the live coroutine wait for the GC coroutine's run, at any
refcount decrement, the VM's release of `$this` after every method call included: other coroutines
ran inside C code that holds what they change, and a cancellation was thrown there, after
`Channel::recv()` had taken its message (`channel/154`).

Request: in a coroutine, `gc_possible_root_when_full()` only queues the run, as TrueAsync does; the
run takes the threshold step once. A coroutine that adds another 100 000 roots of its own before the
run starts waits for it, with the exception and its throw position held, which bounds a loop that
makes garbage (3 000 000 cycles under 128M peak at 60.3M). An explicit `gc_collect_cycles()` waits as
before. Tests `ext/test_scheduler/tests/112`-`121`.

Waits for it: nothing; the pinned core carries it.

## 36. IO hooks: a ring compares its owner's pid with one kept per process

State: on `io-hooks-fixes-ring-pid` `b6ede162357` (on `io-hooks-fixes` `c50c23baf28`) and `io-hooks-fixes`
`b6ede162357`, 2026-10-12 (S8.3a), in the core since `async-core-io-2026-10-12-5` `46369b6a6a5`. PR: none yet;
a candidate for bukka's branch.

Need: `php_io_ring_foreign()` calls `getpid()` on every op to tell a ring inherited across `fork()`, and glibc
has not cached it since 2.25: a system call per op, 2 of the 6 a file read through the Ring made (with the
provider's 4), about 0.8 of the 1.9 µs it lost to a `read()` on the thread.

Request: on Linux the pid is kept, set once by `pthread_once()` at the first ring and refreshed in the child by
a `pthread_atfork()` handler; `getpid()` stays when the registration fails and on other systems (FreeBSD's
`rfork()` runs no atfork handler). Release, 32 MiB files from the page cache, 8 KiB reads, with the provider's
half: Ring 14.3 → 10.8 ms, the thread 6.1 (medians of 11).

Waits for it: nothing; the pinned core carries it.
