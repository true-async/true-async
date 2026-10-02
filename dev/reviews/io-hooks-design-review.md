# IO hooks design: review for a C scheduler provider

Review of the IO hooks design
(https://gist.github.com/bukka/87359261a4bfaa572ce43c93c0554b55, as of 2026-09-30) and its proof of
concept (php/php-src#23997, head `056d9f803a3`), from the side of a C coroutine scheduler built on the
scheduler RFC (https://github.com/true-async/php-async-core-rfc, PoC branch `true-async/php-src:async-core`)
that installs itself as the hooks provider. TrueAsync will be rebuilt that way.

## 1. Summary

The operation model, the per-request provider, deadlines, registrations and the Poll/Ring split fit a
C scheduler. Section 9.1's glue is close to what a real provider looks like. Three things block the
integration as written:

1. **The whole-stream freeze.** One `PHP_STREAM_FLAG_IN_USE` bit covers both directions and every
   caller, and contention throws before the provider is reached. Full-duplex sockets, several
   acceptors on one listener, a shared log pipe and `fclose()` from a supervisor all throw. The claim
   in 9.2 that queueing "can be built on the in-use flag later" as scheduler policy is not possible
   with the current core: no hook runs before the throw. Under the Poll queue the freeze protects no
   buffer at all (`in_flight` is never set there).
2. **mysqlnd bypasses the freeze.** `mysqlnd_vio::close_stream` frees with
   `PHP_STREAM_FREE_RSRC_DTOR`, which skips the busy check, so `$db->close()` from another coroutine
   frees a stream a parked query still uses. Its `res` is NULL, and the orphan path dereferences it.
3. **Orphaned data ops drop bytes.** A cancelled Recv whose completion already happened loses its
   bytes and leaves the stream usable: a pooled connection is reused out of sync with the server, and
   a file read under `F_FILES` leaves a hole.

Two further defects in section 9 (a bailout while a coroutine is parked in `run()`, M12; the provider
not being removed at deactivation, M13) have a fix inside the provider, so they do not block the
integration, but the recipes 9.2 gives for them are wrong.

Each has a contained fix; they are listed with the findings. Findings tagged **[scheduler RFC]** are
changes on the scheduler side, listed here because 9.2 states the integration "needs nothing added to
the RFC itself" and that does not hold.

## 2. Scope and method

Read: the gist (all sections, in depth 3, 5, 7, 8, 9, 11, 12), the three RFC texts (IO hooks, Ring,
Poll API additions), the PR code (`main/io/*`, `main/php_io_hooks.h`, `main/network.c`,
`main/streams/*`, `ext/standard/{streamsfuncs,io_hooks,io_poll,file,exec}.c`, `ext/curl`,
`ext/sockets`, `ext/openssl/xp_ssl.c`, `ext/pcntl`, `ext/mysqlnd/mysqlnd_vio.c`), the scheduler RFC text
and its PoC (`Zend/zend_async_API.{h,c}`, `main/main.c`, `ext/test_scheduler`), and TrueAsync's current
implementation and test suite (`ext/async/tests`) as the reference for what users of a C scheduler
rely on today.

Nothing was built or run except the B2 reproduction (PR head `056d9f803a3`, debug ZTS build with
ASAN, local MySQL). **REPRODUCED** means that run showed the defect; **VERIFIED** means the cited code or text was read and shows the defect;
**INFERRED** means the consequence follows from the code but was not reproduced. `gist:N` is a line
of the gist; other paths are in the PR tree unless marked `scheduler PoC` or `TrueAsync`.

### Reproduction environment

One finding (B2) was reproduced; everything else was checked by reading.

- Source: php/php-src#23997 at `056d9f803a3` ("Remove the guards that will never fail").
- Build: `./configure --disable-all --enable-debug --enable-zts --enable-address-sanitizer
  --with-mysqli --enable-mysqlnd --enable-sockets --enable-pcntl --enable-posix`, gcc 13.3.0,
  Linux 6.6 (WSL2). `php -v`: `PHP 8.7.0-dev (cli) (ZTS DEBUG)`.
- Server: MySQL 8.0.46 on `127.0.0.1`.
- Provider: the PR's own `ext/standard/tests/streams/hooks/scheduler.inc` on the default
  `Io\Poll\OperationQueue`. The Ring (ior) was not built, so the Ring-only paths in B2 and B3 were
  not run.
- Command, from the root of the PR tree:
  `USE_ZEND_ALLOC=0 ASAN_OPTIONS=detect_leaks=0 sapi/cli/php -n io-hooks-repro-mysqlnd-close.php`.
  The script is attached as `io-hooks-repro-mysqlnd-close.php`; its body is in B2.
- Results: with `MYSQLI_OPT_READ_TIMEOUT` set to 2 s, a `heap-use-after-free` in
  `php_io_frame_end()` on every run (two runs); without it, the parked query never wakes and the
  process was killed by `timeout 60`.

## 3. Blockers

### B1. The whole-stream freeze

**Owner:** hooks. **VERIFIED.**

Where: gist sections 3 (gist:78-80), 5.10 (gist:1186-1231) and 9.2 (gist:3872-3875); RFC
"Streams are frozen during an operation". Code: `php_stream_zend_parse_arg_into_stream()` throws on `PHP_STREAM_FLAG_IN_USE`
(`main/php_streams.h:327`) for every function taking a stream; `php_io_frame_begin()` throws for
internal consumers (`main/io/php_io_hooks.c:1027-1046`); `stream_array_freeze()` freezes every member
of a `stream_select()` (`ext/standard/streamsfuncs.c:735-769`); `php_socket_op_begin` does the same
for `Socket` (`ext/sockets/sockets.c:573-589`).

Scenarios that throw `Error("Concurrent access to a stream")` under a provider:

- Full duplex: a reader parked in `fgets($conn)` and `fwrite($conn, ...)` from another coroutine.
  This is WebSocket, HTTP/2, Redis pub/sub and AMQP heartbeats. TrueAsync test
  `io/096-socket_read_and_write.phpt` is this case; a lock over the whole handle deadlocks it even
  with queueing, because the reader is released only by the writer.
- Several acceptors on one listener (`spawn` N accept loops). The design itself expects this: the
  ring parks several accepts on one listener record (gist:1634-1639) and the Poll queue keeps a
  request list per fd. The freeze makes both unreachable.
- A shared log handle on a pipe (a `StreamHandler` on `php://stderr` under Docker): a pipe write
  waits for readiness through the provider (`php_io_file_op()`, `php_io_hooks.c:1530,1557-1570`),
  so a second coroutine logging in the same loop pass throws.
- `fclose()` or `stream_socket_shutdown()` from a supervisor or a graceful shutdown loop. There is no
  way to interrupt a parked read except cancelling the reader.
- `stream_select()` in one coroutine and `fread()` in another on a member stream.
- Several coroutines on one file handle once the provider sets `F_FILES`.

The justification (gist:78-80, 1191-1194) is the Ring: the kernel or a worker owns the read buffer.
Under the Poll queue `op->in_flight` is never set (`php_io_queue_poll.c:239` is its only write, to
false), and on the Ring only `PHP_IO_OP_F_STREAM_BUF` reads reference stream memory; every other data
op uses a bounce buffer (`php_io_ring.c:475-485`). What the freeze actually protects is three
separate things: the lifetime of the `php_stream`, the `readbuf + writepos` pointer captured before
the park, and the descriptor staying open while a watcher is armed. None of them requires excluding a
writer, a metadata reader or a `select`.

9.2 says a per-stream mutex "is scheduler policy and can be built on the in-use flag later; the core
does not decide it." The throw happens in argument parsing and in `php_io_frame_begin()`, before any
hook. A provider has no entry point there, so the core does decide it, and the RFC text makes the
`Error` user-visible behaviour that a later change would have to re-vote.

Proposed change, in three parts:

1. **Per-direction exclusion** at the `php_stream` API level, not at the `php_io` frame, since
   `fgets()` spans several frames and keeps state in `readbuf` between them. Read side: `readbuf`,
   `readpos`, `writepos`, read filters, `eof`, accept. Write side: the write path and write filters.
   A seekable plain file takes both, because `_php_stream_write_buffer` resets the read buffer and
   seeks (`main/streams/streams.c:1112-1113`). TLS takes both unless M7 is decided the other way.
   Functions that only read metadata (`feof`, `stream_get_meta_data`, `stream_socket_get_name`) take
   neither. The split lands at the stream API entry points (read, `fill_read_buffer`, `get_line`,
   write, flush, seek, both copy paths, filter append and remove, cast): about 40 sites in
   `main/streams/streams.c`; TrueAsync's `streams.c` is a working reference.
2. **Contention goes to the provider.** When a side is held, the core builds a wait operation (no
   descriptor, the stream's deadline, cancellable like any op) and runs it; releasing the side
   completes the head waiter. A provider that wants today's behaviour completes it as Cancelled with
   an `Error`, which makes the throw that provider's policy. Without a provider the case cannot arise.
   This is what makes the 9.2 sentence true.
3. **Close as cancel.** `fclose()` does not wait for either side: it marks the stream closed,
   unregisters, and completes every op pinned on the stream with a status such as `PHP_IO_CLOSED`
   (no exception; `fread` returns false with `eof` set). `ops->close` (the `close(fd)`) and the free
   of `readbuf` and the stream move to the last unpin. The `php_io` frame already exists on every
   suspension, so the pin can live in `php_io_frame_begin/end`. Deferring `close(fd)` also removes the
   descriptor-reuse hazard for waiters and ring ops in flight.

Reference designs: Go `internal/poll/fd_mutex.go` (read lock, write lock, closed bit, reference
count; `Close` wakes waiters with `errClosing` and closes at the last `decref`), and TrueAsync
`main/streams/streams.c:67-90,477-508,634-787` with tests `io/085`, `io/086`, `io/089`, `io/096`.

### B2. mysqlnd bypasses the freeze: use after free on close, NULL dereference on cancel

**Owner:** hooks. Close path: **REPRODUCED** under ASAN on the PR head. Orphan path: VERIFIED by
reading; not run (the Ring was not built).

- `php_stream_free()` checks for a busy stream only without `PHP_STREAM_FREE_RSRC_DTOR`
  (`main/streams/streams.c:308`). `mysqlnd_vio::close_stream` always passes it
  (`ext/mysqlnd/mysqlnd_vio.c:677-681`), and `send_close` in `CONN_QUERY_SENT` goes straight there.

  Reproduction with the PR's own test scheduler (`ext/standard/tests/streams/hooks/scheduler.inc`),
  a local MySQL server, a debug ZTS build with `--enable-address-sanitizer`, `USE_ZEND_ALLOC=0`:

  ```php
  <?php
  include 'ext/standard/tests/streams/hooks/scheduler.inc';

  mysqli_report(MYSQLI_REPORT_OFF);
  $db = mysqli_init();
  $db->options(MYSQLI_OPT_READ_TIMEOUT, 2);   // only so that the parked query wakes up
  $db->real_connect('127.0.0.1', 'root', 'root');

  $scheduler = new Scheduler();
  Io\Hooks\set_hooks($scheduler);
  $scheduler->spawn(function () use ($db) { $db->query('SELECT SLEEP(1)'); });
  $scheduler->spawn(function () use ($db) { $db->close(); });
  $scheduler->loop();
  ```

  Result:

  ```
  ERROR: AddressSanitizer: heap-use-after-free ... READ of size 4
    #0 php_io_frame_end            main/io/php_io_hooks.c:1051
    #1 php_io_descriptor_op        main/io/php_io_hooks.c:1222
    #4 php_sockop_read             main/streams/xp_socket.c:140
    ...
    #16 zif_mysqli_query           ext/mysqli/mysqli_nonapi.c:649
  freed by thread T0 here:
    #1 php_stream_free             main/streams/streams.c:466
    #2 mysqlnd_vio::close_stream   ext/mysqlnd/mysqlnd_vio.c:678
    #3 mysqlnd_conn_data::send_close ext/mysqlnd/mysqlnd_connection.c:1122
    #6 zif_mysqli_close            ext/mysqli/mysqli_api.c:278
  ```

  Without `MYSQLI_OPT_READ_TIMEOUT` the parked query never wakes: its descriptor was closed under
  the watcher, and the process waits forever.

  After the resume `recv()` writes into freed memory, and `php_io_frame_end()` writes
  `f->stream->flags` (`php_io_hooks.c:1050-1051`). `ext/dba/dba.c:257,264` has the same pattern.
- mysqlnd removes its streams from the resource lists and sets `res = NULL`
  (`mysqlnd_vio.c:113-126`). `php_io_frame_begin()` guards that (`php_io_hooks.c:1039`), but
  `php_io_stream_orphan()` does `GC_ADDREF(stream->res)` (`:645`) and `php_io_stream_unfreeze()`
  does `zend_list_delete(stream->res)` (`:663`) without a check. A query cancelled under the Ring
  (any Recv is `in_flight` there, `php_io_ring.c:938-944`) dereferences NULL.
- A bailout skips `frame_end`, and request shutdown clears `IN_USE` only on streams in
  `EG(regular_list)` and `EG(persistent_list)` (`php_io_hooks.c:441-449`). A persistent mysqlnd
  stream is in the persistent list and is swept; a non-persistent one is on neither list after
  `mysqlnd_fixup_regular_list`, so `close_stream` frees it while it is still frozen, which is the
  first bullet's path.

Fix: make the busy check unconditional in `php_stream_free()` or, better, replace it with the pin and
deferred free of B1.3; guard `res` in the orphan path; add mysqlnd and dba to the 11.2 list of
extensions that need a guard.

### B3. Orphaned data operations drop bytes

**Owner:** hooks. **VERIFIED** (text and code); failing runs INFERRED.

Gist 5.10 (1277-1278) and the Ring RFC say bytes a Recv already placed are "dropped with the
operation"; `php_io_ring.c:1552-1556` frees the record on the orphaned completion. Only TLS gets a
dead mark (gist:3537-3544, `xp_ssl.c:441-451`); plain streams get no mark.

- A pooled Redis or MySQL connection: a query coroutine is cancelled by a timeout while its Recv has
  completed in the kernel but was not reaped. The bytes are dropped, the stream is unfrozen, the
  connection goes back to the pool, and the next borrower reads the tail of the previous reply as its
  own. RESP has no integrity check, so this is silent.
- A regular file under `F_FILES` on Linux: a read already running in io-wq cannot be cancelled
  (`-EALREADY`), an `IORING_OP_READ` at offset -1 advances the file position, the bytes are dropped,
  and the next `fread()` skips them (TrueAsync `io/100-cancel_keeps_the_position.phpt`).

Fix: when an orphaned `F_STREAM_BUF` read settles with `res > 0`, commit it (`writepos += res`); the
stream is pinned and read-locked until then, so this is safe. A bounced read appends its bytes to
`readbuf`. An orphaned Send, or a read that cannot be committed, marks the stream broken for every
user: generalise `io_dead` into a stream flag visible through `feof()` and `stream_get_meta_data()`,
so pools can discard the connection. For files, a stream-tracked explicit offset (the Windows
overlapped path already has one) removes the hole at the source.

## 4. Major issues

### M1. Cancellation contract

**Owner:** both. **VERIFIED.**

- Two contracts: 5.4 (gist:496-498) says the provider "cancels its backend op, waits for the
  completion" and then resumes; 5.10 (1270-1276) and 9.2 (3810-3814) say the provider does nothing and
  `php_io_run()` orphans the op. Pick one.
- "Completion before cancel: `fread()` returns the bytes" (gist:3814-3818) is incorrect: the VM
  does not keep it, and the 9.1 glue returns SUCCESS in exactly that state (gist:3727-3728). The
  scheduler throws the cancellation at the suspension point, `run()` returns SUCCESS with
  `EG(exception)` set, and `ZEND_DO_ICALL` rethrows on return, so `$data = fread(...)` never assigns:
  the bytes copied out of the buffer are lost to the program, and an `fwrite` that was sent reports an
  exception, so a retry sends twice. Hooks: `run()` never returns SUCCESS with `EG(exception)` set.
  **[scheduler RFC]**: the means to meet that rule; an error enqueued onto a coroutine already
  woken by a normal enqueue is held for its next suspension. The PoC's `ts_enqueue()` overwrites the
  pending error of an already queued coroutine (`test_scheduler.c:1389-1409`).
- The wrapper ladders re-enter `run()` without checking `EG(exception)`
  (`php_io_descriptor_op()` `php_io_hooks.c:1188-1221`, `php_io_waitpid()` `:1832`,
  `php_io_sigwait()` `:1959-1968`). A Ready from a stale Edge bit followed by a cancellation returns
  SUCCESS, `recv()` gives `EAGAIN`, and the loop parks again with the cancellation already delivered:
  a timeout no longer bounds the call. In the PoC a cancelled coroutine cannot park again
  (`test_scheduler.c:1551-1553`), so the re-park needs an error enqueued without the cancelled
  mark, as a timeout-style error is; the consequence is INFERRED, the missing check is VERIFIED. Fix: stop with `ECANCELED` when `EG(exception)` is set
  after a non-Done result, and state that `run()` is never entered with an exception pending.

### M2. "May suspend here" is a single NULL check

**Owner:** both. **VERIFIED.**

The 9.2 guard (gist:3824-3827) checks `ZEND_ASYNC_CURRENT_COROUTINE == NULL` only. Three other states
reach `ZEND_ASYNC_SUSPEND()` where no suspension is possible:

- the scheduler's own context (`test_scheduler.c:1545-1547` throws there); a bridge scheduler that
  logs to a pipe from `onSuspend` hits it;
- a Fiber the scheduler declined: `ZEND_ASYNC_CURRENT_COROUTINE` stays the enclosing coroutine, and a
  `stream_select()` inside Revolt's loop fiber suspends the coroutine from the fiber's stack, leaving
  that stack stranded when the coroutine is resumed (INFERRED) (the engine itself tests
  `EG(active_fiber) && active->coroutine == NULL`, `scheduler PoC Zend/zend_fibers.c:1355-1357`), so
  the row "Foreign fiber: not involved" is wrong;
- after deactivation (M13).

Fix: one predicate, provided by the scheduler API and used by every provider, that returns false in
all four cases; the provider then answers `PHP_IO_UNSUPPORTED`.

### M3. A provider with its own loop

**Owner:** hooks. **VERIFIED.**

- Orphans are reachable only through `php_io_queue`: `php_io_op_finish()` orphans through
  `op->queue->ops->orphan` (`php_io_hooks.c:706-722`), `php_io_stream_orphan()` takes a
  `php_io_queue *`, and `php_stream_free()` drains through `queue->ops->drain`. A provider on libuv
  running `uv_fs_read()` into `readbuf` under `F_FILES` has no way to keep the stream pinned after a
  cancelled `run()`; the gist sentence "one with its own loop has to give the same guarantee itself"
  (1285) means writing a shim queue, and a synchronous `drain` would need a nested `uv_run()`, which
  is not reentrant. Fix: add `orphan(hooks, op)` and `drain(hooks, stream)` to `php_io_hooks_ops`,
  or document the shim-queue protocol.
- A queue cannot be embedded in an external loop: `php_io_queue_ops` has no descriptor accessor
  (`php_io_hooks.h:385-402`). The Ring has `php_io_ring_notify_fd()` (`php_io_ring.h:56`), which
  section 9 does not mention; the Poll queue exposes nothing. Fix: a `notify_fd` entry in the queue
  ops.
- On libuv, registrations save nothing: `uv_poll` is level-triggered and its stop is
  `EPOLL_CTL_DEL`, and `uv_poll_init()` refuses a second watcher on one descriptor, so a libuv
  provider must key its state by descriptor and merge the Read and Write pairs and same-descriptor
  Any members itself. This is not a defect, but the 5.14 gains do not apply to such a provider.

### M4. "Removed before the descriptor closes" is violated in two places

**Owner:** hooks. **VERIFIED.**

- Connect retries. The loop over resolved addresses in `php_network_connect_socket_to_host()`
  closes each failed socket (`main/network.c:1165`) without unregistering the stream's pairs, and
  `php_io_register()` matches an existing record by event only, not by descriptor
  (`php_io_hooks.c:288-292`). With an IPv6 address that answers RST and an IPv4 fallback, attempt 2
  reuses attempt 1's record: under the Poll queue its recorded `hup` answers the connect wait at
  once (`php_io_queue_poll.c:338-343,396-401`). `php_io_connect_ex` then reads `SO_ERROR` = 0
  while the connect is still in progress and reports success (`php_io_hooks.c:1477-1487`), and
  since `hup` is never cleared (`:340` clears `ready` only) every later write wait on the stream
  completes at once and the ladder spins. After `closesocket()` the next `socket()` usually returns
  the same number, so a `reg->fd` check alone misses it: unregister before each `closesocket()`.
- curl. The contract makes an exception: the `remove()` of a curl pair "comes … after libcurl closed
  the socket" (gist:1673-1676), and the window can contain a suspension, since callbacks may do IO
  (7.7, 3401-3406). Another coroutine can get the same descriptor number meanwhile; a libuv provider's
  `uv_poll_stop()` then deletes the new socket from epoll, and that coroutine hangs. Fix: for C
  providers call `remove()` synchronously in the `CURL_POLL_REMOVE` socket callback, or add a
  non-throwing `invalidate(reg)` hook called before the close; state the rule without exceptions.

### M5. Idle waiting, deadlock detection and signals in the 9.1 glue

**Owner:** both. **VERIFIED** (code); spin INFERRED.

- `zend_async_report_deadlock()` does not exist in the scheduler PoC; it comes from `reactor.md`.
  Deadlock handling is scheduler policy in the RFC (`test_scheduler.c:1153-1210`).
- `count_pending() == 0` is wrong in both directions: a permanently armed scheduler watcher on the
  same queue (9.1 recommends putting the scheduler's timers and wakeups there) keeps it above zero, so
  a real deadlock hangs; waits outside the queue (thread-pool tasks) leave it at zero while a wakeup
  is coming. 9.3 maps reactor.md's visible/hidden handles to `countPending()` (gist:3940), but the
  queue has no hidden flag. Fix: `PHP_IO_OP_F_HIDDEN` not counted, a provider-side counter for
  external pending work, and the decision left to the scheduler.
- `wait()` returning `-1`/`EINTR` while a PHP signal handler is pending
  (`php_io_queue_poll.c:655-659`) is ignored by the glue; with every coroutine parked no opcode runs,
  so the handler never runs and the loop spins. The design must say who runs PHP signal handlers when
  everything is parked (for example, enqueue main or a dispatch coroutine).
- Reaping only when the run queue is empty starves IO under a yield loop; reap without blocking once
  per scheduler tick.

### M6. Signals under a provider with its own loop

**Owner:** hooks. **VERIFIED** (text, tests, libuv source).

Section 8 (gist:3642-3656) requires the wait to return when a PHP handler is pending, written for the
core queues. libuv polls again after `EINTR` (`src/unix/linux.c:1486`), so a libuv provider needs an async-signal-safe
wake-up from the signal trampoline (for example a `NotifyHandle` it can register). `SignalHandle`
blocks the signal (6.8, gist:2824-2829), so a `pcntl_signal()` handler for the same signal never runs;
TrueAsync delivers one signal to both (`ext/async/tests/signal/013-signal_pcntl_registered_after.phpt`).
`SignalHandle` is CLI-only under ZTS (gist:2836-2845), while a C scheduler on FrankenPHP has threads.

### M7. TLS suspends inside OpenSSL

**Owner:** hooks. Error queue: **VERIFIED** against OpenSSL's documentation. Re-entry: INFERRED.
Neither symptom was reproduced.

With the custom BIO (7.8, gist:3491-3500) a coroutine suspends inside `SSL_read()`. A second
coroutine's `SSL_write()` on the same `SSL *` then re-enters it mid-call, which OpenSSL does not
support, so TLS can never be full duplex. Before OpenSSL 4.0, `SSL_get_error()` depends on the
current thread's error queue (the `SSL_get_error(3)` history note: since 4.0 it "no longer depends
on the state of the error stack"). The gist already clears the queue before each call because it is
"thread local, not fiber local" (gist:3523-3524), but that does not cover entries another connection
adds while the first call is suspended inside `SSL_read()`; with OpenSSL 3.x they can make the first
`SSL_get_error()` report `SSL_ERROR_SSL`. TrueAsync waits outside `SSL_*` today
(`ext/openssl/xp_ssl.c:3060,3240`). gist:3499-3500 names the BIO pair as the fallback "if the custom
BIO proves problematic with a C scheduler"; this is that case. Decision needed: the BIO pair as the
primary path, or `WANT_READ`/`WANT_WRITE` returned from the BIO with the wait outside `SSL_*`.

### M8. curl is fixed to a per-handle multi

**Owner:** hooks. **VERIFIED.**

`curl_exec()` runs its own multi per easy handle whenever any provider is installed
(`ext/curl/interface.c:2834-2838`), and the C submit-with-callback slot is "not part of the design"
(gist:3693-3696). A C scheduler that drives one shared multi per thread (connection reuse across
handles, HTTP/2 multiplexing across coroutines) cannot keep it. Fix: a capability or hook that lets a
C provider own `curl_exec()` and `curl_multi_*`.

### M9. Missing call sites and timeouts

**Owner:** hooks. **VERIFIED.**

The operations exist; the call sites do not.

- `exec()`, `system()`, `passthru()` and `shell_exec()` open with `VCWD_POPEN` (`exec.c:123-125`):
  the reads suspend through `php_io_read()` on the `NO_SEEK` stream (`plain_wrapper.c:436,670`),
  but the stream has no `child_pid`, so its close is `pclose()` and blocks in `waitpid()`
  (`plain_wrapper.c:748-762`). 11.2.7 lists it as unguarded; opening them through
  `php_stream_popen()` makes the close the WaitPid op.
- `php_poll2()` and `php_pollfd_for*()` with a timeout stay synchronous (7.1). Routing them into a
  Poll or Any op with no stream when a provider is installed covers `ext/ftp`, `pdo_pgsql`
  `getNotify`, `ext/pgsql` and PECL users at once.
- ext/sockets name resolution (7.9, gist:3552-3554); the GetAddrInfo op exists.
- pgsql: `php_io_poll_ex(NULL, NULL, fd, ...)` (gist:624, 640-642) already lets an extension wait on
  its own descriptor, so pgsql is a missing call site, not a design gap. The libpq side needs
  `PQconnectStart`/`PQconnectPoll` and a `PQisBusy`/`PQconsumeInput` loop.
- Pipe read timeouts get an infinite deadline (7.3, gist:3180-3181), and `stream_socket_recvfrom()`
  on UDP too (`main/streams/xp_socket.c:251-260`). `stream_set_timeout()` on a pipe is simply not
  implemented on master, so honouring it breaks nothing.
- Windows: pipes, the console, exec and `stream_select()` on pipes still block the thread under any
  provider (7.3, 7.9, 11.2.6).

### M10. `flock()` without `LOCK_NB` deadlocks

**Owner:** hooks. **VERIFIED.**

Out of scope in 4.1 (gist:132), "an op, a guard or a documented limit" in 11.2.7 (gist:4097). Under a
scheduler the limit is a deadlock, not a stall: coroutine A holds `LOCK_EX` and parks on a timer; coroutine B does its own
`fopen()` and `flock(LOCK_EX)`; the two open file descriptions conflict, the kernel blocks the only
thread, A never runs and never unlocks. `ext/session/mod_files.c:207` has the same problem for two
coroutines with one session id. Fix: gist:1131 already puts `flock` in the work pool, so a Flock work op on the Ring
(`IOR_OP_WORK`), with `LOCK_NB` and a Timer backoff (1-100 ms through `php_io_sleep()`) as the
fallback for a readiness-only provider.

### M11. Lifecycle table

**Owner:** both. **VERIFIED.**

The table in 9.2 (gist:3831-3839) has no rows for shutdown functions (`main.c:1939`, between #2/#3
and destructors, provider still installed), output handlers and module RSHUTDOWN (M13). After a
bailout, a `register_shutdown_function` that reports to an error tracker over curl becomes an Any op
after the scheduler discarded its work. The Shutdown row ("`run()` returns FAILURE with the shutdown
error", gist:3799) pre-empts the RFC's rule that the scheduler decides whether the remaining
coroutines run to completion or are cancelled (`scheduler_rfc.md:330-331`): with the row as
written every IO in a coroutine the scheduler chose to finish throws, and so does every IO in a
shutdown function; in the PoC it is also raised only for `exit()` inside an
adopted Fiber (`scheduler PoC Zend/zend_fibers.c:852-853`). Fix: rows for shutdown functions (normal,
after bailout, after `exit()`) and output handlers; after Shutdown, IO follows the scheduler's
remaining-coroutine policy.

### M12. Bailout while a coroutine is parked in `run()`

**Owner:** hooks (text), provider (fix). **VERIFIED** (code); stack reuse INFERRED.

`php_io_run_ex()` calls `hooks->ops->run()` and then `php_io_op_finish()` with no `zend_try`
(`php_io_hooks.c:962-963`). The Poll queue keeps the caller's op in `req->op`
(`php_io_queue_poll.c:235`) and later writes through it (`:263`, `:505`, `:722`); the Ring does the
same in `deliver_one` (`php_io_ring.c:1882-1889`); the 9.1 `sched_waiter` is on the same stack.

In the scheduler PoC a fatal error in coroutine B is propagated by switching into every suspended
coroutine with a bailout flag and calling `zend_bailout()` on its stack
(`ext/test_scheduler/test_scheduler.c:1059-1112,1572-1574`). The order is: the loop's `zend_catch`,
then `ts_bailout_all()` (`:1248-1252`), then the handover to main with the bailout flag
(`:1263-1272`), then `zend_bailout()` in main (`:1572-1574`), then
`ZEND_ASYNC_RUN_SCHEDULER_AFTER_MAIN(true)` (`main/main.c:2632`). Main receives the `isBailout`
suspend only after every parked coroutine was unwound, so the 9.2 row "isBailout cancels every
in-flight op and drains" (gist:3795, 3836) works on pointers into stacks that were already unwound.
For the main coroutine the stack is the OS stack, reused by shutdown functions, so a later write
`op->queue = NULL` corrupts a live frame (INFERRED).

The provider can close this itself, because the longjmp passes through its own `run()`: bracket the
suspend in `zend_try`; in `zend_catch` detach the op from the queue with `queue->ops->orphan()` (a
plain cancel on the Poll queue; on the Ring the record is kept and the stream stays frozen until it
settles), unlink the waiter, then `zend_bailout()`. What the core's skipped `php_io_frame_end()` would
have done (the `IN_USE` flag, the frame's resource reference, the in-flight counter) is reset by
request shutdown (`php_io_hooks.c:441-449`), and the request is ending anyway. This needs the
scheduler to unwind parked coroutines through their suspension point, as the PoC does; a scheduler
that frees coroutine stacks without unwinding them must first detach every waiter it tracks.

Changes to the design: replace the "cancel at isBailout" recipe in 9.2 with "the provider brackets
its suspend and orphans the op on a bailout", and state in 5.10 that no queue or provider may touch
an op after its frame was abandoned. Bracketing `hooks->run()` in `php_io_run_ex()` as well would do
this once for every provider and also run the frame end; one `setjmp` per op handed to a provider is
small next to a suspend.

### M13. The provider is not removed at deactivation

**Owner:** hooks (text), provider (fix); **[scheduler RFC]** for the PoC guard. **VERIFIED**
(window and PoC path); module order INFERRED.

9.2 says the provider is removed "when the engine deactivates the async state after the final
end-of-main handover" (gist:3781-3783, 3838). The scheduler RFC has no notification there, by
design: after `ZEND_ASYNC_DEACTIVATE` (`scheduler PoC Zend/zend_async_API.h:690`,
`main/main.c:1960`) everything runs synchronously. Suspends #2 and #4 are both
`suspend(true, false)`, so a provider cannot tell the final handover apart. The hooks stay installed
until `basic` RSHUTDOWN (`ext/standard/basic_functions.c:503`), which runs after
`php_output_end_all()` and after the RSHUTDOWN of every module registered after `standard`.

Scenario: a Redis session handler writes the session in RSHUTDOWN, the reply is not there yet, the
Poll op goes to `run()`, `ZEND_ASYNC_CURRENT_COROUTINE` is the non-NULL post-main coroutine so the
9.2 NULL guard does not fire, and `ZEND_ASYNC_SUSPEND()` reaches a scheduler whose state is already
NULL (`test_scheduler.c:1510,1558`): a crash in the PoC. The sentence "session writes … run after
removal, synchronously" (gist:3823-3824) does not hold as written.

The provider can close this itself: `run()` returns `PHP_IO_UNSUPPORTED` when `!ZEND_ASYNC_IS_ACTIVE`
(part of the predicate in M2), and the core then performs the operation synchronously, which is what
the RFC promises after deactivation. Changes to the design: replace the removal sentence in 9.2 with
this rule, or have the core remove the provider immediately before `ZEND_ASYNC_DEACTIVATE`.
**[scheduler RFC]**: `ZEND_ASYNC_SUSPEND()` (`zend_async_API.h:572`) has no `ZEND_ASYNC_IS_ACTIVE`
guard, unlike `ZEND_ASYNC_RUN_SCHEDULER_AFTER_MAIN` (`:577-582`); a suspend after deactivation should
be refused rather than reach a scheduler that is gone.

## 5. Minor issues and PoC defects

- **Hooks lock and coroutines.** `php_io_hooks_lock()` blocks Fiber switches only
  (`php_io_hooks.c:212-216`, checked in `Fiber::suspend`); a provider whose `add()` or `dtor()`
  suspends through the scheduler leaves the lock held. **[scheduler RFC]**: the suspend path honours
  `zend_fiber_switch_blocked()`.
- **Zero deadline.** 5.5's pipe rule (gist:650-652) keeps a non-blocking call from the provider;
  for sockets the ladder sends one op with `dl == 0` after the first `EAGAIN`
  (`php_io_hooks.c:1194-1197`), and no text says whether a provider may suspend on it. On Windows a non-blocking `Socket` then reports
  `WSAETIMEDOUT` where master reports `WSAEWOULDBLOCK` (`sockets.c:510-513`).
- **Any member completed Unsupported.** curl loops (`interface.c:2749-2757`), `stream_select()` returns
  0 (`streamsfuncs.c:904-908`); the Ring produces such members (`php_io_ring.c:884-888,1333-1336`).
  Fold them into an Unsupported Any, as 5.8 (gist:1060-1061) says.
- **Accept of descriptor 0** fails validation (`res > 0`, `php_io_hooks.c:940-942`) and leaks the fd
  in a daemon that closed STDIN. Use `>= 0`.
- **`reg->provider_data` is not reset** on re-add after a provider replacement
  (`php_io_hooks.c:310-316`).
- **Clock.** `zend_hrtime()` is `CLOCK_MONOTONIC_RAW`; libuv uses `CLOCK_MONOTONIC`. State that
  deadlines are compared only against `zend_hrtime()`.
- **Any results from a C provider are not validated** (`index < n`, `n_results <= n`); only the
  userland bridge checks (`io_hooks.c:1419-1420`).
- **Windows errno mapping** (`php_io_hooks.c:46-66`) leaves `ENETUNREACH`, `EHOSTUNREACH`,
  `EADDRINUSE`, `EINPROGRESS` as CRT numbers.
- **`FG(io_reaped)` is per thread**; a status recorded in one thread is invisible to a waiter in
  another (INFERRED).
- **`socket_recv()`/`socket_send()`** add a `getsockopt(SO_RCVTIMEO/SNDTIMEO)` per call under a
  provider (`sockets.c:612-625`).
- **The Ring drops `PHP_POLL_PRI`** (`php_io_ring.c:789-799`).
- **`Socket` objects are unfrozen** while a Ring op on them may still be in flight
  (`sockets.c:593-598`; `php_io_ring_orphan` keeps only ops with a stream), contrary to 5.10.
- **Orphan resource reference** from `php_io_stream_orphan()` is not returned on a close-path free
  (`php_io_hooks.c:659-664,683-687`), INFERRED.
- **Ring `drain()` blocks the thread** (`php_io_ring.c:1846-1868`); an io-wq read on NFS stalls every
  coroutine.
- **A TTY switched to non-blocking** is shared with child processes (7.3, gist:3183-3193): after
  `fgets(STDIN)`, `passthru('vim x')` inherits `O_NONBLOCK`.
- **A pipe write under a provider always takes one readiness wait** even when the descriptor is
  writable (`php_io_hooks.c:1557-1570`), although the first op already switched it to non-blocking
  (gist:3183-3186): syscall-first after the switch saves a suspension per log line.
- **`sleep(0)`**: the PR sends a Timer with deadline "now" (`php_io_hooks.c:2029-2040`); state whether
  it yields.
- **A Poll op with a NULL handle** is legal (7.7 relies on it) but not listed in 5.1 (gist:182).
- **9.1 glue details.** `record_in_flight()` is never cleared and an internal-context key is meant to
  be allocated once at MINIT (`scheduler PoC zend_async_API.h:473-478`); an intrusive waiter list in
  the provider is simpler. Awaiting-info is wiped by the scheduler, not the engine. There is no "RFC
  thread API" (threads are future scope). `sched_run()` returns FAILURE on a `submit()` failure without
  an exception, which violates the `run()` contract.
- **Fork text.** "The child … calls `set_hooks(null)`" (gist:3854-3855) contradicts 5.4
  (`set_hooks()` throws while a C provider is installed).
- **Foreign fibers.** Being offered a Fiber is not being given it; the scheduler may decline. The
  single-provider rule stands on its own; rewrite gist:3857-3862.
- **Bridge example** keys results by `spl_object_id($co)` and does not unset them on the exception
  path; key by op and unset in `finally`.
- **Performance data.** The 5.9 measurements are a userland scheduler on NTS release builds; there is
  no C-provider or ZTS data, and `io_hooks.txt:185` generalises from them.

## 6. Missing from the design

1. Per-direction exclusion, a contention wait handed to the provider, close as cancel with deferred
   free and deferred `close(fd)` (B1).
2. The provider rule for a bailout (bracket the suspend, orphan the op) and a rule that nothing
   touches an abandoned op (M12).
3. The provider rule for deactivation (`run()` answers Unsupported when async is off) (M13).
4. Commit-on-settle for stream-buffer orphans and a stream-level broken mark (B3).
5. A single suspend predicate (M2).
6. `orphan`/`drain` in the hooks ABI, and a queue descriptor for external loops (M3).
7. A non-throwing invalidation before close for curl (M4).
8. Hidden ops, an external pending counter, and who runs PHP signal handlers when every coroutine is
   parked (M5).
9. A signal wake-up hook for providers with their own loop (M6).
10. A TLS duplex decision (M7).
11. A curl ownership hook (M8).
12. Lifecycle rows for shutdown functions, output handlers and module RSHUTDOWN (M11).
13. A C-provider contract: never write op outputs after `run()` returned; report a child reaped for a
    cancelled WaitPid through `php_io_child_reaped()`; the meaning of deadline 0; the clock; an Any
    member is never answered Unsupported on its own.

### On the scheduler side

These requests are for the scheduler RFC, not for the hooks design:

1. On a bailout, parked coroutines are unwound through their suspension point, or the provider
   detaches its waiters before their stacks are freed (M12).
2. `ZEND_ASYNC_SUSPEND()` refuses after deactivation (M13).
3. An error enqueued onto an already woken coroutine is held for its next suspension (M1).
4. One "may suspend here" predicate in the scheduler API (M2).
5. Who runs PHP signal handlers when every coroutine is parked (M5).
6. The suspend path honours `zend_fiber_switch_blocked()` (section 5, hooks lock).

## 7. Coverage parity: a C scheduler today and under the hooks

Reference: TrueAsync and its tests (`ext/async/tests`).

| Feature (tests) | Under the hooks |
|---|---|
| Socket read, write, connect, accept, one coroutine per stream (`stream/001-015`) | covered |
| One socket read and written by two coroutines (`io/096`) | not possible (B1) |
| One file handle used by several coroutines (`io/083`, `085`, `097`) | not possible with `F_FILES` (B1) |
| Close from another coroutine wakes the parked one (`io/086`, `exec/025`) | not possible (B1) |
| Regular file IO, include/require (`io/*`, `include/*`) | covered with `F_FILES`; cancel semantics differ (B3) |
| POSIX pipes, `proc_open`, STDIN (`io/002`, `io/035`) | covered; TTY semantics differ |
| Pipe read timeouts (`io/039`, `040`, `042`, `043`) | not covered (M9) |
| UDP recvfrom and sendto (`stream/028`, `029`) | covered |
| UDP recvfrom timeout (`stream/030`) | infinite (M9) |
| flock waits (`io/081`, `084`) | deadlock (M10) |
| exec family (`exec/*`) | reads covered; `pclose()` blocks (M9); blocking on Windows |
| `proc_close`, `pclose` | covered (WaitPid) |
| Host name lookups, stream DNS (`dns/*`) | covered |
| ext/sockets data operations (`socket_ext/001-005`) | covered |
| ext/sockets name resolution (`socket_ext/006`) | call site missing (M9) |
| `stream_select` (`stream/005`, `010`, `017-037`) | covered; members frozen (B1); pipes on Windows block |
| TLS (`stream/025-027`) | covered; no duplex (M7) |
| curl_exec, curl_multi (`curl/*`) | covered; no shared multi (M8) |
| mysqli, mysqlnd, PDO MySQL (`mysqli/*`, `pdo_mysql/*`) | covered through streams; close from another coroutine unsafe (B2) |
| pgsql, pdo_pgsql (`pdo_pgsql/*`) | call site missing (M9) |
| ftp, pdo_pgsql `getNotify`, other `php_pollfd_for` users | call site missing (M9) |
| sleep family (`sleep/*`) | covered; `sleep(0)` unspecified |
| pcntl handlers together with a scheduler's signal API (`signal/011`, `013-015`) | not possible (M6) |
| Signals in threads (`signal/008`, `009`, `012`) | not possible under ZTS outside CLI (M6) |

## 8. Checked and fine

- The operation model and the per-request single provider fit a C scheduler; `run()`, `add()`,
  `remove()` and `dtor()` are enough for a readiness-only provider.
- The names the 9.1 glue uses exist with the assumed semantics (`ZEND_ASYNC_CURRENT_COROUTINE`,
  `ZEND_ASYNC_SUSPEND`, `ZEND_ASYNC_ENQUEUE_COROUTINE`, `ZEND_ASYNC_ADD_AWAITING_INFO`, the internal
  context API), apart from `zend_async_report_deadlock`.
- Installing the provider at the Launch notification, before the prepend file, is the right point;
  the `php -r` bypass reasoning holds.
- Cancel arriving while the completion is already queued leaves no dangling op: `w.done` is true
  and `run()` returns SUCCESS. What the VM does with that result is M1.
- Cancelling an Any: orphan, then the Poll queue releases the members
  (`php_io_queue_poll.c:502-529,693-705`).
- A coroutine unwound by a graceful exit (not a bailout) returns through `run()`, and
  `php_io_op_finish()` is reached.
- Per-thread hooks and queues (`FG`) match the scheduler RFC's thread-per-request model.
- Refusing `pcntl_fork()` while operations are in flight, and the fork handler pair for schedulers.
- `stream_select()` de-duplicates streams across its sets and unfreezes on every exit.
- The order of frame end and orphan registration has no race under cooperative scheduling.
- Unregistration on the ordinary close paths: `php_stream_release_io()` before `ops->close`,
  `socket_close()`, the `Socket` free handler, `socket_export_stream()`.
- Deadline representation and saturation (`php_deadline.h`); a stream timeout is converted once per op.
- `feof()` never suspends (the zero-timeout liveness check skips the poll).
- `socket_select()`, `pcntl_waitpid()`, `pcntl_sigwaitinfo()` and `SO_RCVTIMEO` become suspendable,
  which TrueAsync does not provide today.
- Unsupported answered by a provider falls back to the core's synchronous wait, which matches what a
  scheduler needs in its own context.
