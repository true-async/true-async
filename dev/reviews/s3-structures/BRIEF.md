# Brief: True Async S3 data structures

True Async is a new PHP extension (repo /home/user/true-async) that implements the scheduler on the
scheduler RFC core (php-src branch async-core-io, commit 834811f2d88). It adapts code of the old
TrueAsync extension ext/async (repo /home/user/php-async, HEAD = 1fdacf8, the "reference"), which was
written against the fork's core (php-src branch true-async-stable, "the fork"). Edmond is the author
of both the old TrueAsync and the scheduler RFC, so the RFC core can be changed where it is wrong.

## Edmond's task
1. Consolidate the logic of old TrueAsync with the new RFC: every concept of the fork/reference maps
   to an RFC field, an extension field, a core change, or nothing, with the reason.
2. Optimise the data structures so they carry nothing superfluous (performance criterion: allocations,
   copies, pointer chasing, bytes per coroutine on spawn, enqueue, switch, suspend, await).
3. Reserve ONE type bit: bit 31 of the `uint32_t flags` at offset 0 of every awaitable. 0 = coroutine
   (`zend_coroutine_t.flags`, the first field of `async_coroutine_t`), 1 = event. The event is the base
   of almost everything else: Future, timers, IO, channels, etc.

## Already agreed with Edmond (do not reopen without a failing scenario)
- The coroutine does not embed the fork's 104-byte `zend_async_event_t`: `zend_coroutine_t` already has
  flags and the object offset. The coroutine keeps a waiters vector itself.
- `zend_coroutine_t.flags`: bits 0-3 status, 4-15 core (`F_CANCELLED` 4, `F_MAIN` 5, `F_FIBER` 6,
  `F_OBJ_REF` 7, 8-15 spare), 16-31 the scheduler's.
- RFC defect: `ZEND_COROUTINE_IS_STARTED` is `status != CREATED`, but enqueue moves CREATED->QUEUED
  before the first run. Fix in the core (step S3.2): flag `ZEND_COROUTINE_F_STARTED` (bit 8) set at the
  first switch in. The extension has no STARTED bit.
- `F_CANCELLED` keeps the RFC meaning "cancellation requested"; the core's `zend_fibers.c:1375` check
  (treats it as force-closed fiber) gets fixed in S3.2. The extension has no CANCEL_REQUESTED bit.
- The fork's `F_YIELD` ("fiber stopped in Fiber::suspend()") is not carried into the RFC: the RFC core
  tracks that via `fiber->context.status` (`zend_fibers.c:1221-1225`). Remove it unless a TrueAsync
  method cannot be answered without a bit (check `coroutine/028`: isQueued and isSuspended both true
  after `Async\suspend()`).
- Field and flag names come from the reference/fork; only the prefix changes where the core already
  owns a `ZEND_` name (extension prefix `ASYNC_`). Do not invent names.
- Scope, context (S9), reactor, channels, threads are out of S3.
- Open: EH_THROW window (`EG(error_handling)`, `EG(exception_class)`) saved by the core per switch as the
  fork does (`fork/zend_fibers.c` zend_fiber_vm_state), or two fields in `async_coroutine_t`.

## Sources (cite file:line)
S = /tmp/claude-0/-home-user/f1d5bd38-7c5c-5b5d-92a4-b889f4a4b4c2/scratchpad/critic
- RFC core: S/rfc-core/Zend/zend_async_API.h (coroutine struct :104), zend_async_API.c, zend_fibers.c,
  S/rfc-core/ext/test_scheduler/test_scheduler.c (reference C scheduler on the RFC API).
- Fork core: S/fork/zend_async_API.h (event :902, callbacks vector ~:883, callback structs :830-870,
  waker, coroutine :1747, coroutine flags :1799), S/fork/zend_fibers.c.
- Reference extension: /home/user/php-async (coroutine.h/.c, scheduler.c, async.c, internal/, tests/).
- RFC text: /home/user/php-async-core-rfc/scheduler_rfc.md.
- Current S3 note (draft 3, partly superseded by the above): /home/user/true-async/dev/plans/S3.md;
  decisions: /home/user/true-async/dev/DECISIONS.md; principles: /home/user/true-async/dev/PRINCIPLES.md.
- Measured sizes (gdb ptype /o, x86-64): RFC zend_coroutine_t 144 B, fork zend_async_event_t 104 B,
  fork zend_async_waker_t 248 B (field `triggered_events` unused in S3), zend_object 56 B, reference
  async_coroutine_t 544 B (in the 640 B ZendMM bin).

Write in English. Quote sources by file:line. Mark anything not verified as "assumption, not checked".
