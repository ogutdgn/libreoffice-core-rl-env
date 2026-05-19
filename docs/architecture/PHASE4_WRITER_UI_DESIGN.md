# Phase 4 — Writer UI Redesign: MS Word Parity Design

> Status: **research complete 2026-05-18**, implementation pending.
> Branch: `phase4/writer-ui-msword-parity`.
> Target: pixel-level visual + interaction parity with Microsoft Word
> (Microsoft 365 / Word 2024) when a Writer document is open, in
> **Black theme** (the variant the owner asked for).
>
> Reference screenshot supplied by owner — `Document1 - Word` blank
> page, Black theme, English UI, Acrobat extension visible (we won't
> ship that). All design decisions below cite either the screenshot
> or the three research dispatches archived in this commit's history.

---

## 1. Target visual

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ W  AutoSave[Off]  💾 ↶ ↷ ▾ │ Document1 - Word │ 🔍 Search │ OD  _ □ ✕      │ Title bar (#363636)
├──────────────────────────────────────────────────────────────────────────────┤
│ File │ Home │ Insert │ Design │ Layout │ References │ Mailings │ … │  Comments  Editing ▾  Share │ Tab strip (#363636)
├──────────────────────────────────────────────────────────────────────────────┤
│ [Paste] │ Aptos(Body) ▾  11 ▾ A↑ A↓ Aa ✏ │ ☰☰☰ ⮒⮐ ↕ ¶ │ Normal No Spacing Heading… ▾ │ Find ▾ Replace Select ▾ │ Dictate │ Editor │ … │ Ribbon body (#1F1F1F)
│ Clipboard │     Font     │  B I U S x₂ x²    │  ⫷⫸⫹⫺ ↕ ▤ ▦  │   Styles                  │   Editing               │ Voice    │ Editor │ … │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│                       ┌──────────────────────────┐                          │
│                       │                          │                          │ Canvas (#141414)
│                       │     [white page #FFFFFF] │                          │
│                       │                          │                          │
│                       │                          │                          │
│                       └──────────────────────────┘                          │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│ Page 1 of 1 │ 0 words │ Text Predictions: On │ Accessibility: Good to go │  Focus  ▣ ▦ ▥   — 100% +  │ Status bar (#1F1F1F)
└──────────────────────────────────────────────────────────────────────────────┘
```

Three horizontal chrome rows: **title bar (32 px)**, **tab strip (32 px)**, **ribbon body (~92 px)** → total chrome above the document ≈ **156 px** at 100% DPI. Status bar at the bottom is **22 px**. Document canvas fills the remainder with a centered white page on a dark canvas.

---

## 2. Theme spec (Black — primary target)

From Fluent 2 tokens + Word product brand.

| Surface | Hex | Token |
|---|---|---|
| Title bar background | `#363636` | grey[22] |
| Tab strip background | `#363636` | grey[22] |
| Ribbon body background | `#1F1F1F` | grey[12] / NeutralBackground2 dark |
| Status bar background | `#1F1F1F` | grey[12] |
| Canvas around page | `#141414` | grey[8] / NeutralBackground3 dark |
| Page (paper) | `#FFFFFF` | white (Word keeps the page white even in Black theme by default; the "dark page" Word-Switch-Background variant is `#1F1F1F` and is V2 scope) |
| Primary chrome text | `#FFFFFF` | NeutralForeground1 dark |
| Secondary chrome text | `#D6D6D6` | grey[84] / NeutralForeground2 dark |
| Disabled text | `#5C5C5C` | grey[36] |
| Group separator (vertical 1 px) | `#3D3D3D` | grey[24] |
| Hover background | `#383838` | grey[22] subtle |
| Pressed / active-press | `#2E2E2E` | grey[18] |
| Selected (toggle on) | `#082338` bg + `#479EF5` outline | brandWeb[20] + brandWeb[100] |
| Focused keyboard outline | `#0F6CBD` 2 px | brandWeb[80] |
| Word product brand (Share button, account ring) | `#2B579A` | Word brand |
| Hyperlink, accent ring | `#0F6CBD` | brandWeb[80] |

Other themes (Colorful, Dark Gray, White, Use System) are V1.1 scope; **V1 ships Black only** as the default and only variant.

---

## 3. Typography

| Surface | Font | Size | Weight |
|---|---|---|---|
| Title bar text | Segoe UI Variable Text (fallback: Segoe UI, system-ui) | 9 pt / 12 px | 400 |
| Ribbon tab labels | Segoe UI Variable Text | 9 pt / 12 px | 400; active tab 600 |
| Ribbon command labels (under icon) | Segoe UI Variable Text | 9 pt / 12 px | 400 |
| Ribbon group bottom label | Segoe UI Variable Small | 8 pt / 10.6 px | 400, not small-caps |
| Tooltip title / SuperTip | Segoe UI Variable Text | 9 pt | 600 |
| Tooltip body | Segoe UI Variable Text | 9 pt | 400 |
| Status bar | Segoe UI Variable Small | 8 pt | 400 |
| **Document default body** | **Aptos (Body)** | **11 pt** | 400 |
| Document headings | Aptos Display | scaling per Word style | 600 |

Fallback chain for chrome on Linux (no Segoe UI): `Segoe UI Variable, "Segoe UI", "Cantarell", "Liberation Sans", sans-serif`. Aptos is shipped freely by Microsoft (https://aka.ms/AptosFonts) and we ship it in `extras/source/truetype/fonts/`.

---

## 4. Iconography

- **Source**: [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons) — MIT license, OK to bundle.
- **Variants**: Regular (line) for rest state, Filled for toggled-on (Bold-while-active, etc.).
- **Pixel-grid sizes shipped**: 12, 16, 20, 24, 28, 32, 48 — hand-tuned per size, never scaled.
- **Usage in Word's ribbon**:
  - Large buttons (Paste, Format Painter, Dictate, Editor): 32 × 32
  - QAT / title-bar icons: 20 × 20
  - Small ribbon buttons (Bold, Italic, alignment): 16 × 16
  - Dialog launchers (the tiny arrow at bottom-right of each group): 12 × 12
- **Color**: icons render `colorNeutralForeground2` = `#D6D6D6` in Black theme; disabled drops to `#5C5C5C`. Filled / toggled variants tint with `#0F6CBD`.

---

## 5. Ribbon — full tab + group + command inventory

Reproduced verbatim from research dispatch a8c905…; trimmed to drop research-only annotations (contextual tabs and the "varies by build" notes are preserved).

### 5.1 Tab order

`File · Home · Insert · Draw · Design · Layout · References · Mailings · Review · View · Help`

Note: the owner's screenshot omits **Draw** (Microsoft 365 hides it on non-touch installs by default). Phase 4 ships Draw as a hidden-by-default tab; reachable via *File → Options → Customize Ribbon → Draw*.

### 5.2 File tab — Backstage view

| # | Item |
|---|---|
| 1 | Home (landing) |
| 2 | New |
| 3 | Open |
| 4 | Get Add-ins |
| 5 | Info |
| 6 | Save |
| 7 | Save As |
| 8 | Print |
| 9 | Share |
| 10 | Export |
| 11 | Transform |
| 12 | Close |
| 13 | Account |
| 14 | Feedback |
| 15 | Options |

### 5.3 Home tab

| Group | Commands |
|---|---|
| Clipboard | Paste (L, split) · Cut · Copy · Format Painter · [dialog launcher] |
| Font | Font name combo · Font Size combo · Increase Font Size · Decrease Font Size · Change Case (dropdown) · Clear All Formatting · Bold · Italic · Underline (split) · Strikethrough · Subscript · Superscript · Text Effects (dropdown) · Text Highlight Color (split) · Font Color (split) · [dialog launcher] |
| Paragraph | Bullets (split) · Numbering (split) · Multilevel List (dropdown) · Decrease Indent · Increase Indent · Sort · Show/Hide ¶ · Align Left · Center · Align Right · Justify · Line and Paragraph Spacing (dropdown) · Shading (split) · Borders (split) · [dialog launcher] |
| Styles | Quick Style gallery (in-ribbon scrollable, default rows: Normal, No Spacing, Heading 1, Heading 2, Title, Subtitle, Subtle Emphasis, Emphasis, Intense Emphasis, Strong, Quote, Intense Quote, Subtle Reference, Intense Reference, Book Title, List Paragraph) · [dialog launcher opens Styles pane] |
| Editing | Find (split: Find / Advanced Find / Go To) · Replace · Select (dropdown: Select All / Select Objects / Selection Pane) |
| Voice | Dictate (L, split) |
| Editor | Editor (L) |

### 5.4 Insert tab

| Group | Commands |
|---|---|
| Pages | Cover Page (dropdown gallery) · Blank Page · Page Break |
| Tables | Table (L, dropdown grid + Insert Table / Draw Table / Convert Text to Table / Excel Spreadsheet / Quick Tables) |
| Illustrations | Pictures (dropdown: This Device / Stock / Online) · Shapes (gallery) · Icons · 3D Models (split) · SmartArt · Chart · Screenshot (dropdown) |
| Add-ins | Get Add-ins (L) · My Add-ins (split) |
| Media | Online Video (L) |
| Links | Link (split: Insert Link / Recent Items) · Bookmark · Cross-reference |
| Comments | Comment (L) |
| Header & Footer | Header (gallery) · Footer (gallery) · Page Number (dropdown: Top / Bottom / Margins / Current Position / Format / Remove) |
| Text | Text Box (gallery) · Quick Parts (dropdown: AutoText / Document Property / Field / Building Blocks Organizer / Save Selection) · WordArt (dropdown) · Drop Cap (dropdown: None / Dropped / In margin / Options) · Signature Line (split) · Date & Time · Object (split: Object / Text from File) |
| Symbols | Equation (split gallery: Insert New Equation + built-ins) · Symbol (dropdown: Recently used + More Symbols) |

### 5.5 Draw tab (hidden by default)

| Group | Commands |
|---|---|
| Drawing Tools | Select Objects · Lasso Select · Eraser (split) · Pen 1 / Pen 2 / Pencil / Highlighter (pen well) · Add Pen (+) |
| Stencils | Ruler · Drawing Canvas |
| Convert | Ink to Shape · Ink to Math · Ink to Text |
| Replay | Ink Replay |

### 5.6 Design tab

| Group | Commands |
|---|---|
| Document Formatting | Themes (L, gallery) · Style Set gallery (in-ribbon) · Colors (dropdown) · Fonts (dropdown) · Paragraph Spacing (dropdown: No Space / Compact / Tight / Open / Relaxed / Double / Custom) · Effects (dropdown) · Set as Default |
| Page Background | Watermark (L, gallery) · Page Color (L, color picker) · Page Borders (L, opens Borders and Shading) |

### 5.7 Layout tab

| Group | Commands |
|---|---|
| Page Setup | Margins (L, dropdown) · Orientation (Portrait / Landscape) · Size (dropdown) · Columns (dropdown) · Breaks (dropdown: Page / Column / Text Wrapping / Next Page / Continuous / Even Page / Odd Page) · Line Numbers (dropdown) · Hyphenation (dropdown) · [dialog launcher] |
| Paragraph | Indent Left (spinner) · Indent Right (spinner) · Spacing Before (spinner) · Spacing After (spinner) · [dialog launcher] |
| Arrange | Position (gallery) · Wrap Text (dropdown) · Bring Forward (split) · Send Backward (split) · Selection Pane · Align (dropdown) · Group (dropdown) · Rotate (dropdown) |

### 5.8 References tab

| Group | Commands |
|---|---|
| Table of Contents | Table of Contents (L, gallery) · Add Text (dropdown) · Update Table |
| Footnotes | Insert Footnote · Insert Endnote · Next Footnote (split) · Show Notes · [dialog launcher] |
| Citations & Bibliography | Insert Citation (split: Add New Source / Placeholder / Search) · Manage Sources · Style (combo: APA / Chicago / IEEE / MLA / …) · Bibliography (split gallery) |
| Captions | Insert Caption · Insert Table of Figures · Update Table · Cross-reference |
| Index | Mark Entry · Insert Index · Update Index |
| Table of Authorities | Mark Citation · Insert Table of Authorities · Update Table |

### 5.9 Mailings tab

| Group | Commands |
|---|---|
| Create | Envelopes (L) · Labels (L) |
| Start Mail Merge | Start Mail Merge (L, dropdown) · Select Recipients (dropdown) · Edit Recipient List |
| Write & Insert Fields | Highlight Merge Fields · Address Block · Greeting Line · Insert Merge Field (split) · Rules (dropdown) · Match Fields · Update Labels |
| Preview Results | Preview Results (toggle, L) · First / Previous / Go to / Next / Last Record · Find Recipient · Check for Errors |
| Finish | Finish & Merge (L, dropdown: Edit Documents / Print / Send Email) |

### 5.10 Review tab

| Group | Commands |
|---|---|
| Proofing | Editor (L) · Spelling & Grammar (split) · Thesaurus · Word Count |
| Speech | Read Aloud |
| Accessibility | Check Accessibility (split) |
| Language | Translate (dropdown) · Language (dropdown) |
| Comments | New Comment · Delete (split) · Previous · Next · Show Comments (split: List / Contextual) |
| Markup | Display for Review (dropdown: Simple / All / No / Original) · Show Markup (dropdown checklist) · Reviewing Pane (split: Vertical / Horizontal) |
| Tracking | Track Changes (L, split) · Accept (L, split) · Reject (split) · Previous · Next · [dialog launcher] |
| Compare | Compare (L, dropdown) |
| Protect | Block Authors · Restrict Editing |

### 5.11 View tab

| Group | Commands |
|---|---|
| Views | Read Mode · Print Layout · Web Layout · Outline · Draft |
| Immersive | Focus · Immersive Reader |
| Page Movement | Vertical · Side to Side |
| Show | Ruler (checkbox) · Gridlines (checkbox) · Navigation Pane (checkbox) |
| Zoom | Zoom · 100% · One Page · Multiple Pages · Page Width |
| Window | New Window · Arrange All · Split · View Side by Side · Synchronous Scrolling · Reset Window Position · Switch Windows (dropdown) |
| Macros | Macros (split: View / Record / Pause Recording) |

### 5.12 Help tab

| Group | Commands |
|---|---|
| Help | Help · Contact Support · Feedback · Show Training · What's New |

### 5.13 Contextual tabs

| Tab(s) | Trigger | Groups |
|---|---|---|
| Table Design + Layout | Cursor inside a table | TD: Table Style Options · Table Styles · Borders. L: Table · Draw · Rows & Columns · Merge · Cell Size · Alignment · Data |
| Picture Format | Picture selected | Adjust · Picture Styles · Accessibility · Arrange · Size |
| Shape Format | Shape / TextBox / WordArt selected | Insert Shapes · Shape Styles · WordArt Styles · Text · Accessibility · Arrange · Size |
| SmartArt Design + Format | SmartArt selected | (per research) |
| Chart Design + Format | Chart selected | (per research) |
| Header & Footer | Activated | Header & Footer · Insert · Navigation · Options · Position · Close |
| Equation | Selected | Tools · Conversions · Symbols · Structures |
| Background Removal | After Remove Background | Refine · Close |
| Outlining | View → Outline | Outline Tools · Master Document · Close |

---

## 6. Title bar layout (left → right)

| Segment | Contents |
|---|---|
| QAT (left) | Word icon (20 px) · AutoSave toggle (off by default, slider control, label "AutoSave  [Off]") · Save · Undo (split caret for multi-step) · Redo / Repeat · Customize QAT caret |
| Center | `<DocumentName> - Word` (Segoe UI 9 pt). Unsaved doc starts as `Document1 - Word`. AutoSave-on cloud docs show `<Name> - Saved to <Location>` with file-info caret. |
| Right of center | Microsoft Search bar (`Search`, placeholder centered, Alt+Q to focus) |
| Right cluster | Account avatar (20 px circle, `#2B579A` background, white initials) · Comments button (icon + label, badge for unresolved) · Editing dropdown (`Editing` / `Reviewing` / `Viewing`) · Share (pill, `#2B579A`, white text + person-arrow glyph + caret) · OS window controls (min / max / close) |

Note: in Word 2024 builds, the Comments / Editing / Share cluster lives in the **title bar**, not the tab row. The owner's screenshot confirms this layout.

---

## 7. Status bar layout

**Left-anchored** (order from far left):

1. `Page X of Y` (click opens Navigation pane → Pages tab)
2. Spell-check status icon (green check / red X / spinning)
3. Word count (e.g. `0 words` — selection variant shows `n of m words`)
4. Editor feedback indicator
5. `Text Predictions: On` (toggle)
6. `Accessibility: Good to go` / `Investigate` (click opens Accessibility pane)
7. (conditional) Track Changes indicator
8. (conditional) Language indicator
9. (conditional) Macro recording indicator

**Right-anchored** (order from far right):

1. Zoom slider with `−`, `+` buttons, percentage label (`100%`)
2. View shortcut trio: Read Mode · Print Layout (default highlighted) · Web Layout
3. Focus toggle button

Right-click anywhere on the status bar opens **Customize Status Bar** — 23 toggleable items per Word reference.

---

## 8. Task panes / sidebars

- **Right-anchored** (default): Editor, Comments, Reviewing pane (Vertical), Format Picture / Shape / Object, Designer, Researcher, Selection Pane, Citations, Restrict Editing, Dictate / Transcribe.
- **Left-anchored**: Navigation pane (Ctrl+F), Clipboard pane (Home → Clipboard launcher); Styles pane defaults right but can be dragged left.
- **Bottom-anchored**: Reviewing pane (Horizontal variant).
- Each pane has X in its own header and a caret menu for Move / Size / Close / Float. Floating panes are top-level.

**Rulers**: horizontal at top of document column (between ribbon and page), vertical at left edge (Print Layout only). View → Ruler controls visibility.

---

## 9. Initial state

Word default boots to a **Backstage Start screen** (templates + Recent files), not a blank document. Clicking *Blank document* opens the chrome described above.

**Phase 4 decision**: bypass the Start screen and open straight to a blank document. The owner's reference screenshot is the post-blank-doc state, and a Start screen replication would explode V1 scope (template gallery, Microsoft Graph for Recent files, account flyout integration, …). The Start screen variant can return in V1.1.

LO equivalent: disable the Start Center for Writer-only launches; see [§13 implementation plan](#13-implementation-plan), step P4-D.

---

## 10. Default document settings (Word equivalents)

| Setting | Word default | LO will adopt |
|---|---|---|
| Default body font | Aptos (Body) 11 pt | Aptos 11 pt — bundle Aptos in `extras/source/truetype/fonts/`, set as default in `officecfg/registry/data/org/openoffice/Office/Writer.xcu` |
| Default line spacing | 1.08 | 1.08 |
| Paragraph spacing after | 8 pt | 8 pt |
| Page size | Letter (US locale) / A4 (rest) | follow Word: locale-driven default |
| Page margins | 1.0 inch top/bottom/left/right (Normal) | 2.54 cm = 1 inch |
| Default zoom | 100% | 100% |
| Default view | Print Layout | Print Layout (LO calls this "Normal" / "Print Layout") |
| Save format default | .docx | Already .docx via Phase 1; verify |
| AutoSave on cloud docs | On (M365 only) | Off by default (no cloud); leave LO autosave on local 10 min |

---

## 11. LibreOffice mapping — what we'll actually edit

| Word concept | LO equivalent | File(s) |
|---|---|---|
| Ribbon (tab strip + tabbed body) | **Tabbed notebook bar** (`notebookbar.ui`) | `sw/uiconfig/swriter/ui/notebookbar.ui` and family |
| Tab activation | NotebookBar tab `id` mapping | same |
| Group → command structure | `<object class="GtkBox" id="GroupNameBox">` blocks | same |
| Backstage / File menu | LO's File menu + Start Center | `framework/uiconfig/startmodule/menubar/menubar.xml`, `sfx2/uiconfig/sfx/ui/startcenter.ui` |
| Title bar | OS-native by default in LO; we ship a custom titlebar widget through `vcl/source/window/menubarwindow.cxx` | (touchy — see [§14 risks](#14-risks)) |
| QAT | Mapped onto LO's customizable toolbar (`framework/uiconfig/.../singletoolbarmode.xml`) | |
| Microsoft Search | LO has `.uno:SearchDialog` + Find toolbar; we'll add a global command-search popup similar to Cmd+K | new module under `framework/source/uielement/commandsearchpopup.cxx` (V1.1 scope; V1 ships LO's existing Find) |
| Status bar items | `sw/uiconfig/swriter/statusbar/statusbar.xml` | |
| Sidebar / task panes | Sidebar decks under `sfx2/uiconfig/sfx/sidebar/` + `sw/uiconfig/swriter/sidebar/` | |
| Icon theme | Add a `fluent_office/` theme under `icon-themes/`, derived from MIT Fluent UI System Icons | `icon-themes/fluent_office/` (new), `icon-themes/template/links.txt` for fallbacks |
| Default font / margins | `officecfg/registry/data/org/openoffice/Office/Writer.xcu`, `.../Common.xcu` | |
| Default opening behavior | `officecfg/.../Setup.xcu` → `StartCenter` keys | |
| Color theme | LO's experimental dark theme + `vcl/source/app/IconThemeSelector.cxx` | |

---

## 12. Open questions for the owner

| # | Question | Recommendation |
|---|---|---|
| Q1 | Acrobat / extension tabs visible in the reference screenshot — we don't ship those, right? | ✅ confirmed by owner. Acrobat tab omitted. |
| Q2 | Trademark / branding — `Document1 - Writer` + generic blue W icon, no Microsoft logos / Word name | ✅ confirmed by owner. |
| Q3 | Start screen (Backstage Home) behavior | ✅ `soffice --writer` → blank document directly (skip Start Center). No-arg `soffice` keeps the existing LO Start Center (already implemented this way). |
| Q4 | Black theme only in V1, other variants V1.1 | ✅ confirmed by owner. |
| Q5 | Fluent UI System Icons full bundle | ✅ confirmed by owner. |
| Q6 | Rewrite LO's existing Tabbed notebook bar variant | ✅ confirmed by owner. |
| Q7 | Bundle Aptos font in the binary | ✅ confirmed by owner. |
| Q8 | Document Calc / Impress side effects + continue | ✅ confirmed by owner. See [`PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md`](PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md) (created lazily on first side effect). |

**Scope clarification (owner direction)**: Phase 4 wraps LO's **existing** functionality in Word's UI shell. We do **not** implement new Word features that LO lacks. Word features without LO equivalents are catalogued in [`PHASE4_MISSING_FEATURES.md`](PHASE4_MISSING_FEATURES.md) and either omitted, rendered as disabled buttons, or wired to LO equivalents — case by case. Anything in LO that can't be made to match Word's UI gets logged in [`PHASE4_BLOCKERS.md`](PHASE4_BLOCKERS.md) with a V2 fix sketch.

---

## 13. Implementation plan

Each step lands on `phase4/writer-ui-msword-parity`, build-verified, with same-commit doc updates per the `keep-docs-in-sync` skill. Steps are sized so each is one focused commit (with fix-commits as needed, Phase 3 pattern).

| Step | Subject | Verification |
|---|---|---|
| P4-A | Make Tabbed notebook bar the default UI for Writer | `instdir/program/soffice --writer` opens with tabs at top, no classic menubar |
| P4-B | Rewrite Writer's `notebookbar.ui` tab order to Word's: File / Home / Insert / Design / Layout / References / Mailings / Review / View / Help. Hide Draw by default. | All 10 tabs visible, switching tabs works, no crashes |
| P4-C | Rewrite each tab's group structure to match §5 of this doc | Each tab's groups match the Word table; commands map to existing `.uno:*` URLs where they exist |
| P4-D | Disable Start Center for Writer-only launch; open blank doc directly | `soffice --writer` skips Start Center and shows blank `Document1.odt` (saved as `.docx` per Q2) |
| P4-E | Apply Black theme as default — `officecfg` color preferences for the surfaces listed in §2 | Visual matches screenshot for the chrome colors |
| P4-F | Add `fluent_office/` icon theme bundle and set as default | Ribbon icons swap to Fluent style; canvas blocks render correctly |
| P4-G | Title bar redesign — embed QAT (Save, Undo, Redo) on left, Search bar middle, Account + Comments + Editing + Share on right | Window decoration matches §6 |
| P4-H | Status bar rewrite per §7 — drop LO's classic counters, add Word's set | Status bar visually matches |
| P4-I | Bundle Aptos font in `extras/source/truetype/fonts/` and set as default body font in `Writer.xcu` | New document opens with "Aptos (Body) 11" in the font combo |
| P4-J | Default settings sweep — margins (1 in), line spacing (1.08), paragraph spacing after (8 pt), page size (locale-driven) | New blank doc matches Word's geometry |
| P4-K | Sidebar / task pane order pass — make Styles default to right-dock, Navigation pane to left-dock, etc. | Pane positions match §8 |
| P4-L | Side-effects documentation — `PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md` with everything Phase 4 broke / changed in Calc / Impress | Doc exists, lists side effects per commit |
| P4-M | AGENTS.md + ROADMAP.md + USAGE.md sync per `keep-docs-in-sync` skill | All doc references updated, status table moved to ✓ done |

Estimated effort: P4-A through P4-E land first as a "visual MVP" — the chrome looks right. P4-F is the biggest single step (Fluent icon bundle + ~80 icon-link rewrites). P4-G through P4-K are polish. P4-L and P4-M close the phase.

---

## 14. Risks

| Risk | Mitigation |
|---|---|
| Tabbed notebook bar XML schema is undocumented internally — large changes risk visual regressions | Diff against current `notebookbar.ui` step-by-step; reject any change that breaks `soffice --writer` boot |
| Title bar customization in LO requires touching `vcl/source/window/menubarwindow.cxx` — touchy area | Keep P4-G minimal: drop the LO classic menubar, render a header band inside the notebook bar instead of a true OS-titlebar replacement (avoids per-OS native chrome quirks) |
| Aptos font license / distribution — Microsoft offers it free but ship requirements may vary | Use the `aka.ms/AptosFonts` direct distribution route; bundle the SIL OFL-equivalent variant if available, otherwise note the user-install fallback in USAGE.md |
| Fluent UI System Icons — 1900+ icons in the full set; mapping every LO `.uno:` slot may have gaps | Cover the ~80 ribbon-visible commands in V1; missing icons fall back to LO's existing `colibre` theme for that one icon (LO's icon theme fallback chain supports this) |
| Calc / Impress regression — they share `sfx2/` UI infrastructure with Writer | Capture every cross-app change in `PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md`; smoke test Calc + Impress after every step |
| Trademark / branding exposure | See Q2 — default to "Writer" naming + generic blue W icon |

---

## 15. Out of scope for V1

- Light / Colorful / Dark Gray / White themes (only Black ships)
- Backstage Start screen with Recent / Pinned / Templates
- Microsoft Search command-and-content unified search (V1 uses LO's existing Find)
- Cloud / OneDrive integration (Save to OneDrive, Share, Comments at-mentions)
- Dictate / Read Aloud / Editor AI pane functionality (icons land but commands wire to no-ops or LO equivalents)
- Touch-mode UI and stylus / Draw tab functionality
- Co-authoring presence indicators
- AutoSave (cloud)

---

## 16. Cross-references

- [`AGENTS.md`](../../AGENTS.md) §4 — at-a-glance phase status
- [`docs/architecture/ROADMAP.md`](ROADMAP.md) §3.4 — Phase 4 entry in the canonical plan
- [`docs/USAGE.md`](../USAGE.md) — operational commands (will need updates for default opening behavior)
- [`docs/architecture/PHASE3_LOGGER_DESIGN.md`](PHASE3_LOGGER_DESIGN.md) — logger contract Phase 4 must not break
- [`docs/architecture/PHASE4_BLOCKERS.md`](PHASE4_BLOCKERS.md) — running log of LO features that can't reach Word UI parity (populated during implementation)
- [`docs/architecture/PHASE4_MISSING_FEATURES.md`](PHASE4_MISSING_FEATURES.md) — catalog of Word features LO doesn't have; tracks omit / disabled / wire-to decisions
- [`docs/architecture/PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md`](PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md) — cross-app side effects (created lazily on first occurrence)
- Owner-supplied reference screenshot — committed alongside this doc as `phase4-word-reference.png` if storage permits, else linked from the commit message

### Research dispatch outputs (archived in commit history of this branch)

1. **Ribbon tab structure** (agent a8c905…): full 11-tab inventory with commands, button sizes, contextual tabs
2. **Visual style spec** (agent aa451a…): Fluent 2 token reference, hex codes for all 5 themes, Aptos / Segoe UI typography, Fluent UI System Icons, spacing
3. **Chrome & status bar** (agent a9d182…): title bar / QAT / Microsoft Search / status bar / task panes / Start screen behavior

---

## 17. Source-of-truth note

When this doc and the owner's screenshot disagree, **the screenshot wins**. When this doc and the research disagree, **the research wins** unless explicitly noted otherwise (Q2 / Q3 / Q4 / Q9 above). When implementation discovers a constraint that violates this doc, update **this doc**, not the implementation.
