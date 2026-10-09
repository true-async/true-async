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
- A bug found in bukka's code (Edmond, 2026-10-06), in this order: (1) Edmond hears of it first,
  with the problem explained (the code, what breaks, for whom); (2) a pull request to bukka's
  code; (3) our core stays free of the bug meanwhile: the fix is one commit on `io-hooks-fixes` in
  true-async/php-src, a branch from the PoC head, merged into the current `async-core-io-<date>`
  until bukka's head carries it. Held by discipline, no gate.
- A bug in php-src itself, outside both RFCs (Edmond, 2026-10-06): the fix is one commit with
  its test on `php-src-fixes` in true-async/php-src, never on an RFC branch. The branch starts
  from the merge base of php/php-src `PHP-8.4` (the oldest branch taking bug fixes) and the
  pinned master, so merging it into the core brings only the fixes; the commit leaves `NEWS`
  alone (its 8.4 section would conflict in every core merge), and the PR text suggests the
  line. Each fix also gets a branch of its own at that commit, and Edmond opens the pull request
  to php/php-src from it: a PR opened from `php-src-fixes` would take every later fix pushed
  there. A core update merges `php-src-fixes` into `async-core-io-<date>` until php-src master
  carries the fix. Held by discipline, no gate.
- Core contents: `async-core-io-<date>` holds merges only: `async-core`, php-src master, bukka's PR
  head, `io-hooks-fixes` and `php-src-fixes`. A commit of ours never lands there directly:
  scheduler changes go to `async-core`, fixes to bukka's code to `io-hooks-fixes` and to him as
  PRs, fixes to php-src itself to `php-src-fixes`. Gate: `git log --no-merges
  async-core-io-<date> ^origin/async-core ^upstream/master ^<hooks head> ^origin/io-hooks-fixes
  ^origin/php-src-fixes` is empty.
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

### Branches of true-async/php-src used by this project

| Branch | Based on | What it is for |
|---|---|---|
| `master` | php/php-src master | a mirror, synced from php/php-src (pre-approved) |
| `async-core` | php/php-src master | the scheduler RFC PoC, head of php/php-src#22561; our scheduler changes |
| `io-hooks-fixes` | bukka's `io_hooks_poc` head | our fixes to bukka's IO hooks PoC, sent to him as PRs |
| `php-src-fixes` | `PHP-8.4` and master's merge base | our fixes to php-src bugs outside both RFCs, merged into the core |
| `io-hooks-connect-started`, `signal-unblock-at-removal` | bukka's `io_hooks_poc` head | one fix each to the IO hooks PoC, the branch of its PR to bukka |
| `stream-chunk-size-read-filter`, `stream-filter-remove-in-callback`, `stream-copy-close-in-callback`, `stream-pclose-in-filter`, `zlib-inflate-error-handler`, `pcntl-dispatch-keeps-handler-mask`, `http-last-response-headers-leak`, `fiber-gc-include-symbol-table` | `php-src-fixes`' base | one fix each from `php-src-fixes`, the branch of its PR to php/php-src |
| `async-core-io-<date>` | the previous one | the core this repository builds on: merges of the four above and bukka's head; the pinned one is in "Pinned core" |
| `async-core-io` | php/php-src master | the first core branch, kept as it was |
| `fiber-callable-object-8.4`, `fiber-callable-object` | `PHP-8.4`, master | the Fiber `$this` use-after-free fix, php/php-src#24134 |
| `true-async` | php/php-src | the TrueAsync fork's core, a reference for porting |

The other branches there belong to the TrueAsync fork's earlier work and are not used here.

## Pinned core

