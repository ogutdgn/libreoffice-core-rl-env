# Phase 4 — Word Features Missing in LibreOffice

> Features that exist in Microsoft Word (Microsoft 365 / Word 2024)
> but do **not** exist in LibreOffice Writer. Phase 4 ships the
> Word **UI shell** around LO's existing functionality, so these
> Word features can't be wired to anything real today — their UI
> elements either:
>
> - **omit** from the ribbon entirely (clean break), or
> - **render the button** but disable / no-op it (visual parity), or
> - **wire to an LO equivalent** (e.g. Word's "Editor" pane wires
>   to LO's existing Spell Check + Tools pane).
>
> Decisions are recorded inline. Implement-later candidates live
> in `## Deferred to V2+` at the bottom.

---

## Format

Each missing feature is one row in the table below. Required fields:

- **Word feature** — the visible UI element / command name
- **Where in Word** — tab + group
- **LO status** — `none` (no equivalent), `partial` (LO has a weaker
  version), `equivalent` (LO has a working but differently-named
  feature we can wire to)
- **V1 decision** — `omit`, `disabled button`, or `wire to: <thing>`
- **V2+ note** — what an actual implementation would entail (or "out
  of scope")

---

## Catalog

| # | Word feature | Where in Word | LO status | V1 decision | V2+ note |
|---|---|---|---|---|---|
| | | | | | |

*(populated during implementation — empty for now)*

---

## Deferred to V2+

*(empty for now)*

---

## Decision rules

When choosing between `omit`, `disabled button`, and `wire to`:

- **Omit** when the feature is so foreign to LO that a disabled
  button confuses more than informs (e.g. cloud-only AutoSave).
- **Disabled button** when the user might reasonably expect it from
  Word and an empty space breaks layout parity (e.g. Microsoft
  Search bar — render it but no-op).
- **Wire to** when LO already does the same thing under a different
  name (e.g. Word's "Editor" pane → LO's `.uno:SpellingAndGrammarDialog`
  + Sidebar Properties deck).

When in doubt, **disabled button** > omit. Visual parity is the goal;
non-functional buttons match the screenshot, missing ones don't.
