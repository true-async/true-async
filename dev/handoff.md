# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-01. Active step: **S3.1** (in progress), design note of the scheduler.

## State

- S1 and S2 closed (S1.5, the Windows build of the core, deferred). `main` is green in CI: lists,
  formatting, roadmap, `pocs-dbg`, `pocs-asan`, Windows, mutants-coverage.
- The core branch `async-core-io` `834811f2d88` is published in true-async/php-src (explicit
  refspec, no upstream). Local prefixes: `~/ta-prefix/pocs-dbg`, `~/ta-prefix/pocs-asan`.
- `dev/plans/S3.md` is the third draft of the S3.1 note, after Critic rounds 1 and 2 and a
  performance review (all 2026-10-01). It is **not agreed** with Edmond.
- The data structures the note adopts are shown in
  https://claude.ai/artifact/XZ6dhkRQAar83HfPqMoqLp; Edmond has **not approved** them.

## Decided in this session (also in `DECISIONS.md`)

- S3 adapts `ext/async` `1fdacf8` code (circular buffer, scheduler, coroutine) critically, with
  performance as the criterion; the fork's async API parts it needs move into
  `src/true_async_API.{h,c}`.
- Waiting keeps the reference model unchanged: events, callbacks, the waker, `resume_when`, the
  coroutine as an event; moved from the fork's core into the extension. A home-made wait-source
  design was rejected.
- INI prefix `true_async.*`; Windows CI job in S2.4; TSAN tree waits for S9.

## Open, to go through with Edmond one at a time, in the order he picks

1. `AsyncCancellation`'s parent: `\Error`, `\Exception`, or `\Cancellation` added to `async-core`
   (cost listed in the note, section 12.1).
2. Fibers in S3 through `intercept_fiber` (up to 23 tests) or later.
3. The coroutine state fields beside the RFC status (`started`, `yielded`, `cancel_requested`,
   `cancel_pending`, `protected`), `F_CANCELLED` at delivery.
4. Blocked switch: user waits throw, the `await` slot stays strict, the GC checks its own
   precondition, `from_main` ignores the block.
5. `exit()` in a coroutine: a request exit, other coroutines unwound, not cancelled catchably.
6. `asHiPriority()` a no-op; FIFO for every coroutine, GC-minted ones included.
7. `finally` handlers in one coroutine without scope or iterator.
8. The core fixes on `async-core` before code (plan S3.2; note section 9).
9. Per-coroutine output buffers: all `output_buffer` tests `needs-core:` or a core change request.
10. Fiber pool cap.
11. Performance criterion for S3's Done when: at most 3 % more `instructions:u` per operation than
    the reference on B1-B5, allocations per operation not above it.
12. Test hooks build switch (`--enable-true-async-test-hooks`) for the EH_THROW test.
13. The S3 list: about 113 tests without fibers.
14. From the structures artifact: the event's callback vector removes by swapping the last entry in
    (positions move) against the note's stable-handle rule; `deferred_cancellation` against the new
    cancel fields; `triggered_events` unused in S3; `vm_stack` never assigned; the size of
    `async_coroutine_t` (576 B before the new fields, 544 in the reference).

No code is written for S3. Edmond: "не спеши писать код".

## Next

After the answers: rewrite `dev/plans/S3.md` into the agreed version; write `tests/lists/S3.txt`
and `S3.excluded` by reading each candidate; then S3.2 (fixes on `async-core`, each with a test,
then a core update by `WORKFLOW.md`).

Before benchmarks: rebuild the reference release build `~/php-src/bld-rel-zts` at `1fdacf8` (`make`,
then `make sapi/cli/php`; its `ext/async` objects predate `1fdacf8`), and build a release core in
`~/ta-prefix/pocs-rel` from a separate tree. The benchmark scripts and the malloc counter of the
performance review are in the session scratchpad (`b/`), not in the repository.

## How to run

- `TRUE_ASYNC_CORE_SRC=~/php-src2 tools/test.py --lane pocs-dbg | pocs-asan | pocs-dbg-cov`
- `tools/mull.py --known-answer`; `tools/mull.py --diff-ref <Base:> --stage N`
- `tools/check-lists.py --reference ~/php-src/ext/async`; `tools/format.sh --check`;
  `tools/roadmap.py --check`
