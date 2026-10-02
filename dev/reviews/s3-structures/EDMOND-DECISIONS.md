# Edmond's decisions after BRIEF.md (they override the brief and both expert reports)

1. EH_THROW window: the core saves EG(error_handling)/EG(exception_class) per switch in zend_fiber_vm_state,
   as the fork does (S3.2). No extension fields. Criterion: speed (equal instructions, 16 B less per coroutine).
2. S3 performance Done-when: at most 3 % more `instructions:u` per operation than the reference 1fdacf8 on
   B1-B5; allocations per operation not above the reference. Edmond's key criterion is speed.
3. REVISED (Edmond, later the same day; supersedes the earlier "no core flag" version): the RFC is wrong,
   IS_STARTED must mean "the body began executing", which the status cannot express. The core gets
   `ZEND_COROUTINE_F_STARTED` (bit 8); `ZEND_COROUTINE_IS_STARTED` reads the flag. RFC contract: the scheduler
   sets it immediately before the body's first instruction (in-place run and main included); a coroutine
   cancelled before it ran (first entry with an error) never gets it. Core changes (S3.2): zend_fibers.c:770
   cancels when !FINISHED; the failed-enqueue branch zend_fibers.c:1038-1039 releases without cancelling;
   test_scheduler sets the flag and its five callers follow. The extension uses the RFC bit and has NO STARTED
   bit of its own; ported ZEND_COROUTINE_IS_STARTED/SET_STARTED keep their meaning.
4. Type bit polarity: bit 31 = 0 means coroutine, 1 means event. Every event constructor MUST set bit 31,
   including ported code that memsets and then assigns flags wholesale (it must OR the bit in). A debug-build
   ZEND_ASSERT at each generic wait entry checks the object's class against the decoded type.
5. F_CANCELLED keeps the RFC meaning "cancellation requested" (scheduler_rfc.md:138 confirms); the
   zend_fibers.c:1375 term is dropped (extended_data == NULL suffices). No CANCEL_REQUESTED bit.
6. F_YIELD dropped; isSuspended = !FINISHED && (SUSPENDED || QUEUED); the fiber deadlock exemption reads
   fiber->context.status.
7. Nested protect() is a bug (reference async.c:248-293): fix with was_protected; also to be fixed in php-async.
8. The scheduler RFC defines no PHP-visible names (scheduler_rfc.md:45-46, 350-353), so no \Cancellation in
   the core. DECIDED: `Async\AsyncCancellation extends \Error` (catch (\Exception) does not swallow it).
9. Fibers are in S3: intercept_fiber and the fiber tests (up to 23) are taken into S3. DECIDED.
10. DEFERRED to 2026-10-02 (Edmond): question 7, waits where switching is blocked (GC await from a blocked context). Do not decide it in the reports; list it as open.
11. Scope: a `zend_async_scope_t *scope`-style pointer (8 B, name from the fork) goes into async_coroutine_t NOW, plus a hook point in spawn; the Scope logic itself stays in S9. DECIDED.
12. Type names: base event `async_event_t`; every fork type with the `zend_async_` prefix keeps its suffix and takes the `async_` prefix in the extension. DECIDED.
13. (2026-10-02) Question 7.1, GC where switching is blocked: the GC starts its coroutine (new_gc_coroutine,
   zend_gc.c:2181-2197; enqueue needs no switch) and returns 0 without awaiting; the collection runs at the next
   tick. The root still lands in the buffer (zend_gc.c:724-735). Side effect to settle in S3.2:
   gc_adjust_threshold(0) raises the threshold by a step (:681-692). Core change on async-core.
14. (2026-10-02) WITHDRAWN 2026-10-02: Edmond says this was not his decision but Claude's error; the extension never reads zend_fiber_switch_blocked(). Question 7.2: Async\suspend() and Async\await() where switching is blocked throw Error, checked by
   the extension through ZEND_API zend_fiber_switch_blocked() (zend_fibers.c:412), as the Fiber methods do
   (:1314, 1370, 1397, 1421, 1464). No core change.
15. (2026-10-02) Questions 7.3 and 7.4 dropped: no rule for from_main and the block counter, no reset hook.
   A bailout crosses fibers through ZEND_FIBER_TRANSFER_FLAG_BAILOUT in both cores (fork zend_fibers.c:651-652,
   694-697; RFC 648, 690-691); the switch-block counter is reset per request only, in the fork as in the RFC
   (fork :2022, RFC :1621), so a block leaked by a fatal error behaves as in plain PHP. No core change.
