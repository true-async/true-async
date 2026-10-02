# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.5** (not started); S3.4 closed.

## State

- Core pinned: `async-core-io-2026-10-02` (`8a29d63edcf`). CI gates every lane on every list; a
  test that cannot pass yet carries the standard `--XFAIL--` section naming its step. The commit
  that makes it pass removes the section.
- S3.3: internal API (`src/true_async_API.{h,c}`), `src/internal/` buffer and allocator, test hooks,
  `tests/internal/001`-`018`.
- S3.4: exceptions, `Awaitable` (refuses classes of other modules), `Completable`, the `Coroutine`
  object (`src/coroutine.{h,c}`, methods throw "not implemented yet" until their steps), INI
  `true_async.debug_deadlock`; `tests/classes/001`-`007` (`006` is S3.5's, XFAIL). Classes register
  only when `true_async.enable` is on (read as a boolean word). `new Async\Coroutine` throws
  (DECISIONS 2026-10-02).
- The extension never calls `zend_fiber_switch_block()`: a notify runs its callbacks with
  `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` set and puts back the value it found (Edmond, 2026-10-02).
- The notify cursor is a field of the callbacks vector (Edmond, 2026-10-02); no global frame
  array. `async_coroutine_t` is 304 B.
- The notify has no `zend_try`, returns `void`, only saves and restores the pending exception;
  finish handlers fire once and their `bool` is ignored; no `ASYNC_G(bailing_out)` (review of
  2026-10-02, DECISIONS). Edmond has yet to confirm "every callback runs after one throws".

## Obligations for S3.5

- Finalize holds the coroutine's object across its notify; teardown with a linked wait record wakes
  the waiter with an error (section 4.4).
- `Async\` functions are registered in MINIT after the enable check (`zend_register_functions`, as
  the test hooks are), so a disabled extension registers none (Critic on S3.4).
- `coroutine_object_free` asserts `fiber_context`, `scope`, `awaiting_info`, `switch_handlers` are
  NULL: the step that fills each releases it first.
- Remove the XFAIL section of `tests/classes/006` when `spawn` works.
- The coroutine fiber entry's catch puts `ZEND_ASYNC_IN_SCHEDULER_CONTEXT` back to false after a
  bailout, as TrueAsync does (`scheduler.c:2026-2031`).
- The RFC finish-handler slots call `async_finish_handler_add/remove` with the coroutine
  (`async_coroutine_t *`), which owns the vector.

## Later steps

- Next core update: `ZEND_ASYNC_DEACTIVATE` also clears `in_scheduler_context`, so a bailout in
  scheduler context does not carry into the next request.
- S3.10: `bailout_all_coroutines` sets `ASYNC_COROUTINE_F_BAILOUT` on every coroutine it unwinds,
  before the switch too (bug 9); a test of a bailout out of a top-level notify.
- S9 (callbacks run PHP): the exception save and restore keep an exit marker on top, as the engine
  does (S3.md 3.6 "As built").

- Nested notifies recurse with no depth limit: completing a chain of awaitables must not nest one
  notify per link (S3.md 3.6 "As built").

## Next

1. S3.5.
