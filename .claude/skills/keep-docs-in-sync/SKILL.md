---
name: keep-docs-in-sync
description: Use after any change to the project plan, codebase structure, runtime contract, build steps, conventions, or any user-visible behavior. Verifies the doc set (ROADMAP.md, AGENTS.md, CLAUDE.md, PHASE_X_*.md) still reflects reality before declaring the task done. Treat doc drift as a build failure.
---

# Keep docs in sync

This project keeps a tight, hand-maintained documentation set. Every
change that affects how the code is built, used, planned, or
discussed must be reflected in the docs **before** you mark the task
complete — not in a separate "docs catch-up" pass later.

## The doc set

These five files (plus future `docs/architecture/PHASE_X_*.md` per
phase) are the canonical source of truth. Every change that touches
their subject matter requires a same-commit (or same-PR) update.

| File | What it owns |
|---|---|
| [`docs/architecture/ROADMAP.md`](../../../docs/architecture/ROADMAP.md) | Canonical roadmap. Phase status, per-phase detail, decision log. **Single source of truth for "what we are doing and why".** |
| [`AGENTS.md`](../../../AGENTS.md) | Agent guide: project context, workflow, branch flow, build commands, conventions, runtime contracts (§4.3 Phase 3 logger). One-line phase status table in §4 mirrors ROADMAP. |
| [`CLAUDE.md`](../../../CLAUDE.md) | Claude-specific behavior rules (commit attribution, skill usage, debugging discipline) + brief Phase 3 logger pickup section so a cold Claude session knows the logger is live. |
| [`docs/USAGE.md`](../../../docs/USAGE.md) | Day-to-day operational commands: launching soffice, locating logs, inspecting JSONL, consolidating with `rllogger-export.py`, opting out, troubleshooting. |
| [`docs/architecture/PHASE3_LOGGER_DESIGN.md`](../../../docs/architecture/PHASE3_LOGGER_DESIGN.md) | Full Writer logger design + step-by-step verification log. |
| [`docs/architecture/WRITER_CALC_EXTRACTION.md`](../../../docs/architecture/WRITER_CALC_EXTRACTION.md) | Phase 1 module-deletion analysis. |

## When to update each

After **every** change, ask the following — if any answer is yes,
update the corresponding doc in the same commit.

### Did the change advance a phase, finish a step, or cancel scope?

→ `ROADMAP.md`. Update the phase status table (§2), the per-phase
detail (§3), and add a decision-log entry (§4) if scope shifted.

→ `AGENTS.md` §4 status table — must stay in sync with ROADMAP §2.
A mismatch between the two is a documentation bug.

### Did the change affect the logger's runtime contract?

(Event shape, activation flags, default paths, output files, command
map, trigger semantics, args extraction, lifecycle markers, anything
in `semantic.jsonl` / `raw.jsonl` / `outcome.jsonl`.)

→ `AGENTS.md` §4.3 — the user-facing contract.

→ `PHASE3_LOGGER_DESIGN.md` — the design rationale. Add a row to
the implementation-order table (§7) for any new step. Update the
event-shape examples (§2) when fields change.

→ `CLAUDE.md` "Mevcut durum" section — only when the activation /
opt-out / opt-in story changes.

### Did the change touch build commands, branch flow, or repo conventions?

→ `AGENTS.md` §3 (workflow), §6 (build commands), §8 (commits).

→ `CLAUDE.md` §3-§5 (Claude-specific build discipline + workflow).

### Did the change touch how a user runs the binary or inspects logs?

(New env var, new output file, new helper script, changed default
path, new troubleshooting case.)

→ `docs/USAGE.md` — the operational quick-reference. Keep it concise;
deeper rationale belongs in `PHASE3_LOGGER_DESIGN.md` or
`AGENTS.md` §4.3.

### Did the change add, remove, or rename a top-level module?

→ `AGENTS.md` §4.x module list if the module is part of a phase
contract.

→ `ROADMAP.md` §3 per-phase detail.

### Did a new architecture doc land under `docs/architecture/`?

→ Cross-reference it from `ROADMAP.md` §5 and `AGENTS.md` §10.

## How to actually do this

After implementing the change, **before** the commit:

1. Run `git diff --stat HEAD` to see what files touched. Walk the
   list against the checklist above.
2. Open every doc that needs an update. Make the edits in the same
   working copy as the code change.
3. Bundle code + doc edits in one commit. Use a body line per doc
   file in the commit message so the diff for reviewers is
   self-explanatory.
4. Mention "doc drift" explicitly in the commit message if the
   change is doc-only catch-up — don't bury it.

**Anti-pattern**: shipping the code change first, then a follow-up
"docs(...)" commit "later". The follow-up always slips. Past
examples in this repo where the V1 → V1.1 expansion exposed drift
in `AGENTS.md` §4.3 were only caught because the owner asked for a
final audit before merge. Don't rely on that.

## Trivial / no-op cases

You do **not** need a doc update for:

- Bug fix that doesn't change the documented contract (e.g. fixing
  a SIGSEGV during atexit — the contract was always "session_end
  fires"; the fix just makes it actually work).
- Internal refactor with no external surface change.
- Build-system fix that doesn't change build commands users run.

If the only artifact a downstream reader cares about is a
behavioral guarantee the docs already make, a fix that restores
that guarantee is doc-neutral. **When in doubt, update.**

## Verification

Before declaring the task done, re-read the headline of each touched
doc out loud (or scan the first 20 lines). If anything in those
headlines contradicts what the code now does, the docs are still
stale.
