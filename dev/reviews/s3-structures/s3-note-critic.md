# S3 note critic, round on the agreed S3.md (2026-10-02)

SUMMARY (10 lines)
1. HIGH: `suspend()` (4.2) has no rule for the case where the run queue's next entry is the suspending coroutine itself. `coroutine/028` reaches it twice and B3 on every iteration, so step 4 switches into the running context. Separately, 4.3 files U2 under RUNNING, but a coroutine inside its own tick is SUSPENDED, so U2 can never be reached.
2. HIGH: the tick never says what happens to a PHP exception raised inside it, and `suspend()` neither saves EG(exception) on entry nor returns false while one is pending. EG(exception) is not part of `zend_fiber_vm_state`, so it crosses the switch and the next coroutine's body silently never runs.
3. MED-HIGH: wrong premise about the core. The RFC core already has `ZEND_ASYNC_G(in_scheduler_context)`, `exit_exception` and `active_coroutine_count` (R:652-666, 694-696), and ts writes them. The note "moves" the flag into the extension, plans a TLS measurement whose answer is already known, and never says which exit-exception slot it uses.
4. MED: `Fiber::start/resume/throw/suspend` inside the tick. The core wakes the caller or queues the fiber before it calls the suspend slot, so the refusal 4.6 promises leaves a switch half-done. This needs an S3.2 core change, which in turn depends on item 3.
5. MED: S3.2 item 5 (D5) predictably breaks `fiber/030`, which is in the S3 list. ts.c:1558 and :1451 still treat CANCELLED as final, so item 5's own core test cannot pass on `ext/test_scheduler`. A ts.c change is missing from S3.2.
6. MED: `asHiPriority` (D35) needs a flag that the flags table does not allocate. "Next enqueue" can mean once or always, and the method row reads as "enqueue now".
7. MED: a GC run declined in scheduler context also raises the GC threshold by 10 000. S3.2 item 8 fixes only the switch-blocked case.
8. MED: `Async\suspend()` is missing from the phase-0 list. Under a blocked switch it queues itself, gets refused, and the next `await()` returns NULL early.
9. MED-LOW: the D2 verdict mixes in the core's own cost (unguarded observer call on every switch in the RFC core). Several rows in section 12 have no pass/fail threshold.
10. LOW: the "never set while PROTECTED" rule is broken by cancel step 2; fault injection covers 2 of the 6 unlink sites PLAN requires; the inline-vector test clashes with the notify bit; behaviour is pinned by excluded tests; D36 sits in two lists; state after `from_main` call 2 is undefined.

Verified against: RFC core (scratchpad `rfc-core/` and the full tree `opt/m/rfctree/`), fork `/home/user/php-src-true-async`, reference `/home/user/php-async`. I traced the code by reading; I did not build or run anything.

---

## 1. HIGH: the coroutine that suspends can be the next one in its own run queue, and nothing handles it

**Defect.** 4.2 step 4 (S3.md:396-398) only takes the no-switch path when `S == RUNNING`. Otherwise it switches to whatever the tick pops. Two ordinary paths leave the current coroutine in the queue:
- **Yield.** `Async\suspend()` pushes the current coroutine (4.3, S3.md:425-426), and step 2 keeps it QUEUED.
- **Woken in its own tick.** 4.3 picks the row by status. The coroutine in its tick is SUSPENDED (step 2, S3.md:389), so a wake goes to the SUSPENDED row ("push, QUEUED", S3.md:423). The U2 short path is listed under RUNNING (S3.md:425), which a coroutine in its own tick never is. 4.2 step 3 (S3.md:394) and the U2 row (S3.md:451) assume U2 fires; the dispatch table makes that impossible.

Switching into yourself trips `ZEND_ASSERT(to != from)` at zend_fibers.c:483. Release builds jump into the live context.

