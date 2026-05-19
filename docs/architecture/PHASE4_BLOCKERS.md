# Phase 4 — Implementation Blockers

> Things in LibreOffice that cannot be made to match the Word UI as
> specified in [`PHASE4_WRITER_UI_DESIGN.md`](PHASE4_WRITER_UI_DESIGN.md).
>
> Populated **as encountered** during implementation. Each entry
> captures: what we wanted, what blocks it, what we shipped instead
> (the fallback), and what a V2 fix would look like.

---

## Format

Each blocker is one section. Required fields:

- **What we wanted** — the line from the design doc
- **Why blocked** — concrete reason: missing UNO API, native widget
  limitation, sfx2 / framework architecture, fragile cross-app
  shared code, etc.
- **What we shipped** — the V1 fallback that's good enough but not
  parity
- **V2 fix sketch** — what would unblock it, even if expensive

Keep entries terse. Link to the relevant commit / file paths so
future Claude pickups can see the actual code path.

---

## Active blockers

*(populated during implementation — empty for now)*
