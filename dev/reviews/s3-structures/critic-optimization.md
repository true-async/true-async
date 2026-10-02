# Critic on optimization.md

1. HIGH. Tombstone vector makes B5 quadratic. Proposed rm scans from 0 (opt/vec_fuzz.c:25); under tombstones the
   k-th waiter's removal scans k+1. B5 (1000 waiters): target notifies at coroutine.c:668 in its own fiber (not
   scheduler context; ACT_AS_START only sets the acting coroutine F:2269); each waiter callback calls
   async_coroutine_resume, which cleans the waiter's events at once (coroutine.c:864-866) -> del_callback on the
   target during notify (coroutine.c:1026-1028 -> Fc:1665). Fork (Fc:1654-1676): self-removal swaps data[cur-1]
   with the last and decrements cur, so each removal finds its entry at index 0: 1000 compares per fan-in.
   Proposed: 500 500 scan steps per fan-in, ~500 steps (~1500-2000 instructions, estimate) per await. Fuzz never
   removes the running callback nor measures complexity. Wake order changes (fork A,C,B; proposal A,B,C; reference
   tests not checked). Spec gaps: the "tombstone written" flag has no home (bit 31 of capacity is the notify
   bit); compaction must run before notify drops its protective reference (Fc:1739).
   Fix: keep swap-removal during notify with a corrected cursor: removing i < cur: data[i] = data[cur-1];
   data[cur-1] = data[--len]; cur--; removing i >= cur: plain swap with the last. O(1), no tombstones, keeps the
   reference order. Cursor in a thread-local {vector*, uint32_t *cursor} saved/restored around nested notify;
   vector stays 16 B. Also fixes the fork defect (A B C D, remove A from B's handler -> A B B C, confirmed).
2. MEDIUM-HIGH. RFC status cannot express every waker state the fold needs. IGNORED is also set without
   cancelling on spawn failure paths: async_API.c:172, 181, 199; scheduler.c:1486, 1495 (e.g. a user
   SpawnStrategy::afterCoroutineEnqueue throws; reference disposes without running, coroutine.c:466, 492; fold:
   body runs). RESULT vs NO_STATUS has readers: coroutine.c:805 (resume of NO_STATUS outside scheduler context
   throws "has not been suspended"; under the fold both are RUNNING), coroutine.c:846 (was_waiting),
   coroutine.c:84 / Fc:869 (waker cleaned only when status != NO_STATUS). Fix: two spare bits
   ASYNC_COROUTINE_F_IGNORED (all seven IGNORED sites) and a "woken" bit for RESULT; define resume-on-RUNNING by it.
3. MEDIUM. Flat-edge rules: (a) "start/stop once per distinct event" scans earlier edges on every suspend
   (scheduler.c:1121) and wake (:1509): await_all over 10 000 futures ~5e7 compares; unnecessary, reactor
   start/stop are counted (libuv_reactor.c:351-375, loop_ref_count): start/stop once per edge; check non-reactor
   start/stop are idempotent or counted. (b) Event-side removal and reference ownership missing: fork finds the
   trigger by event when the awaitable disposes a callback (Fc:1009), one reference per distinct event
   (Fc:1240-1242). await_all([$f, $f]) via the array path registers two edges without dedupe
   (async_API.c:1002-1050; dedupe at :540 only on the iterator path) -> per-slot release double-releases -> freed
   while in use. Fix: each edge owns one reference (trans_event only for the edge passed with it); event-side
   dispose does edge->event = NULL; release (O(1)).
4. MEDIUM. Lazy suspend location reads a stale frame: fiber_context->execute_data is written at the switch
   (fiber_context_update_before_suspend at scheduler.c:400, 411, 519, 537), not at suspend; scheduler_next_tick
   runs microtasks, reactor callbacks and resolve_deadlocks (scheduler.c:1540-1590) before it;
   dump_deadlock_info -> coroutine_info prints "suspended at" (scheduler.c:841, coroutine.c:1079-1086) from the
   previous park or the post-body pin (coroutine.c:544). Reference captures at scheduler.c:1715 before the tick.
   Fix: one store execute_data = EG(current_execute_data) at suspend entry; read only when
   (SUSPENDED || QUEUED) && fiber_context.
5. MEDIUM. async_awaitable_from_object decodes a userland Completable (async.stub.php:9-13) as a coroutine:
   handlers->offset 0 -> base = zend_object, first word = GC refcount, bit 31 = 0 -> waiter pushed at offset 152 of
   a 56 B object; the debug assert itself dereferences garbage. Reference has the same hole. Fix:
   interface_gets_implemented on Awaitable rejecting non-extension classes, or reject handlers->offset == 0.
6. LOW. "One formula" for the object of an awaitable is false: coroutine: object_offset != 0, F_OBJ_REF bit 7
   selects indirection (R:166-171); event: bit 4 ZEND_OBJ selects object_offset vs ref_count; on a coroutine bit 4
   is F_CANCELLED. Fix: two helpers behind the bit-31 branch.
7. LOW. Bins: section 2.1 omits switch_handlers (consolidation.md:78): base 368/352 B, still 384 bin, 32 B
   headroom; O6 becomes 472/456 B = 512 bin, not 448; option B in 4.3 needs a counter (4 B per coroutine or global).

Holds: reference GC bug coroutine.c:310-318 (leak, not corruption); bin arithmetic (zend_object_properties_size
-16 for the final property-less Coroutine: 544->528->640, 360->344->384, 464->448->448; zend_alloc_sizes.h:48-52);
triggered_events and dtor have no writers; core writes coroutine flags only via masked SET_STATUS and |=; the
32-bit handle cannot carry a pointer (R:367-379; only caller zend_gc.c:2134-2140); coroutine/008 checks types only.
