# Workflow

How work is done in this repository and in the core branch it builds on.

## Branches

- Extension repository, initial stage: every commit goes straight to `main`, no task branches
  no PRs and no issues. The stage ends when Edmond says so; the rule that replaces it gets a `DECISIONS.md`
  entry. Held by discipline, no gate.
- Core: the pinned `async-core-io` branch ("Pinned core") has no upstream in a local clone, so a
  bare `git push` fails instead of landing in php/php-src#22561. Gate: no upstream set
  (`git rev-parse @{u}` fails). It is published in true-async/php-src under the same name for CI,
  by explicit refspec only: `git push origin <branch>:<branch>`.
- Merge, never rebase pushed history; force-push only on Edmond's explicit word. An unpushed step
  commit is rebased on `origin/main` before its push, since several threads push to `main`
  (`dev/PLAN.md`, "Parallel tracks"). Held by discipline, no gate.
- Ownership (Edmond, 2026-10-01). The scheduler RFC and its PoC are ours: a bug in it is fixed
  on `async-core` in true-async/php-src (the head of php/php-src#22561) and merged into
  `async-core-io`. bukka's projects (the IO hooks PoC php/php-src#23997, ior) are fixed through
  pull requests to his repositories from our clones, when a fix is needed. Held by discipline,
  no gate.
- Core contents: `async-core-io` holds merges only: `async-core`, php-src master and bukka's PR
  head. A commit of ours never lands there: scheduler changes go to `async-core`, fixes to
  bukka's code go to him as PRs. Gate: `git log --no-merges async-core-io ^origin/async-core
  ^upstream/master ^<hooks head>` is empty.
- Core update (a new head of either PoC, or master): a new branch `async-core-io-<date>` from
  the current core branch (a dash: git refuses `async-core-io/<date>` beside the branch
  `async-core-io`), the new heads merged in, master first when `async-core` carries a newer master
  (merging `async-core` alone gives two merge bases and false conflicts); debug and ASAN built; the S1 suites run and
  compared per test with `tools/results.py --diff` against the current branch; every difference
  gets a reason in the plan. The extension moves to the new branch only after that; the old one
  stays as it was. Held by discipline, no gate.
- Requests to bukka's RFCs (IO hooks, Poll additions, Ring) are listed in `dev/RFC-CHANGES.md`
  with the PR link and its state, one topic per PR; the file starts with the first request. The
  scheduler RFC is ours and changes on `async-core` directly.

## Pinned core

| Part | Revision | In `async-core-io-2026-10-05-4` `9531d5b0b1f` |
|---|---|---|
| php-src master | `d7f966e073b` | merged |
| Scheduler PoC (`async-core`, php/php-src#22561) | `63d4869bff4` | merged |
| IO hooks PoC (php/php-src#23997) | `056d9f803a3` | merged |
| ior | `2fb12e8ce01` | built per tree, "Building the core" |
| `ext/async` (reference tests, true-async/php-async) | `1fdacf8575b` | `tests/lists/REFERENCE` |

CI pins the same core and ior in `.github/workflows/ci.yml` (`CORE_REF`, `IOR_REF`): a core update
changes both places.

A listed test that cannot pass yet carries run-tests' `--XFAIL--` section naming the plan step that
makes it pass; the runner expects XFAIL for it, and a pass (run-tests' WARN) fails the lane until
the section is removed in that step's commit. `check-lists.py` hashes a test without the section.

Newer heads not yet taken: IO hooks `608927ebe09` (2026-10-02). The pinned branch was compared
without ior (`dev/PLAN.md`, S3.2); Edmond accepted it on 2026-10-02.

## Security

- Every T2 stage closes with a security pass after Code Reviewer: a subagent checks the stage diff
  against the checklist of `dev/SECURITY.md`; each finding is fixed with a test or recorded there
  with the reason. The stage's plan carries the pass as its last step. Held by discipline, no gate.
- A decision that changes what the extension or CI exposes gets a line in the journal of
  `dev/SECURITY.md`, beside its `DECISIONS.md` entry when it has one.

## Tests

- An existing test is changed only with a reason: the test is wrong, it contradicts the RFC or a
  recorded decision. Critic judges the reason before the change; when Critic doubts, the question
  goes to Edmond (2026-10-02). A ported test carries the change as `changed:` with the
  `DECISIONS.md` line that records the reason. Held by discipline; `check-lists.py` catches an
  untagged edit of a ported test.

## Code

- A name states the role of the value: no one-letter or clipped variable names (`c`, `x`, `ce`, `rec`,
  `cb`) but `coroutine`, `awaitable`, `cancellation`, `records`, `callback`. A loop index `i` is the one
  exception. A name also says which object it holds, not its place in a type hierarchy: not `base`,
  `entry` or `obj`; the name itself is the author's choice. Edmond, 2026-10-02. Held by discipline, no
  gate.
- Branch hints as in the core and TrueAsync: an `if` whose condition is an error or a rare case is wrapped
  whole in `UNEXPECTED()` (`if (UNEXPECTED(EG(exception) != NULL))`, `FAILURE`, a bailout, NULL from a
  lookup or from an allocator that can return it; `emalloc` never does), the clearly frequent path in
  `EXPECTED()`. A condition whose sides are about equally likely, or that depends on the configuration,
  stays bare. Edmond, 2026-10-02. Held by discipline, no gate.
- A blank line follows a block's closing `}` before the next statement, block or declaration.
  Edmond, 2026-10-02. Held by discipline, no gate.
- A TLS global (`EG()`, `ASYNC_G()`, a `ZEND_ASYNC_*` macro) read two or three times in one function
  may be cached in a local variable; a preference, not a rule. Edmond, 2026-10-02.

## Commits

- English, one topic per commit; a core change for an RFC is one commit and one
  `RFC-CHANGES.md` entry. Held by discipline, no gate.
- Subject: one line up to 128 characters, imperative mood, names the change in terms of the
  result. No ticket number.
- Body only when the reason is not clear from the subject, up to ten lines; it says why, not
  what the diff shows. No attribution trailers.
- Only finished changes reach `main`: draft commits are squashed into the commit they complete
  before the push. Held by discipline, no gate.
- One plan step is one commit and one push. The `dev/PLAN.md`, `dev/handoff.md`, `dev/DECISIONS.md`
  and README roadmap edits of the step go into that commit, not into commits of their own. The
  Critic and the Sage read the local commit before the push; their fixes are amended into it.
  A handoff for a new thread goes into the next step's commit, or alone only when the thread
  stops mid-step. Edmond, 2026-10-02: main had three times more commits than changes. Held by
  discipline, no gate.
- Finished commits are pushed to `main` without asking Edmond for an OK on the diff (Edmond,
  2026-10-02). Held by discipline, no gate.
- `CHANGELOG.md` at the root, Keep a Changelog 1.1.0: every user-visible change gets a line under
  `[Unreleased]` in the commit that makes it; tests, tools and CI do not. Held by discipline, no gate.
- `dev/DECISIONS.md`: as short as possible, a line for the decision and a line for the reason.
  Held by discipline, no gate.

## Building the core

`tools/ci/build-core.sh <dbg|asan> <core sha> <ior sha>` builds ior and the core for one tree and
installs the core into `~/ta-prefix/pocs-<tree>`, the default prefix of `tools/test.py`; CI runs the
same script. ior (https://github.com/libior/ior) is built with cmake as the `build-ior` action of
php/php-src#23997 does (`-DIOR_WITH_THREADS=ON`, tests and bench off, Release), into
`~/ior-<tree>`; the ASAN tree adds `-DIOR_ENABLE_ASAN=ON`.

Both trees carry the io_uring and the thread backends; io_uring needs `liburing-dev`.
`IOR_BACKEND=threads` selects the thread backend at run time.

Configure line of every core tree (the ASAN tree adds `--enable-address-sanitizer
--enable-undefined-sanitizer`):

```
./configure --enable-zts --enable-debug --with-ior=$HOME/ior-<tree> --enable-test-scheduler \
  --with-curl --with-openssl --enable-sockets --enable-pcntl --with-mysqli --with-pdo-mysql \
  --prefix=$HOME/ta-prefix/pocs-<tree>
```

`make install` puts the core into its prefix; the extension builds against that prefix's
`phpize` and `php-config` (`dev/plans/S2.md`, section 1).

A revision without one of the switches (the scheduler PoC has no `--with-ior`, the hooks PoC no
`--enable-test-scheduler`) takes the same line; autoconf prints
`WARNING: unrecognized options` and goes on.
