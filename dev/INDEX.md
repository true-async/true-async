# Index

Where to look in this repository and around it. Pointers only.

## Documents

- `dev/PLAN.md`: stages S1-S10 and the active step; start every session here.
- `dev/handoff.md`: where the last session stopped and the open questions; the plan outranks it.
- `dev/PRINCIPLES.md`: trade-offs settled in advance; read when a plan fork or a decision needs one.
- `dev/DECISIONS.md`: what was decided and why, including the rejected options.
- `CHANGELOG.md`: user-visible changes, Keep a Changelog format.
- `dev/SECURITY.md`: threat model, the security pass of every T2 stage, the journal of security
  decisions, open findings.
- `dev/WORKFLOW.md`: branches, commits, how the core and ior are built.
- `dev/HEALTH.md`: the weekly health check: how it is run, the open findings, its journal.
- `dev/BENCHMARKS.md`: the results journal of the benchmarks, with dates, builds and outcomes.
- `bench/`: the benchmarks B0-B5 of `dev/plans/S3.md` section 12, the allocation counter, the
  test_scheduler control and the variant patches measured against the code; `tools/bench.py` runs
  them.
- `tools/results.py`: per-test statuses from run-tests output, and a diff of two runs.
- `tools/plan-page.py`: renders `dev/PLAN.md` as the progress page
  (https://claude.ai/artifact/2NzNg5GSZo25MbdDY5PgSE); a daily routine republishes it from `main`.
- `dev/plans/S2.md`: S2 notes: build against the core, runner, test lists, layers, CI, Mull.
- `dev/plans/S3.md`: S3 notes: the scheduler on the scheduler API, structures, wait model,
  cancellation, lifecycle, fibers, test ownership by step; "As built" paragraphs record the code.
- `dev/plans/S3.7-spec.md`: the PHP-visible behaviour of S3.7 (`await()`, the GC's wait,
  `getAwaitingInfo()`), the specification the S3.7 tests were written from.
- `dev/plans/S1-lsan-fork.md`: S1's LeakSanitizer report after `fork()` with ior's thread backend.
- `dev/reviews/io-hooks-design-review.md`: review of the IO hooks design (php/php-src#23997);
  the source of the B1-B3 and M1-M13 references in the plan.
- Codes in source comments: Dn is item n of `dev/reviews/s3-structures/EDMOND-DECISIONS.md`, Un an unlink
  site (`dev/plans/S3.md` 4.4), Bn a benchmark (`dev/plans/S3.md` 12).
- `dev/reviews/s3-structures/`: raw S3.1 material of 2026-10-01: Edmond's decisions, the two experts'
  reports (fork-to-RFC consolidation, structure layouts), the Critic rounds, probe sources (`.c.txt`).

## Sources

- `src/true_async.c`: the module: INI, MINIT/RINIT/RSHUTDOWN, the `Async\` functions (`spawn`,
  `await`, `suspend`, `protect`, `current_coroutine`, `get_coroutines`, `graceful_shutdown`).
- `src/scheduler.c`, `.h`: the scheduler behind the core's slots: fiber contexts and their pool,
  the run queue and switches, the scheduler coroutine, main's coroutine, cancellation, deadlock
  resolution, the request lifecycle.
- `src/coroutine.c`, `.h`: the `Async\Coroutine` object, its body's run and its finalize.
- `src/true_async_API.c`, `.h`: the internal API: the callbacks vector and its notify, finish and
  switch handlers, the wait record.
- `src/exceptions.c`, `.h`: the exception classes and `CompositeException`.
- `src/internal/`: the circular buffer and the fuzz hook (`fuzz.c`, built with
  `--enable-true-async-fuzz`).
- `src/test_hooks.c`, `.h`: `TrueAsync\Test\` functions for `tests/internal/`, built with
  `--enable-true-async-test-hooks`.
- `src/known_answer.c`, `.h`: the planted functions of Mull's known-answer check, built with
  `--enable-true-async-known-answer`.
- `src/*.stub.php`: stubs; `*_arginfo.h` is generated from them by the core's `gen_stub.php`.

## Build and tools

- `config.m4`: the build switches (`--enable-true-async`, test hooks, fuzz hook, known-answer
  functions); `config.w32`: the Windows build, with the extension and test hooks switches only;
  `php_true_async.h`: the module globals.
- `.github/workflows/ci.yml`: the CI lanes; `tools/ci/build-core.sh` builds ior and the core
  (`WORKFLOW.md`, "Building the core").
- `tools/test.py`: builds the extension for a lane, starts the MySQL fixture when the run needs
  it (`WORKFLOW.md`, "Test fixtures") and runs the listed tests; `--seeds N` for the fuzz runs. `tools/run-tests.patch` is its patch of php-src's runner.
- `tools/lists.py`: parses `tests/lists/`; `tools/check-lists.py` checks the lists against the
  tests and the reference; `tests/lists/S3.excluded` gives a reason for every reference test left out.
- `tools/check-gates.py`: grep gates over `src/` (fork constructs, banned calls, build flags).
- `tools/mull.py`, `tools/mull/`: mutation testing with Mull and its known-answer test.
- `tools/roadmap.py`: rewrites the README roadmap from `PLAN.md`; `--check` in CI.
- `tools/format.sh`: clang-format 18 over the C sources; `--check` in CI.

## Outside the repository

- true-async/php-src: the core, the pinned branch in `WORKFLOW.md` ("Pinned core").
- true-async/php-async at `tests/lists/REFERENCE`: today's TrueAsync, the reference and the source
  of tests.
- `E:\php\true-async-plan.html`: source of the design page of 2026-10-01
  (https://claude.ai/artifact/9oHVzBL9FtYrsACfRJnMpF); it lags behind `PLAN.md`, the progress page
  above does not.

## Not yet present

`RFC-CHANGES.md`: starts with the first change request to an RFC.