16. (2026-10-02) Question 10, exit() in a coroutine: as TrueAsync (graceful shutdown: every other coroutine gets a
   catchable AsyncCancellation("Graceful shutdown"), reference scheduler.c:891-907, 1005-1030), plus a deadline:
   after an exit() the scheduler gives the coroutines 5 seconds to finish; what is still alive then is unwound
   without a catchable exception (finally blocks run). The deadline needs a clock in the scheduler loop and a
   reactor timeout, so it lands with the reactor (S4); S3 records the rule.
17. (2026-10-02) O6, inline zend_fcall_t: the default is TrueAsync's layout, a separate fcall block from spawn until
   the Coroutine object is destroyed (fork zend_async_API.h:1870-1871; reference coroutine.c:164, 183-185).
   O6 (one allocation fewer per spawn, 512 bin against 384 + 112) is taken only if B1 measures it faster.
   Edmond: he trusts the previous code; departures from it need a measurement.
18. (2026-10-02) isSuspended() on the running coroutine returns true in the reference (coroutine.c:1609-1619, the
   cleaned waker reads as "not yet resulted"): a bug. True Async: false while RUNNING; getSuspendLocation()
   then gives "unknown" (taken by the coordinator, not asked). To be fixed in php-async as well.
19. (2026-10-02) enqueue on a FINISHED coroutine: false with an Error, as TrueAsync. Core call sites checked:
   zend_fibers.c:1440, 1477 expect false-with-exception (RETURN_THROWS); :1038 releases and lets it propagate;
   :876, 881, 908 ignore the result but wake a caller parked in zend_fiber_await, which cannot be FINISHED.
   RFC comment (zend_async_API.h:248) gains the FINISHED case (S3.2).
20. (2026-10-02) Priority. GC coroutines follow the RFC: no special treatment, FIFO like every coroutine
   (scheduler_rfc.md:570, 631 say only "a dedicated coroutine"; the gc_new_coroutine slot leaves priority to the
   scheduler). The no-op asHiPriority() (reference coroutine.c:1434-1439, a TODO) is a bug: it puts the coroutine
   at the front of the run queue, as TrueAsync's internal priority does (async_API.c:155-162). Priority is mainly
   for internal use. A reference test that depends on the old order may be changed.
21. (2026-10-02) Coroutine::finally() moves to S9 with Scope: it runs as in TrueAsync, through an iterator in a
   child scope (reference coroutine.c:1275-1303). No interim implementation in S3 ("no crutches", Edmond); its
   tests leave the S3 list for that stage's list.
22. (2026-10-02) Per-coroutine output buffers: not now (PLAN S10). The output_buffer tests stay out of S3
   (6 needs-core:, 2 component:S6).
23. (2026-10-02) Fiber context pool: TrueAsync's policy (keep at least ASYNC_FIBER_POOL_SIZE = 4, grow while the
   pool holds fewer contexts than the run queue, destroy the rest; reference scheduler.c:465-485, scheduler.h:22).
   The high-water-mark pool of the note is measured against it on an await chain before any change.
24. (2026-10-02) ASYNC_COROUTINE_F_BAILOUT (bit 19) stays in the extension. The RFC expresses bailout as an
   argument at the event (finish handler is_bailout, R:83-90; suspend(from_main, is_bailout), R:260-261) and the
   fiber keeps ZEND_FIBER_FLAG_BAILOUT (Rf:803-825); no core code reads a per-coroutine bailout state.
25. (2026-10-02) OPEN. The passive-edge model (passive-edges-optimizer.md) is not accepted: Edmond disagrees that
   generic code always owns a PHP object and that info can be the class name; not every event has a PHP class.
   To be thought through; the event's methods stay undecided (S4 designs events).
26. (2026-10-02) Subscriptions are removed at once when a coroutine is woken, always (the reference deferred it
   through resumed_coroutines when woken in scheduler context, coroutine.c:861-866, scheduler.c:426-436, so a
   queued coroutine could "catch" more events). Waits that gather several results (await_all, await_any_of(n))
   keep catching through their own records until complete; single-result waits must not take a second event
   (it overwrote and leaked the first, fork zend_async_API.c:1256-1260, and a channel value would be lost).
   Measured in S4: N delay() waiters fired in one tick, with and without the deferral.
27. (2026-10-02) await_* over a Traversable (S5): when the waiter leaves (satisfied, cancelled, timed out) the
   iterator coroutine is cancelled. The reference ran the Traversable to its end and kept linking the departed
   waiter (async_API.c:618, 1130-1133).
