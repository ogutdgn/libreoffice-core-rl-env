# Phase 4 — Cross-App Side Effects

> Changes that Phase 4 (Writer UI parity with MS Word) made to LO
> infrastructure shared with Calc and Impress. Listed here so
> Phases 5 (Calc → Excel) and 6 (Impress → PowerPoint) can pick
> them up explicitly — undo them, keep them, or extend them per
> Excel / PowerPoint parity needs.

Populated lazily as side effects appear during Phase 4
implementation.

---

## 1. Sidebar tab bar permanently hidden (`sfx2/source/sidebar/SidebarController.cxx`)

**What changed**: The `mpTabBar->Show()` call that makes the right-
edge vertical icon strip visible on every frame was disabled (the
LOK path already skipped it; we extended that to the native UI
path too).

**Why**: Word's blank-doc UI does not have a persistent right
sidebar tab bar. Removing it was a Phase 4 parity requirement.

**Effect on Calc / Impress**: Same behavior — the right-edge tab
bar (Properties / Slide Transition / Animation panes etc. for
Impress; Properties / Functions / Navigator for Calc) is hidden
by default in those apps too.

**User can still open the sidebar via**:
- `View → Sidebar` menu
- `.uno:Sidebar` UNO command
- `Ctrl+F5` keyboard shortcut
- Specific deck dispatches: `.uno:Navigator` (F5), `.uno:Designer`
  (F11), etc.

**Phase 5/6 decision points**:

- **Excel** has a Properties/Format pane that auto-opens when an
  object is selected (chart, picture, shape). Keeping the tab bar
  hidden but making the panel auto-summon on context change would
  match Excel.
- **PowerPoint** has dedicated right-side panes (Animation,
  Transitions, Designer, Selection). PowerPoint shows these
  on-demand rather than via a permanent tab bar — current Phase 4
  state is closer to PowerPoint than to LO default.

If Phase 5 wants the tab bar back for Calc-only, the cleanest
approach is to add an app-conditional check in the Show() call
site, gated on the application enum (Writer / Calc / Impress).
For now the tab bar stays hidden across all three.

---

## Catalogue (running)

| # | Subsystem | File / config | Phase 4 change | Calc impact | Impress impact |
|---|---|---|---|---|---|
| 1 | Sidebar tab bar | `sfx2/source/sidebar/SidebarController.cxx` line ~505 | Disabled `mpTabBar->Show()` | tab bar hidden, panes summon-only | tab bar hidden, panes summon-only |
| 2 | Sidebar deck auto-open | `sfx2/source/sidebar/SidebarController.cxx` `RequestOpenDeck` | Made no-op; deck never auto-summons | Calc Properties / Functions panes don't auto-pop on selection; users need F5 / F11 etc. | Impress Slide Transition / Animation panes don't auto-pop on slide selection; same shortcut workaround |
| 3 | Sidebar docking window | `sfx2/source/sidebar/SidebarDockingWindow.cxx` ctor | Call `Hide()` at end of ctor; splitter never paints, user can't drag-reveal a docked panel | Calc loses the same drag-to-summon path for its sidebar | Impress loses the same drag-to-summon path for its sidebar |
