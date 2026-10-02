# Critic on consolidation.md

Keys: R = RFC zend_async_API.h, Ra its .c, Rf RFC zend_fibers.c, F/Fc fork header/.c; bare names = php-async 1fdacf8.

1. CRITICAL. Report contradicts EDMOND-DECISIONS 3 and 4. Decision 3: bit 8 back to spare; ASYNC_COROUTINE_F_STARTED
   needs a bit in 16-30 (renumber 16-20); no IS_SCHEDULED; IS_STARTED stays status != CREATED; Rf:770, ts untouched;
   main: core sets only MAIN+RUNNING (Ra:737-738), the extension sets ASYNC F_STARTED in its launch slot
   (reference scheduler.c:1246) and on every re-minted main (RFC.md:154-156; S3.md section 6 call 1); section 10
   item 1 becomes comment-only. Decision 4: polarity A; every event constructor ORs bit 31; reference prefix
   0x80 | 1u<<31; coroutine allocator does nothing; debug ZEND_ASSERT at generic wait entries.
   Silent hazard: ported code calling ZEND_COROUTINE_IS_STARTED/SET_STARTED compiles against R:189-190 with the
   other meaning: coroutine.c:1582 left as is -> spawn(fn) -> isStarted() true, fails coroutine/005, 028:109.
   Fix: coroutine.c:956, 1582, 1605, scheduler.c:916 -> ASYNC_COROUTINE_IS_STARTED; coroutine.c:524,
   scheduler.c:1246 -> ASYNC_COROUTINE_SET_STARTED; scheduler.c:1468 see 4; grep gate: no
   ZEND_COROUTINE_(IS|SET)_STARTED in extension sources.
2. HIGH. RFC suspend and enqueue_coroutine slots unmapped. R:255-261 suspend parks (-> SUSPENDED) until someone
   enqueues; core relies on the bare park at Rf:911 (Fiber::suspend), Rf:965 (zend_fiber_await), zend_gc.c:2163.
   Reference suspend with empty waker and not queued destroys the waker and returns true at once
   (scheduler.c:1664-1668). R:248-254 enqueue = fresh enqueue and resume with optional error; reference splits:
   async_scheduler_coroutine_enqueue (scheduler.c:1450-1489, no error) and async_coroutine_resume
   (coroutine.c:800-869, refuses NO_STATUS waker outside scheduler context :804-808; fresh = NO_STATUS Fc:888).
   Scenario fiber/019: start() returns before the body ran, isStarted "no", resume() throws FiberError; GC
   destructors skipped likewise. Fix: core-called suspend always parks (waker WAITING, status SUSPENDED, even
   with zero events); enqueue(c, error) dispatches: CREATED -> first-enqueue bookkeeping; SUSPENDED -> resume
   with error; QUEUED -> merge error only; else refuse.
3. HIGH. Section 7 "pending" rule drops the deadlock cancellation (pending = waker.error or
   deferred_cancellation; drop while pending). Reference: protected cancel returns at coroutine.c:950 before
   SET_CANCELLED :954; after deadlock/shutdown clear PROTECTED (scheduler.c:867-868, 924-925) :1000 applies the new
   cancel. Scenario: two coroutines each in protect() awaiting the other, both cancelled (deferred); reference:
   detector clears PROTECTED, cancels both, both unwind, DeadlockError; section 7: both dropped -> hang or leak.
   Fix = reference rule: protected: store into deferred_cancellation only if NULL; unprotected: drop iff
   F_CANCELLED && waker.error is a cancellation (coroutine.c:994-998), else APPLY_CANCELLATION (Fc:1344-1367).
4. HIGH. scheduler.c:1468 first-enqueue test dies if the RFC status is written where the reference writes the
   waker (:1464 waker->status = QUEUED, then :1468 !IS_STARTED && not in ASYNC_G(coroutines)). Scenario:
   spawn+await: never inserted in ASYNC_G(coroutines); finish: zend_hash_index_del fails (coroutine.c:759-760)
   -> E_CORE_ERROR "Failed to remove coroutine from the list" on the first coroutine; spawn location not
   recorded (coroutine/007). Fix: first = (STATUS == CREATED) before the write; drop the hash lookup at :1469.
5. MEDIUM-HIGH. Missing cast site: scheduler.c:732-733 calls trigger->event->info(trigger->event); for a
   coroutine target offset 88 is inside internal_context -> SIGSEGV with edge_cases/003 and
   true_async.debug_deadlock=1. Reference get_awaiting_info returns [] (async_API.c:248-252). Fix: add
   scheduler.c:730-737 with type-bit dispatch to async_coroutine_info(); same in get_awaiting_info or keep NULL.
6. MEDIUM. Finish-handler storage: R:375-379 handle is uint32_t (pointer does not fit); swap removal (Fc:1676);
   fork iterator fix-up only decrements (Fc:1654-1659): [H1..H4], remove H1 while H3 runs -> H3 twice, H4 never;
   breaks R:83 "fires exactly once". Each RFC handler needs a heap wrapper {base, fn, waiter, data, id}: one
   emalloc per add. Fix: uint32 id from a counter skipping 0, remove by trampoline+id; refuse add/remove during
   notify as ts does (ts:337-340, 369-372), or fix the adjustment.
7. MEDIUM. is_bailout (R:88) of finish handlers: reference sets BAILOUT only at scheduler.c:970, 987, not for
   queued coroutines switched with the BAILOUT transfer flag (:966-968) nor in coroutine.c:547-552. Fix:
   is_bailout = bit || a scheduler-wide "bailing out" flag set in the execute catch and bailout_all.
8. MEDIUM-LOW. Embedded internal_context teardown: reference disposes at finalize (coroutine.c:676-678) and destroy
   (:212-214); Ra:186-189 is a bare zend_hash_destroy. Both -> double free; finalize only -> get_gc walks a
   destroyed table (coroutine.c:280-286). Fix: zend_hash_clean at finalize, destroy only in free_obj.
9. LOW. isSuspended on the current coroutine is true in the reference (waker NO_STATUS after clean,
   coroutine.c:522, Fc:921) and getTrace returns a stale frame; new formulas give false/null; no test pins it;
   P2.2 wants a DECISIONS line. Factual errors in the report: protect/003 does nest (no cancel); DECISIONS.md has
   no "16 STARTED, 17 YIELD" entry. Polarity A: ~12 zero-allocated event constructors (pecalloc in
   libuv_reactor.c etc.) rely on flags 0: from S4 use one async_event_init().

Sound: RESULT_USED needs no bit (only reader future.c:586 via F:1941); ZVAL_RESULT needs no bit if every callback
dispatches on the type bit before reading event->flags; method formulas pass coroutine/005, 013, 028, 029, 038,
009, 037, info/002 (fiber/019 after 2); the core never clobbers 16-31 (Ra:737-738, Rf:1011 mask/OR); the 9.4 macro
list matches a grep; section 10 items 2-5 correct.
