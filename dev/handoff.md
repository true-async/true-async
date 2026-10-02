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

## Open with Edmond

- He does not like the notify frame stack (`ASYNC_G(notify_stack)[32]`). Proposed: a `cursor` field
  in `async_callbacks_vector_t` (vector 24 B, coroutine 304 B, same 320 B bin), no stack, no
  lookup, no limit; the removal rule of S3.md 3.6 stays (it fixes the fork's bug 7). Waiting for
  his answer; do not change it before he agrees.

## Obligations for S3.5

- Finalize holds the coroutine's object across its notify; teardown with a linked wait record wakes
  the waiter with an error (section 4.4); the scheduler's bailout handling calls
  `async_callbacks_bailout_reset()`.
- `Async\` functions are registered in MINIT after the enable check (`zend_register_functions`, as
  the test hooks are), so a disabled extension registers none (Critic on S3.4).
- `coroutine_object_free` asserts `fiber_context`, `scope`, `awaiting_info`, `switch_handlers` are
  NULL: the step that fills each releases it first.
- Remove the XFAIL section of `tests/classes/006` when `spawn` works.

## Next

1. Edmond's answer on the notify cursor.
2. S3.5.
