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
| 1 | Themes (color/font/effects theme picker) | Design / Document Formatting | none | omit | Would require a document-wide theme model (color sets + font sets + effects bundled). LO has page styles + Designer dialog but no `Theme` aggregate. V2+: add a theme abstraction layer over existing style sets. |
| 2 | Style Sets gallery | Design / Document Formatting | partial | wire to: `.uno:LoadStyles` | LO can load styles from a template, but has no live gallery preview of style sets. V2+: add a gallery widget enumerating template files in user/share styles dirs. |
| 3 | Colors picker (theme colors swap) | Design / Document Formatting | none | omit | Tied to themes. No standalone "swap palette" command in LO. V2+: needs theme model. |
| 4 | Fonts picker (theme fonts swap) | Design / Document Formatting | none | omit | Tied to themes. No "Heading/Body font pair" command in LO. V2+: theme model would expose this. |
| 5 | Paragraph Spacing presets (Compact / Tight / Open / Relaxed / Double) | Design / Document Formatting | partial | wire to: `.uno:ParaspaceIncrease` / `.uno:ParaspaceDecrease` | LO only has incremental adjusters, not named presets. V2+: add a dropdown that applies named para-space presets to default paragraph style. |
| 6 | Effects (shadow/glow theme effects) | Design / Document Formatting | none | omit | Effects are a Word theme concept. V2+: theme model. |
| 7 | Set as Default (current design as default) | Design / Document Formatting | none | omit | Word saves current theme as Normal.dotm default. LO equivalent would be "save as default template" plus theme persistence. V2+: needs theme model + default-template hook. |
| 8 | Page Color (one-click background fill) | Design / Page Background | partial | wire to: `.uno:PageDialog` (Page Style dialog has Background tab) | No dedicated `.uno:PageBackgroundColor` command. V2+: add a quick color picker that sets the page style background fill directly. |
| 9 | Insert Merge Field (named field picker) | Mailings / Write & Insert Fields | partial | wire to: `.uno:InsertField` | LO inserts merge fields via the generic field dialog when a data source is bound, but lacks Word's per-column dropdown. V2+: add a merge-field dropdown that enumerates active data source columns. |
| 10 | Address Block (composed block widget) | Mailings / Write & Insert Fields | none | omit | LO inserts addresses through the Mail Merge Wizard only, not as a standalone ribbon command. V2+: extract the wizard's address block panel into a `.uno:InsertAddressBlock` command. |
| 11 | Greeting Line | Mailings / Write & Insert Fields | none | omit | Same situation as Address Block. V2+: extract from wizard. |
| 12 | Highlight Merge Fields | Mailings / Write & Insert Fields | none | omit | LO has no toggle for highlighting merge fields in the document body. V2+: add view-state toggle that styles SwFieldType::Database fields. |
| 13 | Match Fields | Mailings / Write & Insert Fields | none | omit | Word maps standard address fields to data-source columns. LO maps inside the wizard only. V2+: surface the wizard's column-mapping dialog as a standalone command. |
| 14 | Rules (mail merge if-then-else) | Mailings / Write & Insert Fields | partial | omit | LO supports conditional text via `.uno:InsertField` → Functions, but no Word-style "Rules" dropdown. V2+: add a Rules submenu wrapping the relevant field types. |
| 15 | Check for Errors (preview validation) | Mailings / Preview Results | none | omit | LO has no merge validation command. V2+: scan merge fields against data source schema before generating output. |
| 16 | Find Recipient | Mailings / Preview Results | none | omit | No equivalent. V2+: add search-by-column on the active data source view. |
| 17 | Auto Check for Updates / What's New | Help | none | omit | LO has no in-app "what's new" panel; release notes are external. V2+: in-product changelog viewer. |
| 18 | Contact Support | Help | none | omit | LO routes to community channels rather than commercial support. V2+: open a configurable support URL (per-distribution). |
| 19 | Show Training | Help | none | omit | LO has no built-in training content. V2+: web-based tutorial launcher. |
| 20 | Dictate (Voice group) | Home / Voice | none | wire to: `.uno:SpellingDialog` (placeholder, visual parity only) | Word's Dictate streams audio to cloud STT. LO has no native dictation. V2+: integrate a desktop STT (Whisper-cpp) behind a new `.uno:Dictate` command. Button id `Home-Dictate` is wired to the Spelling dialog as a no-op stub so the ribbon group renders. |
| 21 | Editor (Editor group) | Home / Editor | partial | wire to: `.uno:SpellingDialog` | Word's "Editor" pane is a unified spell/grammar/style review surface. LO's Spelling dialog covers the core slice. V2+: extend with a sidebar deck that aggregates writing-style suggestions (LanguageTool integration already exists as an extension). |
| 22 | Text Effects & Typography (Font group) | Home / Font | partial | wire to: `.uno:FontworkGalleryFloater` | Word's drop-down applies text glow / shadow / outline / 3D effects in-place. LO's Fontwork inserts a separate art object, not character-level effects. V2+: a real char-effect dropdown wired to character shadow/outline/glow attributes. |
| 23 | Multilevel List (Paragraph group) | Home / Paragraph | partial | wire to: `.uno:ChapterNumberingDialog` | LO has chapter numbering as a dialog rather than an inline gallery of multilevel list presets. V2+: gallery widget enumerating built-in list templates plus user-defined ones. |
| 24 | Replace (Editing group split entry) | Home / Editing | equivalent | wire to: `.uno:SearchDialog` | LO's Find & Replace dialog covers both modes; Word splits Find / Replace into two ribbon entries but they open the same underlying dialog. No work needed in V2+. |
| 25 | Add-ins (Add-ins group) | Home / Add-ins | partial | wire to: `.uno:ExtensionManager` | Word's Add-ins button opens the Microsoft store / installed Office add-ins picker. LO's Extension Manager is the equivalent surface (manage installed `.oxt`). V2+: rebrand label "Add-ins" and surface curated extension gallery, but no functional gap. |

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
