# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.7** (not started); S3.6 closed (47633ef), review fixes in
94c5ef5. S3.15-S3.18 (health check) run after S3.14 (Edmond).

## First: Edmond's review of S3.6 (2026-10-02)

Do these before S3.7 code; the first one may change S3.7 and later steps.

1. **The scheduler follows TrueAsync's hybrid algorithm, or a recorded reason why it cannot.**
   Edmond: TrueAsync's scheduler code runs between coroutines (the tick and
   `execute_next_coroutine` inside suspend, direct switches) and in a separate scheduler
   coroutine `ZEND_ASYNC_SCHEDULER` with its own fiber, both at once. Ours (S3.md section 5) has
   only the first half; the drain on the OS stack (`drain_context()`, `scheduler_drain`) stands in
   for the second only after the script ends. Map TrueAsync's algorithm from
   `/root/php-async/scheduler.c` (scheduler launch and `main_transfer` around `:1300-1420`,
   `switch_to_scheduler` `:383`, `execute_next_coroutine` `:497-546`, `fiber_entry` `:1764-2060`,
   `bailout_all_coroutines` `:949-995`), check what the RFC core allows (the from_main calls,
   `EG(main_fiber_context)`, the GC, ts.c), then tell Edmond with code which it is. It decides the
   bailout path too: today `fiber_entry`'s `zend_catch` sends BAILOUT straight to main's stack
   or the drain (ts.c's shape, not TrueAsync's: TrueAsync goes through the scheduler fiber, which
   runs `bailout_all_coroutines`, then to main). Never describe TrueAsync as a separate scheduler
   fiber alone.
2. **TLS reads into locals.** A TLS global (`EG()`, `ASYNC_G()`, a `ZEND_ASYNC_*` slot) read two
   or three times in one function goes into a local first. Add the line to `dev/WORKFLOW.md`
   "Code" and fix the S3.6 spots (`EG(exception)` in `scheduler_suspend`, `scheduler_tick`) in the
   S3.7 commit. Minor (Edmond).
3. **`async_wait_kind_t` (S3.3) has no TrueAsync counterpart**; TrueAsync keeps `del_callback` and
   `info` on each event. Edmond questioned it and was shown that plain records never read `kind`
   (`F_TYPED`), but did not confirm it. If S3.7 keeps it, say so with the code before writing
   `async_wait_unlink`; switching to TrueAsync's event methods is cheapest before S3.7.
4. **No `zend_fiber_switch_*` at all** in `src/` (done in 94c5ef5, gate extended; D14 withdrawn,
   it was Claude's error, not Edmond's decision). The core's own switch-block windows (pcntl
   dispatch, ticks, the IO-hooks lock) stay open, as in TrueAsync; the Critic suggested a phpt
   that pins the pcntl or ticks case. Not done.
5. **Names say what the thing is** (Edmond asked about `fiber_loop`, `next`,
   `scheduler_idle_context`; renamed to `run_coroutines`, `next_coroutine`, `drain_context`).
   Check new names before review, not after.

## State

- Core pinned: `async-core-io-2026-10-02-2` (`82df2fc6ccc`). CI gates every lane on every list; a
  test that cannot pass yet carries `--XFAIL--` naming its step, and the commit that makes it pass
  removes the section. run-tests (`tools/run-tests.patch`) fails a test the timeout killed.
- S3.3-S3.5: internal API, classes, the `Coroutine` object (304 B), the 21 slots, the FIFO run
  queue, pooled contexts with the in-place run, main adopted as a copy of `EG(main_fiber_context)`,
  from_main calls 1-3.