| Part | Revision | In `async-core-io-2026-10-08-4` `77dbfc061f3` |
|---|---|---|
| php-src master | `e17e2970bc0` | merged through bukka's head |
| Scheduler PoC (`async-core`, php/php-src#22561) | `b7c70909437` | merged |
| IO hooks PoC (php/php-src#23997) | `566a6833eb5` | merged |
| Our fixes to the IO hooks PoC (`io-hooks-fixes`) | `2a74924668c` | merged; `dev/RFC-CHANGES.md` 2, 18, 19 |
| The connect fix (`io-hooks-connect-started`, bukka/php-src#4, on bukka's `2bf149c2f50`) | `dca62e39da4` | picked onto `8159f7baa5c` (`async-core-io-2026-10-07-4`) |
| Our fixes to php-src (`php-src-fixes`) | `68f790cb2d4` | merged |
| ior (`release-handle` in true-async/ior, on libior/ior `2bfd2319896`) | `d46649f6425` | built per tree, "Building the core" |
| `ext/async` (reference tests, true-async/php-async) | `1fdacf8575b` | `tests/lists/REFERENCE` |

CI pins the same core and ior in `.github/workflows/ci.yml` (`CORE_REF`, `IOR_REF`): a core update
changes both places.

A listed test that cannot pass yet carries run-tests' `--XFAIL--` section naming the plan step that
makes it pass; the runner expects XFAIL for it, and a pass (run-tests' WARN) fails the lane until
the section is removed in that step's commit. `check-lists.py` hashes a test without the section.


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
- After the push, the thread watches the `push` run of its commit in CI (`actions/runs?head_sha=<sha>`;
  in the cloud container through the GitHub MCP Actions tools, since `gh` there has no login) until
  every job ends, and reports the step done or starts the next one only after that. A job red in
  this run and green in the parent commit's run is the commit's: its fix is a commit of its own,
  pushed at once. Any other red job (another thread's commit, a runner that never started) is
  reported to that thread or to Edmond with the job's link. Edmond, 2026-10-09, after the lists job
  stayed red unnoticed from S9.24 (8432655) to 8b9ba78; it replaces the habit "threads do not wait
  for CI" (`dev/HEALTH.md`, 2026-10-05). Held by discipline, no gate.
- `CHANGELOG.md` at the root, Keep a Changelog 1.1.0: every user-visible change gets a line under
  `[Unreleased]` in the commit that makes it; tests, tools and CI do not. Held by discipline, no gate.
- `dev/DECISIONS.md`: as short as possible, a line for the decision and a line for the reason.
  Held by discipline, no gate.

## Test fixtures

- MySQL: the tests of the groups `mysqli` and `pdo_mysql` reach the server named by
  `MYSQL_TEST_HOST`, `MYSQL_TEST_PORT`, `MYSQL_TEST_USER`, `MYSQL_TEST_PASSWD` and `MYSQL_TEST_DB`,
  TrueAsync's names. The CI lanes run TrueAsync's `mysql:8.3` service and set them. Locally, with
  `MYSQL_TEST_HOST` unset, `tools/test.py` starts a private `mysqld` of the installed MySQL 8
  (`apt-get install mysql-server-core-8.0`) on a free port of 127.0.0.1 with CI's user, password
  and database (root, root, `test`), and removes it with its data after the run; a run without
  those groups starts none.
- HTTP: a test starts its own `php -S` through TrueAsync's `tests/common/http_server.php` (or
  php-src's `php_cli_server.inc`). `tools/test.py` gives every test `PHP_CLI_SERVER_WORKERS=4`, so
  four forked workers accept beside the server's own process and concurrent requests are served
  concurrently. Windows has no fork: the Windows
  lane skips the worker test, and the MySQL test, having no server.
- php-src helpers: seven tests include a helper of the core's own tests (`php_cli_server.inc`,
  `ext/curl/tests/server.inc`) from the checkout named by `$TRUE_ASYNC_CORE_SRC`, which
  `tools/test.py` requires when one of them runs. The helpers write their router and document
  files into that checkout and remove them at the end, so a crashed run leaves files there; point
  it at a build tree or CI's throwaway checkout, not a working clone.

## Building the core

`tools/ci/build-core.sh <dbg|asan> <core sha> <ior sha>` builds ior and the core for one tree and
installs the core into `~/ta-prefix/pocs-<tree>`, the default prefix of `tools/test.py`; CI runs the
same script. ior (https://github.com/libior/ior) is built with cmake as the `build-ior` action of
php/php-src#23997 does (`-DIOR_WITH_THREADS=ON`, tests and bench off, Release), into
`~/ior-<tree>`; the ASAN tree adds `-DIOR_ENABLE_ASAN=ON`.

Both trees carry the io_uring and the thread backends; io_uring needs `liburing-dev`.
`IOR_BACKEND=threads` selects the thread backend at run time. Without liburing 2.2+ ior's cmake
leaves the io_uring backend out without an error, and ior also falls back to the thread backend
when io_uring is refused (seccomp, `kernel.io_uring_disabled`, a kernel before 5.19). A cloud
container lacks `liburing-dev` until the packages of the "Packages" step of
`.github/workflows/ci.yml` are installed. `tools/test.py` refuses a core whose ior runs a backend
other than io_uring, or other than the thread backend under `IOR_BACKEND=threads`: CI runs io_uring.

Fork tests under ASAN can fail when ior threads are alive at `fork()`. On the thread backend they
always are; on core -6, 10 of 17 fork tests fail, `reactor/011` passes on a retry, while the
debug lane passes all 17. On io_uring ior starts its worker pool, with a timer thread, at the first
work op (`getaddrinfo`, `getnameinfo`, `fsync`), so a parent that ran one before the fork can meet
the same failures (read from ior's code, not seen in a test). The causes are outside the extension:
LeakSanitizer in the child warns about every thread of the parent (`dev/plans/S1-lsan-fork.md`);
`ior_queue_forget()` leaves the child the parent's thread pool by design, which LeakSanitizer
reports as leaks from `ior_threads_pool_*`; the child's new ior threads can block for good on a
lock of ASAN's allocator that the child inherited locked (`reactor/035` then prints
`child: not woken` and times out).

Configure line of every core tree (the ASAN tree adds `--enable-address-sanitizer
--enable-undefined-sanitizer`):

```
./configure --enable-zts --enable-debug --with-ior=$HOME/ior-<tree> --enable-test-scheduler \
  --with-curl --with-openssl --enable-sockets --enable-pcntl --with-mysqli --with-pdo-mysql --with-zlib \
  --prefix=$HOME/ta-prefix/pocs-<tree>
```

`make install` puts the core into its prefix; the extension builds against that prefix's
`phpize` and `php-config` (`dev/plans/S2.md`, section 1).

A revision without one of the switches (the scheduler PoC has no `--with-ior`, the hooks PoC no
`--enable-test-scheduler`) takes the same line; autoconf prints
`WARNING: unrecognized options` and goes on.
