# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.4** (not started); S3.3 closed.

## State

- Core pinned: `async-core-io-2026-10-02` (`8a29d63edcf`). CI gates every lane on every list; a
  test that cannot pass yet carries the standard `--XFAIL--` section naming its step (Edmond,
  2026-10-02; `CLOSED_STAGE` was reverted). The commit that makes such a test pass removes the
  section: a passing XFAIL test is WARN, which `tools/test.py` counts as a failure.
- S3 is split into S3.3-S3.14 (agreed by Edmond); `dev/plans/S3.md` section 14 names the tests
  each step owns.
- S3.3: `src/true_async_API.{h,c}` (callbacks vector, finish handlers with ids, waker, exception
  save and restore) and `src/internal/` (circular buffer and allocator from php-async, under the
  project's BSD header: the code is Edmond's). `--enable-true-async-test-hooks` (every lane)
  builds `TrueAsync\Test\callbacks_scenario()` and `buffer_scenario()`; `tests/internal/001`-`017`
  pass on dbg. Where the code differs from the spec text, S3.md 3.6 "As built in S3.3" says so.
  The module exports only `get_module` (`-fvisibility=hidden`).
- `tools/check-gates.py` runs the grep gates of S3.md section 11 in the `lists` job; a core update
  that adds a `zend_async_*_t` type adds it to `CORE_TYPES` there.
- Obligations for S3.5: its finalize holds the coroutine's object across the notify; teardown with
  a linked wait record wakes the waiter with an error (section 4.4); the scheduler's bailout
  handling calls `async_callbacks_bailout_reset()`.

## Next

1. S3.4: exceptions, `Awaitable`, `Completable`, the `Coroutine` object, INI; the seven `changed:`
   ports of S3.md section 9.
