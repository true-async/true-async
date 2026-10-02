# Handoff

Where the work stopped and what the next session needs. Replaced whole at every stop; the plan
(`dev/PLAN.md`) outranks this file when they differ.

Written 2026-10-02. Active step: **S3.3** (not started): its split into steps waits for Edmond's
agreement.

## State

- S3.2 closed: the S3.2 fixes are on `async-core` (`2aee763aeed`), merged into
  `async-core-io-2026-10-02` (`8a29d63edcf`), pinned in CI and "Pinned core". CI green on it
  (run 36995546671 at `5ad8c0a`).
- CI change: lanes gate on the lists up to `CLOSED_STAGE` in `.github/workflows/ci.yml` (2); the S3
  list runs in a non-blocking step. Before it, every lane was red since the S3 list was frozen:
  127 of 135 tests FAIL on dbg because no S3 code exists, on the old core as on the new one.
- `tools/roadmap.py --check` fails after every edit of the plan's step marks; rerun
  `tools/roadmap.py` in the same commit.
- No extension code for S3 yet.

## Proposed split of S3.3 (not in the plan until Edmond agrees)

S3.3 internal API; S3.4 classes and the seven `changed:` ports; S3.5 spawn and run; S3.6 suspend;
S3.7 await and GC; S3.8 cancellation and exit paths; S3.9 fibers; S3.10 shutdown windows and
bailout (old S3.4); S3.11 measurements of S3.md section 12; S3.12 fault injection and fuzz; S3.13
stage review (Critic, coverage, Mull, Code Reviewer, `CLOSED_STAGE` = 3); S3.14 security (old S3.5).
Each implementation step owns named tests: every one of the 133 goes to the first step whose
features it uses (15, 9, 33, 35, 25, 15 tests for S3.5-S3.10, `edge_cases/013` to S3.4), to be
written into `dev/plans/S3.md` as section 14. Critic round 1 on the split: 12 findings, all
accepted (tests claimed by steps that lacked their features, no owner for the `changed:` ports,
fuzz before the shutdown windows, the stage Critic before the measurements). "Both core trees":
the `rfc` tree exists only once a change to bukka's RFCs is needed; S3 needs none.

## Next

1. Edmond's answer on the split; then write it into `dev/PLAN.md` and section 14 of S3.md.
2. S3.3.