28. (2026-10-02) APPROVED by Edmond: the wait model of wait-model-sage.md section 2 (wait records on the waiting
   frame's stack; waker = error, result, wait, wait_count, 40 B; unlink by the waker at every exit of suspend();
   no event methods; the record is the RFC awaiting-info registration). A prior-art review against other
   languages follows before it is written into S3.md.
29. (2026-10-02) Channel waiters (S9): the waiter structure of a parked send/recv lives on the waiting frame, not
   on the heap (reference channel.c:692); the CHANNEL wait kind's abort hook removes it from the channel queue
   when the frame never runs again (bailout, shutdown). One allocation fewer per parked send/recv; measured on
   channel benchmarks against the reference.
30. (2026-10-02) Names accepted: async_wait_kind_t, async_wait_unlink(), async_wait_abort(), async_wait_begin(),
   ASYNC_CALLBACK_F_RECORD, ASYNC_CALLBACK_F_COUNTED, ASYNC_CALLBACK_F_TYPED, the coroutine field awaiting_info,
   ASYNC_G(wait_unprotected).
31. (2026-10-02) Second wait inside the tick and waits in scheduler context: TrueAsync's flag, not the fiber switch
   block. The fork's ZEND_ASYNC_G(in_scheduler_context) (fork zend_async_API.h:2195, 2255-2256) becomes
   ASYNC_G(in_scheduler_context) with ASYNC_SCHEDULER_CONTEXT / ASYNC_IS_SCHEDULER_CONTEXT; async_wait_begin()
   refuses on it before linking with the reference's text (async.c:62-66); the flag covers the whole tick incl.
   deadlock resolution (the reference clears it early, scheduler.c:1572); the RFC await slot returns false in it
   and the GC only starts its coroutine (D13). The flag stays in the extension (left to the coordinator by
   Edmond). His concern, recorded for B1-B5: TLS of a dynamically loaded module (DLL on Windows, dlopen'ed .so)
   may cost a few cycles more than the core's; measure extension-global access on the ZTS build and move hot
   state into the RFC core if it shows.
32. (2026-10-02) Timeout semantics: option B of timeout-semantics.md. timeout() fixes an absolute deadline when it
   returns (the loop clock refreshed there); the reactor timer is armed only while someone waits, for the
   remaining time; every await on one Timeout shares that deadline; a fired Timeout stays fired. Name unchanged.
   The reference restarted the full time at each await, and only when nobody else waited (libuv_reactor.c:351-374).
   Measured before final: the extra clock read in timeout() (about 24 instructions) against D2.
33. (2026-10-02) Cancellation is delivered once (edge-triggered), as TrueAsync: after a coroutine catches its
   cancellation, its next await waits normally; a new cancel() delivers again (coroutine.c:994-1003). Awaiting a
   finished cancelled coroutine from outside replays its exception every time. Trio's level-triggered model
   (prior-art 13.13) not taken.
34. (2026-10-02) S3 test coverage, seven layers: the S3 list; an own test per decision and per Critic case; fault
   injection through --enable-true-async-test-hooks (debug builds); debug asserts plus an ASAN
   detect_stack_use_after_return run; pocs-dbg-cov with every wait-model line covered except listed OOM
   branches; Mull survivors in wait/scheduler code killed or explained; blind tests by test-author. Layers 3, 5,
   6 are hard conditions of S3's Done when.
35. (2026-10-02) asHiPriority() on a coroutine already in the run queue does nothing to its place (no O(n) scan);
   it sets a flag, and the coroutine's next enqueue puts it at the front. spawn(...)->asHiPriority() therefore
   does not overtake coroutines queued before it on the first run.
36. (2026-10-02) RFC comment of the await slot (R:323-329) gains a third false case in S3.2: "or the wait is not
   possible here (the scheduler is running its own work): false without an exception; the caller does not wait."
37. (2026-10-02) Names accepted: async_awaitable_t, ASYNC_AWAITABLE_F_EVENT, ASYNC_AWAITABLE_IS_COROUTINE(),
   async_event_init(), async_finish_handler_callback_t.
38. (2026-10-02) The scheduler-context flag and the exit exception use the RFC core's existing slots,
   ZEND_ASYNC_IN_SCHEDULER_CONTEXT and ZEND_ASYNC_EXIT_EXCEPTION (R:652-666, 694-696); the extension keeps no
   copies. Revises D31's "stays in the extension". Lets the core's Fiber methods refuse in the tick
   (s3-note-critic.md 3, 4).