**Failing scenarios (day one):**
- **`coroutine/028` (in the list; it pins most of the methods table).** Line 53: `$exception_coroutine` yields while main is parked in `await` and every other coroutine has finished, so the queue is `[exc]`. Line 72: main's `suspend()` with the queue `[main]`. Both pop the current coroutine.
- **B3** ("one coroutine suspending alone", S3.md:922) does this on every iteration. The note calls it "the tick without a switch", but 4.2 defines no such path.
- **Cancel-before-run, then await:** `$c = spawn(fn() => 1); $c->cancel(); await($c);`
  - main links its record and suspends.
  - The tick finalizes `$c` in place.
  - The notify wakes main, and the SUSPENDED row pushes it.
  - Main is the only queue entry, so it is popped and the switch goes into itself.

**Fix:**
- In enqueue, test `c == current && ASYNC_IS_SCHEDULER_CONTEXT` before dispatching on status (as the reference does at coroutine.c:845-852): unlink, `S = RUNNING`, no push.
- In the tick: `next = pop(); if (next == c) { S = RUNNING; goto out; }`. Skip FINISHED entries too: a yielded coroutine that U5 finalizes during its own tick stays in the queue.
- Cancel step 7 ("push only if SUSPENDED", S3.md:629-630) has to go through the same check.

## 2. HIGH: exceptions in the tick, and a pending EG(exception) at suspend entry, are undefined

