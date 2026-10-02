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
- `tools/results.py`: per-test statuses from run-tests output, and a diff of two runs.
- `tools/plan-page.py`: renders `dev/PLAN.md` as the progress page
  (https://claude.ai/artifact/2NzNg5GSZo25MbdDY5PgSE); a daily routine republishes it from `main`.
- `dev/plans/S2.md`: S2 notes: build against the core, runner, test lists, layers, CI, Mull.
- `dev/reviews/io-hooks-design-review.md`: review of the IO hooks design (php/php-src#23997);
  the source of the B1-B3 and M1-M13 references in the plan.
- `dev/reviews/s3-structures/`: raw S3.1 material of 2026-10-01: Edmond's decisions, the two experts'
  reports (fork-to-RFC consolidation, structure layouts), the Critic rounds, probe sources (`.c.txt`).

## Outside the repository

- true-async/php-src: the core, the pinned branch in `WORKFLOW.md` ("Pinned core").
- true-async/php-async at `tests/lists/REFERENCE`: today's TrueAsync, the reference and the source
  of tests.
- `E:\php\true-async-plan.html`: source of the design page of 2026-10-01
  (https://claude.ai/artifact/9oHVzBL9FtYrsACfRJnMpF); it lags behind `PLAN.md`, the progress page
  above does not.

## Not yet present

`RFC-CHANGES.md`: starts with the first change request to an RFC.
