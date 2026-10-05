# Principles

Ratified: 2026-10-01 (Edmond). Each principle is a trade-off settled in advance: the winner
first. The source is the list of eleven principles Edmond set on 2026-10-01 (numbered N1-N11
below); the table at the end shows where each of them went.

## P1. Architecture

- **P1.1 Stock php-src plus the RFCs over private core patches.** Why: N1; the fork's core is the
  reason a slice of `ext/async` could not be ported (design round 1). Flips: never; a needed core
  change becomes an RFC change request (`RFC-CHANGES.md`), an upstream fix, or waits. Gate: CI
  builds the extension against the pinned PoCs-only core (S2.4); a private patch landing on
  `async-core-io` is caught by the merge-only check of `WORKFLOW.md`, held by discipline.
- **P1.2 bukka's IO APIs over libuv.** Why: N4; embedding libuv spun the loop (design round 2).
  Flips: never. Gate: held by discipline, no gate.
- **P1.3 Code written anew over code ported from the fork.** Why: N3. Flips: tests, which are
  ported from `ext/async` and serve as the reference; parts Edmond names for adapting (the circular
  buffer, the scheduler, the needed part of the fork's async API), reviewed and changed where
  wrong rather than copied (`DECISIONS.md` 2026-10-01).
  Gate: held by discipline, no gate.
- **P1.4 TrueAsync's solution over a new mechanism.** Before a global, a counter, a `zend_try`, an
  extra function or field, find how TrueAsync (true-async/php-async and its core) solves the same
  thing and do the same; a mechanism it lacks needs a recorded reason. Fiber switches are never
  forbidden with `zend_fiber_switch_block()`, and `zend_fiber_switch_blocked()` is never read: the
  scheduler-context flag does it. No `zend_try` or
  global counter on a hot path without Edmond's word. Why: Edmond, 2026-10-02, after the notify
  rework (`DECISIONS.md` 2026-10-02). Flips: the code gets better and is correct (Edmond,
  2026-10-05): the `DECISIONS.md` entry says how it is better and how its correctness was checked
  (a test, a review); a `zend_try` or a global counter on a hot path still needs his word. Gate:
  `tools/check-gates.py` forbids `zend_fiber_switch_block()`, `_unblock()` and `_blocked()` in
  `src/`; the rest is held by the Critic, who compares each change with TrueAsync.
- **P1.5 The core API stays over a smaller core.** A slot, function, macro, flag or field of the
  scheduler API is not removed because nothing calls it: the RFC is written for many providers and
  extensions, and any of them may use it. Why: Edmond, 2026-10-05, after S3.18 and S3.20 removed
  uncalled API and broke the bridge `ext-scheduler-hook` ("RFC создаётся для многих API, функции в
  нём потенциально могут быть кем-то использованы"; S3.21 restored it). Flips: the API is wrong
  (a defect, a contradiction with the RFC), and Edmond agrees. Gate: held by the Critic, who
  rejects a removal argued only by "no caller"; the health check reports such API as fine.

## P2. Compatibility

- **P2.1 RFC limitations over TrueAsync behaviour.** Why: N9; a test that contradicts an RFC rule
  is not ported (D2 in `DECISIONS.md`). Flips: when an RFC change request lifts the limitation.
  Gate: the frozen stage list names the reason of every excluded test.
- **P2.2 TrueAsync classes, interfaces and logic over a cleaner new API.** Why: N8. Flips: P2.1,
  or a reason written in `DECISIONS.md`. Gate: a ported test changes only with a `DECISIONS.md`
  entry; `tools/check-lists.py` refuses a `changed:` tag without one (S2.3).

## P3. Testing and platforms

- **P3.1 A frozen test list over code-first progress.** Why: N6; every stage freezes its list
  before code, and lists are cumulative. Flips: never. Gate: the stage's list file in
  `tests/lists/` precedes its first code commit; held by discipline.
- **P3.2 Windows parity over Linux-first speed.** Why: N11; every stage builds and runs its list
  on Windows (local nmake env in `E:\php` with php-sdk, CI job `windows` on
  `windows-2025-vs2026`), pipes are a mandatory group on both OSes. Flips: a test excluded on
  Windows with its reason named. Gate: the `windows` CI job (S2.4).

## P4. Process

- **P4.1 One component at a time, agreed with Edmond, over a design planned whole.** Why: N5.
  Flips: never. Gate: held by discipline, no gate.

## Where the eleven went

| Source | Here |
|---|---|
| N1 regular extension, no private patch | P1.1 |
| N2 built on the scheduler API, IO hooks, Poll API, Poll additions, Ring | P1.1, P1.2 |
| N3 code from scratch, `ext/async` the reference | P1.3 |
| N4 reactor on bukka's APIs, no libuv | P1.2 |
| N5 components one at a time | P4.1 |
| N6 tests first, frozen cumulative lists | P3.1 |
| N7 every wait is an edge in a wait graph | `DECISIONS.md` (a design choice, not a trade-off) |
| N8 TrueAsync compatibility | P2.2 |
| N9 RFC limitations are our rules | P2.1 |
| N10 BSD-3-Clause | `DECISIONS.md` (a decision, not a trade-off) |
| N11 Windows to the maximum, pipes mandatory | P3.2 |
