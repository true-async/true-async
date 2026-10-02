# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.3**, all but the circular buffer and allocator done
(`7f1cba7`).

## State

- Core pinned: `async-core-io-2026-10-02` (`8a29d63edcf`). CI gates every lane on every list; a
  test that cannot pass yet carries the standard `--XFAIL--` section naming its step (Edmond,
  2026-10-02; `CLOSED_STAGE` was reverted). The commit that makes such a test pass removes the
  section: a passing XFAIL test is WARN, which `tools/test.py` counts as a failure.
- S3 is split into S3.3-S3.14 (agreed by Edmond); `dev/plans/S3.md` section 14 names the tests
  each step owns.
- S3.3 in `src/true_async_API.{h,c}`: callbacks vector, finish handlers with ids, waker, exception
  save and restore. `--enable-true-async-test-hooks` (every lane) builds
  `TrueAsync\Test\callbacks_scenario()`; `tests/internal/001`-`011` pass on dbg, valgrind clean.
  Critic: 8 findings fixed; where the code differs from the spec text, S3.md 3.6 "As built in
  S3.3" says so (notify frames in globals, fiber switching blocked during a notify, every callback
  runs after a throw, the caller holds the reference, `F_RUNNING`/`F_REMOVED`).
- `tools/check-gates.py` runs the grep gates of S3.md section 11 in the `lists` job; a core update
  that adds a `zend_async_*_t` type adds it to `CORE_TYPES` there.
- Obligations for S3.5: its finalize holds the coroutine's object across the notify; teardown with
  a linked wait record wakes the waiter with an error (section 4.4); the scheduler's bailout
  handling calls `async_callbacks_bailout_reset()`.

## Next

1. Edmond's answer on the licence of code adapted from php-async (PHP License 3.01): BSD header
   plus "Adapted from true-async/php-async", or keep the PHP License header. Then port
   `internal/circular_buffer.{c,h}` and `allocator.{c,h}` into `src/internal/` (symbols renamed
   `true_async_*`), close S3.3, rerun `tools/roadmap.py`.
2. S3.4.
