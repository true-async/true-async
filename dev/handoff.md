# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.6** (not started); S3.5 closed.

## State

- Core pinned: `async-core-io-2026-10-02-2` (`82df2fc6ccc`) = the previous branch + `async-core`
  `565f515df16` (the stack read before a context cleanup that frees the context). CI gates every
  lane on every list; a test that cannot pass yet carries `--XFAIL--` naming its step, and the
  commit that makes it pass removes the section.
- S3.3 and S3.4: internal API, classes, the `Coroutine` object (304 B).
- S3.5 (`src/scheduler.{h,c}`): the 21 slots, the FIFO run queue, pooled contexts with the in-place
  run, main adopted as a copy of `EG(main_fiber_context)`, from_main calls 1-3 drain the queue on
  the OS stack and re-mint main; `spawn`, `current_coroutine`, `get_coroutines`; the state
  methods. Later slots refuse: `suspend()` outside from_main throws, `await` returns false,
  `cancel` throws, `intercept_fiber` returns NULL.
- The extension never calls `zend_fiber_switch_block()`; the notify runs with
  `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` and has no `zend_try`; `is_bailout` is bit 19 only.
- Reviews from now on: after the code, Critic and the Sage (`general-purpose`, model `fable`, the
  maximum effort) compare it with TrueAsync (`/root/php-async` in the container) and hunt
  inventions; one plan step is one commit with its plan, handoff and DECISIONS lines (Edmond,
  2026-10-02).

## Obligations for S3.6

- A bailout while main is parked goes to main's context with the bailout flag (ts.c:1270-1282,
  section 4.2 step 4, U4), not to `scheduler_idle_context()`: its assert and the comment above
  `fiber_entry` stop holding once main can park.
- `scheduler/003-hi_priority_once` loses its `--XFAIL--`.
- `getSuspendFileAndLine`, `getSuspendLocation` and `getTrace` read `fiber_context->execute_data`,
  which `suspend()` stores at entry.

## Later steps

- S3.7: teardown of a target with a linked wait record wakes the waiter with an error (4.4); the
  await slot makes `gc/022` pass; an automatic GC over objects with `__destruct` hangs until the
  destructor phase can suspend and main can await (Sage's probe: 6000 cycles of two objects with
  an empty `__destruct`), own test in the step's done line.
- S3.6: `suspend()` takes `next` from the queue on a stack with a frame, where a failed stack take
  returns with an exception instead of bailing out: say what happens to `next` (finish it unrun,
  as the drain does).
- S3.8: the rethrow of an unobserved exception from the coroutine's destructor (TrueAsync
  `coroutine.c:219-230`, section 6) and the graceful shutdown; with it, `scheduler/006` no longer
  reaches its out-of-memory coroutine (cancelled before it runs): rebuild the fixture so the bailout
  comes during the cancellation (an OOM in a `finally`); `waker_apply_error` keeps the first
  of two plain errors, TrueAsync's resume replaces it and chains the old one: pick one rule.
- S3.10: `bailout_all_coroutines` sets bit 19 on every coroutine it unwinds; RSHUTDOWN still
  asserts only main is left, which a bailout inside call 2 or 3 breaks today (no call 2 follows
  call 3, e.g. a shutdown function spawns and the stack fails; a bailout inside call 2 also keeps
  `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` set through the shutdown functions; a bailout inside finalize,
  in its notify, leaves a FINISHED coroutine in the registry: TrueAsync wraps finalize in a second
  `zend_try`). `gc/013` and `gc/014` lose their `--XFAIL--`.
- Observers: every re-mint of main notifies a switch into a new context copy, and main's end
  notifies nothing for the copy it frees (as ts.c; TrueAsync never re-mints). Check when an
  observer keyed by context appears.
- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`; the core could
  launch the scheduler for `php -r` too (today `spawn` refuses there, DECISIONS 2026-10-02).
- S9 (callbacks run PHP): the exception save and restore keep an exit marker on top.
- Nested notifies recurse with no depth limit (S3.md 3.6 "As built").

## Next

1. S3.6.
