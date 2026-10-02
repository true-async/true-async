# Frame-local wait records: Critic findings (2026-10-02)

Saved by the coordinator from the Critic's hand-back (the critic agent has no write tool). Keys as in
frame-records-optimizer.md; SF = structures-final.md, D = EDMOND-DECISIONS.md, Rg = RFC Zend/zend_gc.c. Read,
not run.

## Summary
1. Critical: suspend() can return false without anyone waking c (scheduler-context refusal, launch failure,
   switch-blocked if D14's check sits in suspend, the in-place-finalize exception path); 3.2 then returns with
   records still linked on a popped frame. The reference cleans exactly there (async.c:366-368).
2. Critical: a second wait by the same coroutine overwrites waker.wait, orphaning the first wait's records.
   Reachable through the RFC GC (awaits from any release once the root buffer is full, Rg:705-715, 2203-2223)
   and suspend's tick running microtasks, the reactor and deadlock resolution on c's stack with CURRENT == c.
3. High: unlinking typed waits at wake breaks the channel protocol: woken waiters stay queued until they run
   (channel.c:434-436); the rendezvous-cancel decision reads that membership (720-743): a send() that threw
   still delivers its value later.
4. High: U5's guarded-branch list is incomplete: fiber-context creation for a never-started coroutine allocates
   on c's stack after the link (scheduler.c:531-534, 647-650, 208); memory_limit there leaves records on dead
   stack; the debug catch-site assert fires.
5. High (S5): Traversable await_*: the iterator coroutine keeps linking after the waiter's frame returned
   (async_API.c:618, 786-798, 1130-1133): writes into a dead frame.
6. Med-High: a record that fires without waking its waiter (await_all item) stays in the target's vector;
   coroutine finalize frees the vector calling dispose on each element (coroutine.c:668-674; Fc:1768-1773);
   dispose shares a union with kind -> jump into data / debug assert on the first await_all over two coroutines.
7. Medium: nothing clears waker.result once the per-wait clean is gone; a value kept by the "write only while
   UNDEF" guard from an aborted wait is returned by the next await.
8. Medium: phase 1 can submit a frame op or arm a Timeout before any record can cancel it; early return or
   exception leaves the reactor pointing into a dead frame, or reintroduces timer work on calls that never
   suspend.
9. Medium: userland classes can implement Async\Completable (async.stub.php:9): kind_of reads past
   std_object_handlers.
10. Low/missing: U6 has no defined call site (after a fatal error no destructor runs, main.c:1486); no field
    for the awaiting-info vector; kind->completers cannot enumerate who holds a FutureState; liveness does not
    see zero-record parks by foreign callers.
11. Findings 1, 3 and 6 contradict "a frame-side del saves nothing": keep the waker-side unlink AND add
    `if (c->waker.wait) async_wait_unlink(c)` after every suspend(); typed kinds remove themselves frame-side.
12. Sound: 296 B / allocation 280 / bin 320, U4's placement, the ported zend_try sites, call_on_main_stack vs
    main's records, Fiber::resume vs an extension await, the zend_try cost.

## 1. Critical: suspend() returns false with no wake; records stay linked
3.2 assumes false = woken with an error. Reference paths that return false without a wake: launch failure
scheduler.c:1636-1639; scheduler-context refusal 1642-1645 (SF 2.9, structures-final.md:285-286, keeps it after
the link); cancelled-before-start coroutine finalized in place leaves EG(exception) set, 1601-1611 skip the
switch, 1745 returns false; TRY_HANDLE_SUSPEND_EXCEPTION_BOOL ignores the BAILOUT result of
switch_to_scheduler(NULL) (:83) so U4 never runs; D14's refusal if placed after the link. The reference survives
via zend_async_waker_clean() after a failed suspend (async.c:366-368) and after every await (:379); the report
removes it.
Scenario: `declare(ticks=1); $c = Async\spawn(fn() => Async\suspend()); register_tick_function(function() use
($c) { try { Async\await($c); } catch (Error $e) {} }); echo "x\n";` -> await links rec, suspend refuses (ticks
block switching, zend_vm_def.h:8105), frame returns; $c's notify later calls through reused stack; F_COUNTED
never decrements, deadlock resolution never runs again.
Fix: suspend() returns with waker.wait == NULL on every exit (`if (c->waker.wait) async_wait_unlink(c);`), or
that line in the frame after suspend(); move every refusal (no current coroutine, scheduler context,
zend_fiber_switch_blocked()) into phase 0, before the link.

## 2. Critical: a second wait by the same coroutine orphans the first wait's records
RFC GC awaits whenever ZEND_ASYNC_IS_ACTIVE && CURRENT != gc_coroutine (Rg:2203, 2223), no scheduler-context
check; auto GC from gc_possible_root_when_full (Rg:705-715) on any release that keeps references; the notify's
own hold/release of the awaitable is such a release (SF 2.4, Fc:1689, 1739); inside c's suspend() the tick runs
microtasks and the reactor with CURRENT == c, scheduler context true (scheduler.c:1525-1567), then
process_resumed_coroutines and resolve_deadlocks() with scheduler context false (1572-1590), which print via
php_printf (user output handlers). Scenario: c in await($x) with rec[0]; a microtask release fills the root
buffer; GC awaits, links recG, sets c->waker.wait = &recG; in scheduler context suspend refuses -> recG on dead
stack; outside it c parks, GC wake unlinks recG only, outer frame returns, rec[0] stays in $x->callbacks ->
crash on $x's next notify. Same shape with Fiber::suspend() from a destructor: a later resume unlinks the outer
records though $x never fired; outer await returns read_result(x) of an unfinished target.
Fix: phase 0 refuses when c->waker.wait != NULL or in scheduler context, before linking; the await slot returns
false without throwing; the GC returns 0 and its enqueued coroutine collects next tick (shape of D13). One
compare on the hot path.

## 3. High: the channel protocol needs woken waiters to stay queued
CHANNEL kind's unlink = channel_queue_remove at U1. Reference keeps a woken coroutine waiter queued until it runs
(channel.c:434-436); reservation counts rely on it (length > reserved_*, 403-404); post-wake code reads the
return of channel_queue_remove (720-743: "whether it is still there says which of two opposite things
happened"). Scenario: `$ch = new Channel(0); $c = spawn(function() use ($ch) { try { $ch->send("x",
timeout(20)); } catch (OperationCanceledException) { echo "cancelled\n"; } }); await($c);
var_dump($ch->recv());` -> sender's value already in the slot; Timeout's enqueue runs U1 removing it; after
resume was_queued == false read as "value taken"; reference discards it (738-742); frame model: recv() returns
"x" after send() failed. With two parked receivers, length > reserved undercounts; the deadlock timer may be
disarmed while the second waits (403-407). Fix: split the kind hook into abort (U4-U6: typed queue removal) and
wake (U1/U2: generic bookkeeping: F_COUNTED, ARM disarm); typed kinds remove themselves frame-side after resume
(the sketch's del).

## 4. High: U5 is an incomplete list
Fiber-context creation when the next queued coroutine never ran: pool pop or async_fiber_context_create(),
ecalloc on c's stack after the link (scheduler.c:531-534, 647-650 -> 208). Scenario: memory_limit at that
ecalloc -> E_ERROR bails out to c's catch (coroutine.c:546-553) above the records -> debug assert (wait == NULL)
instead of "Allowed memory size exhausted"; release: bailout_all_coroutines (scheduler.c:966-991) unwinds
targets whose finish notify reads c's record from reused stack. Fix: an invariant, not a list: every allocation
or foreign call between first link and switch runs inside a U5 zend_try; add the pool-miss branch (59
instructions per miss); or reserve the context at first enqueue (departs from D23, measure); debug: a global
"records unprotected" flag set at link, cleared at switch, asserted in the coroutine catch.

## 5. High (S5): Traversable await_*: the iterator coroutine writes into the dead frame
await_context is refcounted (async_API.c:562, 786-798); after the waiter returns nothing cancels or joins the
iterator coroutine (1130-1133, 1170); its handler keeps linking against waiting_coroutine (618). Scenario:
`function gen() { yield spawn(fn() => 1); Async\delay(10); yield spawn(fn() => Async\delay(1000)); } $r =
await_any_or_fail(gen());` -> waiter wakes on item 0, frame returns; 10 ms later item 1 is appended through the
dead compound record, and its firing spuriously wakes the waiter's next wait. Fix: refcounted heap context for
the compound record and chunks (reference shape; smaller saving), or cancel and join the iterator coroutine
before returning (behaviour change).

## 6. Medium-High: a fired-but-not-waking record stays in a vector the target tears down
Only TIMER clears rec->event on fire; an await_all item stays linked; coroutine finalize frees callbacks after
notify (coroutine.c:668-674) calling callback->dispose on each (Fc:1768-1773); for a record dispose shares a
union with kind -> call into .rodata (debug: assert). Scenario: `await_all([spawn(fn() => 1), spawn(fn() =>
delay(10))])`. Fix: every record whose target fires at most once removes itself in its callback (O(1) at the
cursor) and sets rec->event = NULL; teardown skips F_RECORD entries; better: no kind/dispose union, kind as its
own field (48 B per record, stack only).

## 7. Medium: stale waker.result preferred over the next wait's result
Reference resets the result in every post-await clean (coroutine.c:121-123). Scenario: c awaits $fut; another
coroutine does `$state->complete(1); $c->cancel();` -> resolve stores 1, U1 runs, cancel on the QUEUED
coroutine applies the error (SF 2.8 (7), structures-final.md:273-275), c throws, 1 stays; next
`await($g->getFuture())` prints int(1) instead of int(2). Fix: clear waker.result on suspend's error exit (cold
path).

## 8. Medium: phase-1 arming/submitting before any record can undo it
Invariant L puts arming a timer, submitting an op and the completed-target check in phase 1; U3 only unlinks.
(a) await_* with a C-level timeout (thread_pool.c:2071) submits its Timer op, then the item scan throws
(async_API.c:993-995): the reactor points into the dead frame. (b) `$t = timeout(100); await_any_or_fail([$done],
$t);` arms $t then finds $done complete: the timer fires into an empty vector, $t becomes a fired cancellation,
a later `await($x, $t)` throws at once; the reference never arms $t. Fix: all completion checks before any arm or
submit; submit and link back to back with nothing that can throw between, or count a submitted op as linked
for U3.

## 9. Medium: kind_of for userland Completables
Async\Completable is a plain interface (async.stub.php:9) with no interface_gets_implemented guard; await accepts
any Completable (async.c:302); kind_of reads past std_object_handlers; the push corrupts the heap. The reference
has the analogous bug (handlers->offset == 0). Fix: interface_gets_implemented rejecting non-internal classes
(no reference test implements Completable).

## 10. Low / missing
- U6 has no call site: after a fatal error php_error_cb marks every object destructed (main.c:1486), no dtor_obj
  runs; U6 could only run from free_obj in handle order (target may be freed first). Pin U6 to RSHUTDOWN before
  zend_deactivate. "Its destructor cancels it" (5.4) does not hold after a fatal error.
- Layout: the lazily allocated awaiting-info vector for foreign callers has no field (+8 B: 304 / 288, still
  bin 320); finally_handlers is superfluous in S3 under D21 (-8 B).
- S7: kind->completers for FUTURE and CHANNEL cannot be enumerated (no reverse references); S7 needs a
  reachability pass from the roots; a record contributes the waiter -> target edge and the type.
- Liveness (suspicion): a foreign extension parked through the RFC SUSPEND/ENQUEUE has no record, no F_COUNTED;
  a count-based deadlock decision may raise a false DeadlockError; the RFC gives foreign suspenders no way to
  declare an external wake source.
- Awaiting-info window: with the deferral, records stay visible after enqueue, against R:64 (diagnostics only).
- Line: the await_* context allocation is async_API.c:956, not 951.

## Previous Critic (passive edges) under this model
0A dump: answered for records, zero-record parks get a generic line. 0B S7: type tag answered, completer
enumeration not. 0C: unchanged. Lost cleanup at destroy: partly (4, 1, 10 open). 1 F_COUNTED: holds only if U1
or U6 always runs; leaks on finding 1's paths. 1/5 Timeout liveness: answered. 2/7 eager Timeout: answered for a
single await, reintroduced by phase-1 arming in await_* (8). 3, 4: unchanged. 12: leak fixed, the guard creates
finding 7.

## Sound
296 B, allocation 280, bin 320 (zend_object_alloc memset 240 B); U4 placement (scheduler.c:1718-1722);
bailout_all_coroutines switches into every started parked coroutine (966-968, 989-991); ported zend_try sites
(thread_pool.c:248-257) in a callee of the records' frame; async_call_on_main_stack (scheduler.c:174-177) below
main's saved SP; Fiber::resume cannot wake a fiber parked in an extension await (Rf:899 vs 1428); the zend_try
cost method.