- S3.6 (`src/scheduler.c`): the suspend slot by 4.2 (refusals, exception saved, frame stored,
  SUSPENDED unless a yield, the tick, direct switch to the next queued coroutine, a yield with
  nobody ahead runs on, the waker's error thrown on return); `Async\suspend()` refuses before its
  self-enqueue; the defer slot and the tick (microtasks in scheduler context, the first throw ends
  the tick and becomes the exit exception) in suspend, after each coroutine in a context's loop,
  and in the drain; a bailout in a coroutine while main is parked goes to main's stack (U4), and the
  from_main call drops the queue before main finishes; `getSuspendFileAndLine`,
  `getSuspendLocation`, `getTrace` read the parked frame through the core's execute-data slot. Both
  `Async\suspend()` and the slot refuse inside a Fiber the scheduler did not adopt (until S3.9).
- The extension never calls any `zend_fiber_switch_*` function; the notify and the tick run with
  `ZEND_ASYNC_IN_SCHEDULER_CONTEXT`; no `zend_try` in the notify or the tick.
- Reviews: after the code, Critic and the Sage (`general-purpose`, model `fable`) compare it with
  TrueAsync (`/root/php-async` in the container) and hunt inventions; one plan step is one commit.

## Obligations for S3.7

- U5 (4.2, 4.4): the tick runs microtasks between the first link and the switch. 4.4 wants a
  `zend_try` there that aborts the wait; TrueAsync has none and Edmond allows no `zend_try` on a hot
  path without his word (DECISIONS 2026-10-02). Bring it to Edmond with the records.
- `async_wait_unlink` is called where 4.2 places it (the refusals, after the switch, the bailout
  return) and is a stub that asserts `wait == NULL`; the bailout return (U4) becomes
  `async_wait_abort` (4.4).
- The Sage's GC probe (12 000 cyclic objects with `__destruct`) ends since S3.6, in main and in a
  coroutine; the own test of the S3.7 done line still has to be written.

## Later steps

- S3.8: the drain's tick runs with no frame: a microtask that throws there is fatal ("Exception
  thrown without a stack frame"), not an exit exception (TrueAsync's scheduler has a root frame);
  only the GC defers in S3, and its handler does not throw. The tick's exit exception does not
  start the graceful shutdown yet: `edge_cases/014` hangs
  (its first coroutine yields forever) and fails by timeout; `edge_cases/015` too. 4.2 step 4 skips
  FINISHED entries (a cancel-before-run finalized in place) once S3.8 can finalize a queued
  coroutine; until then no FINISHED entry reaches the queue. The rethrow of an unobserved exception
  from the coroutine's destructor (TrueAsync `coroutine.c:219-230`) and `scheduler/006`'s fixture
  (an OOM in a `finally`); `waker_apply_error` keeps the first of two plain errors, TrueAsync's
  resume replaces it and chains the old one: pick one rule.
- S3.9: `scheduler/013` pins the refusal of `suspend()` inside an unadopted Fiber; adopting every
  Fiber changes it (DECISIONS 2026-10-02).
- S3.10: a coroutine that yields and bails out in its own tick or stack take (out of memory) is
  finalized while its entry is still queued, and the drop reads it freed (Critic): decide with
  S3.8's FINISHED entries whether a queued coroutine keeps its birth reference until popped.
  A bailout with a started coroutine in the queue (a yield) fails the drop's assert:
  `bailout/012` (XFAIL again). A stack take in `suspend()` that bails out (out of memory) loses the
  popped coroutine, as the drain's take would not. `bailout_all_coroutines` sets bit 19 on every
  coroutine it unwinds; RSHUTDOWN asserts only main is left. `gc/013` and `gc/014` pass already.
- Observers: every re-mint of main notifies a switch into a new context copy, and main's end
  notifies nothing for the copy it frees (as ts.c; TrueAsync never re-mints).
- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`; the core could
  launch the scheduler for `php -r` too (today `spawn` refuses there, DECISIONS 2026-10-02).
- S9 (callbacks run PHP): the exception save and restore keep an exit marker on top.
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

## Next

1. Item 1 of "First" above: answer Edmond on the hybrid scheduler.
2. S3.7.
