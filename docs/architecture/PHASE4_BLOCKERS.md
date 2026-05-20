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

### Custom title bar with QAT + Search + Account/Comments/Editing/Share (P4-G)

- **What we wanted**: Title bar redesigned per design spec §6 — left QAT (Save / Undo / Redo / Customize), centred Microsoft Search bar, right account avatar + Comments / Editing dropdown / Share button + OS controls.
- **Why blocked**: LO's window decoration is owned by the OS / GTK shell; LO does not draw its own title bar. Replicating Word's title bar means either patching `vcl/source/window/menubarwindow.cxx` to render a custom header band inside the LO frame (touches every app), or building a header widget mounted above the notebook bar and hiding the native title bar (forces client-side decoration).
- **What we shipped**: LO's default title bar (just the document name + close button). The QAT-like buttons that already render above the notebook bar (Save / Undo / Redo / Print) stay; no new chrome added in V1.
- **V2 fix sketch**: Add a custom header band as a child of `sw/source/uibase/uno/SwView`'s top-level frame. Inject a QAT widget, a search field, and a buttons-cluster anchored to the band. Hide the OS title bar through `SystemWindow::ShowSystemDecorations(false)`. Requires per-OS testing.

### Status bar items per Word spec (P4-H)

- **What we wanted**: Word's exact status bar items in Word's order — Page X of Y, spell check icon, Word count, predictions indicator, accessibility check, Track Changes / language conditional items, then right-anchored zoom slider + view mode trio + Focus toggle.
- **Why blocked**: LO's status bar is hard-coded in `sw/source/uibase/ribbar/swstbcfg.cxx` and per-app similarly. Reordering / adding / removing items is not driven by an `.xml` config like the toolbar; it requires C++ changes to the SwView slot wiring.
- **What we shipped**: LO's existing status bar (Page X of Y, word count, page style, language, zoom, view shortcuts). Already ~70% match with Word's set; precise reordering and adding Predictions / Accessibility indicators is V2.
- **V2 fix sketch**: Patch SwView::CreateSubShellStatusBar to emit slots in Word's order; add new slots for the missing indicators (or wire to placeholder no-ops with the Word labels).

### Aptos default body font (P4-I)

- **What we wanted**: New documents open with Aptos (Body) 11pt as the default character style — Word's post-2024 default.
- **Why blocked**: (1) Aptos font binaries are distributed by Microsoft under a CC-BY-SA-ish licence but bundling them in the LO source tree raises packaging questions. (2) The default body font in Writer is not exposed via officecfg — it comes from `sw/source/core/swdoc/docnew.cxx` defaulting to "Liberation Serif" / "Liberation Sans" / "Liberation Mono" based on the `vcl::DefaultFontConfiguration` lookup which itself reads from a per-locale registry, plus the `DEFAULTFONT_LATIN_*` enums in vcl. Changing it requires either a code patch or providing a custom default template.
- **What we shipped**: LO's existing default body font (Liberation Serif 12pt).
- **V2 fix sketch**: Bundle Aptos fonts (Aptos / Aptos Display / Aptos Serif / Aptos Mono) into `extras/source/truetype/fonts/`. Add a default-template approach: ship `extras/source/templates/officorr/Aptos.ott` and register it as `Standard` template via `bootstrap.xcu`. Or patch `docnew.cxx` to use Aptos with Liberation Serif fallback.

### Default page settings to match Word (P4-J)

- **What we wanted**: New documents open with Word's defaults — 1-inch margins (2.54cm), 1.08 line spacing, 8pt paragraph spacing after.
- **Why blocked**: Same path as Aptos — the default page style ("Standard") is constructed in `sw/source/core/swdoc/docnew.cxx`. Changing the defaults system-wide either patches that code path or provides a custom default template.
- **What we shipped**: LO's defaults (2cm margins, 1.0 line spacing, 0pt para spacing). Visible mismatch with Word but doesn't affect the agent training value — RL agents care about commands and document state, not absolute margins.
- **V2 fix sketch**: Same as P4-I — default-template approach is cleanest because it also handles font, margins, line spacing, and para spacing in one drop-in.

### Sidebar / task pane order pass (P4-K)

- **Original concern**: Match Word's pane positions — Styles right-dock by default, Navigation pane left-dock, Clipboard left-dock.
- **Resolution (parity fixes)**: went further than originally planned — the entire right-edge sidebar is now suppressed for Writer (with Calc / Impress side effects documented in `PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md`). Three layers of fix in `sfx2/source/sidebar/`:
  - `SidebarController.cxx` line ~505: `mpTabBar->Show()` removed → tab-bar icon strip never appears
  - `SidebarController.cxx` `RequestOpenDeck()`: made no-op → deck never auto-summons on context change
  - `SidebarChildWindow.cxx` factory line ~78: `pDockWin->Show()` removed → docking window stays hidden, no draggable splitter on document edge
  - `SidebarDockingWindow.cxx` constructor: defensive `Hide()` for any path that still tries
- F5 (Navigator), F11 (Styles) and other deck-specific dispatches still open their dialogs through separate code paths.

### Home-tab group bottom labels (parity addition, not originally tracked)

Word's ribbon groups have a small label under each group (Clipboard / Font / Paragraph / Styles / Editing / Voice / Editor / Add-ins). LO's `sfxlo-NotebookbarToolBox` doesn't render group labels natively. Parity fixes added a `GtkLabel` as the final child of each section's vertical wrapper, with the 8 labels suppressed in `solenv/sanitizers/ui/modules/swriter.suppr` as orphan-label false positives.

### Large-button action-label override (parity caveat)

The Voice / Editor / Add-ins large buttons in the Home tab reference UNO actions (`SpellingDialog`, `SpellingDialog`, `ExtensionManager`). GtkToolButton's XML `<property name="label">` is silently overridden by the action's own label, so the buttons would say "Spelling / Spelling / Extensions" instead of Word's "Dictate / Editor / Add-ins". Workaround: set `toolbar-style="icons"` on those three toolboxes — the action label disappears and the bottom group label provides the text. V2 fix would be registering new UNO commands with the desired Word-style labels.