**Defect.** The tick runs PHP code (microtasks, in-place finalize, which releases closures and so runs destructors, and the deadlock report's output handlers). U5 covers bailouts only. Nothing in 4.2 says what happens to a thrown exception. Step 5 mentions "the tick's false return" (S3.md:400-401, 407-408), but no tick return value is defined anywhere. Step 6 returns `true` whenever `waker.error` is NULL, even with EG(exception) set.

The reference saves EG(exception) on entry and restores it on exit (scheduler.c:1630, 1743). It sends tick exceptions to graceful shutdown (`TRY_HANDLE_SUSPEND_EXCEPTION_BOOL`, scheduler.c:79-88) and returns `*exception_ptr == NULL`. The note copies `zend_exception_save_fast` (S3.md:861) but never places it in the contract.

`zend_fiber_vm_state` (RFC zend_fibers.c:106-152) does not save EG(exception), so a pending exception follows the switch. `zend_call_function` returns SUCCESS without running anything while EG(exception) is set (zend_execute_API.c:923-928).

**Scenarios:**
- **Throwing destructor in the tick.**
  1. `$o` has a `__destruct` that throws. Run `$c = spawn(function() use ($o) {}); unset($o); $c->cancel(); await($d);`.
  2. The in-place finalize of `$c` releases the closure; `__destruct` throws in main's tick.
  3. The tick switches to `$d` with EG(exception) set.
  4. `$d`'s body never runs (`zend_call_function` returns immediately). Its finalize adopts main's exception as its own (as coroutine.c:629-635 does), and `await($d)` in main returns `$d`'s "result".
- **GC while an exception unwinds** (derived from reading). An exception unwinds a frame, and freeing a CV fills the root buffer. Then `gc_possible_root_when_full` (zend_gc.c:713-715) → `zend_gc_collect_cycles` → `ZEND_ASYNC_AWAIT` (zend_gc.c:2223), which calls `suspend()` with EG(exception) pending. The next coroutine on the queue then starts with that exception.

**Fix:**
- Step 0b: `zend_exception_save_fast` on entry; restore on exit.
- Tick rule: an exception raised by tick work becomes the exit exception and starts graceful shutdown, as in the reference. The alternative, making it c's exception and returning false, is the other defensible choice; pick one in the note.
- Step 6: `return EG(exception) == NULL`.
- A pool miss whose mmap fails throws an exception, not a bailout (zend_fibers.c:217-289). It needs the same rule: abort c's wait, then apply the tick rule.

## 3. MED-HIGH: the RFC core already owns the scheduler-context flag and the exit-exception slot

**Fact.** R:652-666 defines `in_scheduler_context`, `exit_exception` and `active_coroutine_count` in `ZEND_ASYNC_G`, with macros at R:694-696. ts writes them: `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` at test_scheduler.c:532, 934, 947, 1096-1127, 1338, 1552, and `ZEND_ASYNC_EXIT_EXCEPTION` at 899-905, 1242-1244 and 1939-1941. No core C file reads them (grep over rfctree Zend/ and main/).

**Where the note goes wrong:**
- 4.6 (S3.md:498-500) and D31 describe the flag as the fork's, "moved into the extension".
- Section 12 (S3.md:946) measures ZTS TLS cost to decide "moving hot state into the RFC core". The slot is already in the core, accessed through `ZEND_TSRMG_FAST`.
- RSHUTDOWN "drops the exit exception" (S3.md:704) and section 6 says DeadlockError "becomes the exit exception", without saying which slot. main.c only prints EG(exception) (main.c:2632, 1942), so the hand-over from the exit exception to EG(exception) at the end of each `from_main` call is also undefined.

**Cost of two flags.** Under True Async the core's flag is always false, so no core check can see the tick (needed by finding 4). Any extension that reads `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` gets a wrong answer.

**Fix:**
- Write `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` and `ZEND_ASYNC_EXIT_EXCEPTION` and keep no extension copies. This needs no RFC change; it is still TrueAsync's flag, just stored in the core's slot.
- Drop the TLS row for this flag. State whether `active_coroutine_count` is kept up to date.
- D31 says "stays in the extension", so this needs Edmond's confirmation.

## 4. MED: Fiber methods inside the tick end half-done; the claimed `Error` is not what happens

**Defect.** 4.6 (S3.md:507-509) says `Fiber::suspend()` inside the tick "gets the same Error". But the core changes state before it calls the suspend slot:
- `zend_fiber_coroutine_yield` clears `caller_coroutine`, sets the fiber SUSPENDED and enqueues the caller (zend_fibers.c:897-908) before `ZEND_ASYNC_SUSPEND()` at :911.
- `Fiber::resume/throw` enqueue the fiber coroutine (:1440, :1477) before `zend_fiber_await` suspends.
- `Fiber::start` enqueues the fiber coroutine (:1038).

**Scenario.**
1. A fiber body calls `Async\await($x)`.
2. In its tick, an in-place finalize runs a destructor that calls `Fiber::suspend(1)`.
3. Its caller is woken: `$f->start()` returns 1, but `Fiber::suspend` threw `Error` and the fiber is still parked in `await`.
4. The fiber's status was reset to RUNNING (:920), so the caller's later `$f->resume()` throws "Cannot resume a fiber that is not suspended".

A `$f->start()` inside the tick throws `Error`, yet the body runs later anyway.

**Fix (an S3.2 change missing from section 10):** the four Fiber methods refuse on `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` next to the existing `zend_fiber_switch_blocked()` checks (:1314, 1370, 1421, 1464). Alternatively, `zend_fiber_switch_blocked()` returns true while the flag is set. Either way it depends on finding 3.

## 5. MED: D5 / S3.2 item 5 breaks `fiber/030`, and the ts.c follow-up is missing

**`fiber/030` (in the list; S3.md:747-748 says "not checked").**
1. The script prints `done`; call 1 drains.
2. The only parked coroutine is a suspended fiber, so the exemption cancels it with a graceful exit (section 6).
3. `finally { Fiber::suspend(); }` runs while `$fiber` is still alive, so `extended_data != NULL`.
4. With item 5 the `IS_CANCELLED` term is gone, so the fiber parks again instead of throwing `FiberError`.
5. The exemption fires again. Delivery is edge-triggered and `waker.error` is NULL, so the graceful exit is delivered, unwinds the fiber and is filtered out.
6. Output is `done` only. The expected `Fatal error: Uncaught FiberError: Cannot suspend in a force-closed fiber` never appears.

**ts.c.** ts_cancel (test_scheduler.c:1451, the `IS_CANCELLED` term) and ts_suspend (:1558, "Cancelled: never park again") keep CANCELLED final. Item 5's required core test ("catch the cancellation and suspend again") therefore fails on `ext/test_scheduler`, which is the only way to test S3.2 changes.

**Fix:**
- Add a ts.c change to S3.2: drop the terminal reading at :1558 and the CANCELLED term at :1451.
- Decide `fiber/030`: either exclude it with a reason citing D5, or have the exemption's graceful cancel mark the fiber force-closed (for example, have 1375 test a force-close marker instead of CANCELLED).
- The list cannot be frozen with this open.

## 6. MED: `asHiPriority` has no storage and two readings

D35's flag ("sets a flag; the next enqueue puts it at the front", S3.md:580-581) has no bit in the flags table (S3.md:57-72, "21-30 spare"), which claims to hold all coroutine state.

"Next enqueue" can be read two ways:
- **Once:** cleared on use.
- **Sticky:** every later wake goes to the front. In a ping-pong loop the prioritized coroutine then always overtakes.

The method row "puts the coroutine at the front of the run queue" (S3.md:117) can also be read as enqueueing a SUSPENDED coroutine now, which would wake it without its event.

**Fix:** allocate `ASYNC_COROUTINE_F_HI_PRIORITY` (bit 21), state whether it is sticky or one-shot, and say that the method never enqueues.

## 7. MED: S3.2 item 8 misses the scheduler-context GC decline

In scheduler context the await slot returns false (D31), and `zend_gc_collect_cycles` then returns 0 (zend_gc.c:2234). `gc_possible_root_when_full` passes that 0 to `gc_adjust_threshold` (zend_gc.c:715), which raises the threshold by `GC_THRESHOLD_STEP`.

Automatic GC inside the tick is the common case the note itself names (S3.md:708-711). Item 8 (S3.md:826-828) covers only the switch-blocked path.

**Fix:** item 8 covers every declined run, for example a distinct return value that `gc_adjust_threshold` skips.

## 8. MED: `Async\suspend()` is outside the phase-0 entry list

4.1 (S3.md:353-354) lists the waits that run phase 0, and `Async\suspend` is not among them. 4.8 (S3.md:532) describes it only as "self-enqueue, zero-record park". So the switch-blocked check (D14) happens in `suspend()` step 0, after the self-enqueue has already made the coroutine QUEUED.

**Scenario.**
1. `declare(ticks=1); register_tick_function(fn() => ...try Async\suspend() catch...)`. `ZEND_TICKS` blocks switching (zend_vm_def.h:8105).
2. `Error` is caught, and main is left QUEUED.
3. Main then calls `await($x)`. Step 2 sees QUEUED and treats it as a yield.
4. The tick pops main, which is the unspecified case of finding 1. If that case is fixed, `await` returns NULL before `$x` finishes.

Section 11's "THROW_IF_SCHEDULER_CONTEXT becomes async_wait_begin()" (S3.md:859) implies the fix, but 4.1 and 4.8 contradict it by omission.

**Fix:** list `Async\suspend` in 4.1. It calls `async_wait_begin()` (without the self-await test) before the self-enqueue.

## 9. MED-LOW: the D2 comparison measures two different cores; several measurements have no threshold

**Core cost.** The reference runs on fork core `863f6dd`; True Async runs on RFC core `834811f2d88`.
- The RFC core calls `zend_observer_fiber_switch_notify` unguarded on every switch (zend_fibers.c:495). The fork guards it with `ZEND_OBSERVER_FIBER_SWITCH_ENABLED` (F zend_fibers.c:509-510).
- This lands on every B2 and B4 operation, and the note moves it out of S3 (S3.md:847).
- prctl is symmetric between the two cores (same `zend_mmap.h`) and is kernel-side, so it is not the issue.

**Fix:** either add the one-line guard to S3.2, or run a control on both cores (an empty scheduler, or ts on B2/B4) and subtract it.

**Rows in section 12 without a pass/fail rule:**
- O6: "faster" and "must not regress" have no margin.
- Pool policy: no rule.
- U5 placement: "whether U5 moves" has no criterion.
- ZTS TLS: "if it shows" has no threshold.
- Single-inline vector, notify frame chain: no rule.
- Linear unlink: "shows O(N^2)" is not defined.

## 10. LOW items, each concrete

- **PROTECTED rule broken by cancel step 2.** Section 6 step 2 (S3.md:616-619) runs before the PROTECTED step 4, so `protect(fn() => current_coroutine()->cancel())` sets `F_CANCELLED` inside `protect()`. That breaks S3.md:60 and :79. The same step justifies itself with "suspend sets the status before linking", which is false: links happen in phase 4, and the status is set in suspend step 2. Fix: test PROTECTED first, or reword the invariant.
- **Fault injection.** PLAN S3 Done-when (PLAN.md:138) requires "a bailout through every unlink site". Layer 3 (S3.md:795-797) injects only at U4 and U5. Add U3 (OOM at a phase-2 reservation), U6 (fatal error while parked) and a wake-path bailout covering U1/U2.
- **Inline vector vs notify bit.** 3.6 (S3.md:252-256) uses `capacity == 0` to mean "inline single element" but stores the notify bit in bit 31 of `capacity`. When a one-waiter coroutine finishes, the self-removal during the notify reads `capacity > 0` and indexes `single` as an array. State that the inline test masks bit 31.
- **Pins by excluded tests.** `fiber/019` pins `isSuspended` (S3.md:106) but is excluded as `needs-core:` (S3.md:777). `coroutine/038` pins four formulas (S3.md:105-109) but is `component:S4`.
- **D36 in two lists.** It is S3.2 item 12 (S3.md:838) and also under "Core change requests outside S3" (S3.md:849-850). D36 says S3.2.
- **State after `from_main` call 2 (bailout).** Only call 1 re-mints main (S3.md:694-695). Shutdown functions still run after a fatal error. Call 3 and any spawn or await inside them meet the "no current coroutine" refusal (4.2 step 0), which is written for `from_main=false`. Define: call 2 re-mints main (as ts_main_suspend does, test_scheduler.c:1522-1526), 4.2 applies only to `from_main=false`, and every `from_main` call moves the exit exception into EG(exception) before returning.
- **Speculative: switch-handler order.** 4.2 runs the leave switch handlers after the tick (S3.md:397); the reference runs them before (scheduler.c:1708-1711). The core's shutdown-destructor handlers (zend_execute_API.c:261-285, zend_objects_API.c:90-114) spawn the iterator that continues the destructor pass on leave. With leave after the tick, deadlock resolution can run before that iterator exists: a destructor waits on something only a later destructor resolves, and you get a false DeadlockError. Relevant to S3.4; no test in the list covers it.

## Checked and correct
- The layout sizes: 296 / 280 / bin 320, waker 40, record 40, finish handler 56, event 24; bins and the allocation formula match.
- The zend_fibers.c, zend_gc.c and main.c line citations I sampled all match.
- The cursor-rule arithmetic holds when simulated, including self-removal and removal of an element that already ran.
- The test-list counts add up.
- Within the scope I examined, the note honours D1, D3, D4, D8, D9, D11-D14, D16, D17, D19-D24, D26-D33 and D37.

Files used: `/home/user/true-async/dev/plans/S3.md`, `/home/user/true-async/dev/reviews/s3-structures/EDMOND-DECISIONS.md`, `/home/user/true-async/dev/PLAN.md`, `/tmp/claude-0/-home-user/f1d5bd38-7c5c-5b5d-92a4-b889f4a4b4c2/scratchpad/critic/rfc-core/{Zend/zend_async_API.h,Zend/zend_fibers.c,ext/test_scheduler/test_scheduler.c}`, `/tmp/claude-0/-home-user/f1d5bd38-7c5c-5b5d-92a4-b889f4a4b4c2/scratchpad/critic/opt/m/rfctree/{Zend/zend_gc.c,main/main.c,Zend/zend_execute_API.c,Zend/zend_objects_API.c,Zend/zend_vm_def.h}`, `/home/user/php-src-true-async/Zend/zend_fibers.c`, `/home/user/php-async/{scheduler.c,coroutine.c,async.c}`, `/home/user/php-async/tests/{coroutine/028,fiber/019,fiber/022,fiber/030}*.phpt`.
