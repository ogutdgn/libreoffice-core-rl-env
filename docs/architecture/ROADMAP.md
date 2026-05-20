# LibreOffice RL Environment — Roadmap

> Single source of truth for what we are building, in what order, and
> what's done vs. pending. Cross-referenced from
> [`AGENTS.md`](../../AGENTS.md) §4.
>
> Last updated: 2026-05-18 (Phase 3 V1.1 merged into `dev`).

---

## 1. Project goal

Take a vanilla LibreOffice fork and turn it into a **runtime
environment for downstream reinforcement-learning agent
experimentation** (Computer Use Agents). Specifically:

1. **Strip** the codebase down to Writer + Calc + Impress — peer
   apps, mobile / non-C++ bindings, legacy filters etc. don't ship to
   RL agents.
2. **Instrument** each remaining app with a structured event logger
   so agent training and replay pipelines can consume real user
   sessions.
3. **Redesign** the UI for visual + interaction parity with MS Word /
   Excel / PowerPoint, so agent skills transfer cleanly between
   Office and this open stack.
4. **Distribute** the result as a Docker multi-stage image with
   `instdir/` baked in — agents pull and run, they don't rebuild.

The final artifact is a container; the source tree itself doesn't
ship downstream. That observation has driven several scope decisions
(see §4 "Decision log").

---

## 2. Phases at a glance

| # | Phase | Status | Notes |
|---|---|---|---|
| 0 | Vanilla build verification | ✓ done — `942e4161c` | Confirmed `instdir/program/soffice` produces from a clean WSL build before any deletions. |
| 1 | Incremental module deletions (1A–1G) | ✓ done — `d38f631d4` | 7 groups, build verified between each. See §3.1. |
| 2 | Folder restructure (`apps/` + `core/`) | **cancelled** | Source cosmetics don't ship to RL agents in the docker image; the restructure cost (cross-module path rewrites, hybrid intermediate states) outweighed the developer-ergonomics gain. See §4.1. |
| 3 | Writer logger | ✓ V1.1 done — `e2515c989` | Always-on event log: raw / semantic / outcome. See §3.3. |
| 4 | Writer UI redesign (→ MS Word) | ✓ V1 + parity fixes done | Tabbed UI default + Word tab order + new Design/Mailings/Help tabs + Dark theme + sifr_dark icons + sidebar fully suppressed + Home tab Word groups with bottom labels. See §3.4. |
| 5 | Calc logger + UI redesign (→ MS Excel) | future | Same recipe as Phases 3 + 4 for Calc. |
| 6 | Impress logger + UI redesign (→ MS PowerPoint) | future | Same recipe for Impress. |
| 7 | Docker multi-stage image | future | Build-stage → runtime-stage with pre-built `instdir/`. |

---

## 3. Per-phase detail

### 3.1 Phase 1 — module deletions ✓

Seven groups, deleted in suggested order, each verified by a full
`make` + headless smoke before the next started. Detailed module
analysis lives in
[`WRITER_CALC_EXTRACTION.md`](WRITER_CALC_EXTRACTION.md).

| Group | What was removed | Risk |
|---|---|---|
| 1A | Peer apps (starmath, basctl, dbaccess, forms, reportbuilder/-designer, sdext, swext) | low |
| 1B | Language bridges (jurt, jvmaccess, jvmfwk, javaunohelper, ridljar, bean, cli_ure, net_ure, rust_uno, jsuno, pyuno, scripting) | low |
| 1C | Mobile / non-Linux platform (android, ios, osx, apple_remote, winaccessibility) | low |
| 1D | Help system (helpcompiler, xmlhelp) | low |
| 1E | Legacy filters (hwpfilter, lotuswordpro) | low |
| 1F | Old tests + extensions (qadevOOo, smoketest, nlpsolver, librelogo, remotebridges) | low — `libreofficekit` + `uitest` preserved |
| 1G | OpenCL (GPU acceleration for Calc — unused) | low |

`libreofficekit` and `uitest` are deliberately preserved because
Phase 3 (logger) builds on the doc-state callbacks LOK exposes, and
`uitest` is a plausible foundation for an agent's action interface
(see AGENTS.md §4 "Preserved for Phase 2").

### 3.2 Phase 2 — folder restructure (cancelled)

Two earlier attempts (`chore/strip-to-writer-calc-impress` then
`refactor/apps-core-folder-split`) tried to move `sw, sc, sd` under
`apps/` and the bulk of supporting modules under `core/` to improve
developer ergonomics. Neither finished build-verified. Both branches
are kept as references but **not** cherry-picked.

The cancellation reasoning is captured in detail in AGENTS.md §4 —
short version: the docker image ships pre-built `instdir/`, not
source, so the only beneficiary of a restructure is us as
developers. The cost (cross-module path rewrites, divergence from
vanilla LibreOffice layout that all upstream documentation assumes,
hybrid half-moved states) outweighed the cognitive win.

The mechanism (`gb_Module_MODULELOCATIONS`) is documented in those
branches if a future phase decides to revisit.

### 3.3 Phase 3 — Writer logger ✓ (V1.1)

Always-on event logger that ships as a new top-level `rllogger/`
module. Three output streams per session:

- `raw.jsonl` — VCL events (key/mouse/focus/command/gesture)
- `semantic.jsonl` — `.uno:*` dispatches mapped to RL-friendly
  names with `args`, `trigger`, and `rawEventIdRange` linking back
  to the originating raw events
- `outcome.jsonl` — current document state (URL, modified flag,
  counts, cursor, selection, format-at-cursor), overwritten every
  250 ms

Default base directory is platform-dependent (`~/.lo-rl-logs/` on
Linux/macOS, `%LOCALAPPDATA%\lo-rl-logs\` on Windows);
`LO_RL_LOG_DIR=/path` overrides; `LO_RL_LOG_DISABLE=1` opts out.
A 50-session cleanup keeps the footprint bounded.

`rllogger/util/rllogger-export.py` consolidates one session
directory into a single JSON document matching the cua-bench
`exportLog()` shape — for RL training / replay pipelines that
expect one file per session.

Full design + verification log:
[`PHASE3_LOGGER_DESIGN.md`](PHASE3_LOGGER_DESIGN.md).
Public runtime contract: [`AGENTS.md`](../../AGENTS.md) §4.3.

**V1 → V1.1 expansion** was driven by a comparison with the
cua-bench TypeScript reference (`cua-bench/apps/figma/mock/src/logger/`).
V1 had functional 3-tier capture but emitted only `argCount` per
dispatch, only counts in outcome, and required `LO_RL_LOG_DIR` to
activate. V1.1 added:

- Step 11: UNO argument extraction (`args:{…}` for every dispatch)
- Step 12: Rich outcome (cursor + selection + format-at-cursor)
- Step 13: Always-on default activation + 50-session cleanup
- Step 14: `rllogger-export.py` consolidator

### 3.4 Phase 4 — Writer UI redesign ✓ (V1)

Goal: visual + interaction parity with Microsoft Word, so an RL
agent trained on Word transfers to Writer with minimal adaptation.

**What shipped in V1**:

- Default UI is LO's Tabbed notebook bar (was: classic menubar +
  multi-row toolbar). `soffice --writer` opens directly into the
  Word-style ribbon layout.
- Tab order rewritten to match Word exactly: File · Home · Insert ·
  Design · Layout · References · Mailings · Review · View · Help.
  Extension and Tools tabs deleted (Word has neither).
- Three new tabs (Design / Mailings / Help) added with sensible LO
  command mappings. Word features without LO equivalents catalogued
  in [`PHASE4_MISSING_FEATURES.md`](PHASE4_MISSING_FEATURES.md) —
  19 entries (Themes / Style Sets / Address Block / Track Changes
  helpers / etc.).
- Dark theme as default: `COLOR_SCHEME_LIBREOFFICE_DARK` for
  document area + `ApplicationAppearance=2` (Dark) for chrome.
  Title bar / ribbon / canvas all dark; page stays white.
- Icon theme set to `sifr_dark` — LO's existing monochromatic
  line-icon variant, closest visual match to Word's Fluent UI.
  True Fluent bundle deferred to V2 (~1900 icons, asset import
  work).

**Resolved trade-offs** (decisions captured in §4.3):

- Branding: "Document1 - Writer" + generic blue W icon (not full
  Word clone).
- Acrobat tab: dropped (LO doesn't ship the Adobe plugin).
- Start screen on no-arg launch: kept LO's existing Start Center.
  `--writer` bypasses it.
- Notebook bar variant: rewrote LO's existing Tabbed mode rather
  than building a new ribbon container.

**V1.1 parity refinements** landed on `phase4/parity-fixes` after
V1 merge (commits `1d3e79e72`..`5456ebb7c`):

- Right-edge sidebar tab bar permanently hidden, dock window kept
  invisible, and `RequestOpenDeck` no-op'd — Word has no
  draggable / auto-summoned right pane. Cross-app side effects on
  Calc / Impress catalogued in
  [`PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md`](PHASE4_SIDE_EFFECTS_CALC_IMPRESS.md).
- QAT (the icon strip on the left of the notebook bar) gained
  Comments / Editing / Share buttons matching Word's title-bar
  right cluster.
- Home tab body fully restructured to Word's 8 groups in order:
  Clipboard / Font / Paragraph / Styles / Editing / Voice / Editor
  / Add-ins, with vertical separators and bottom group labels.
- Voice / Editor / Add-ins large buttons rendered icon-only
  (action labels couldn't be overridden cleanly; the bottom group
  label is the visible text under each icon).

**Deferred to V2** (see [`PHASE4_BLOCKERS.md`](PHASE4_BLOCKERS.md)
for the full sketch):

- Custom single-row title bar (CSD with embedded QAT + Search +
  Account / Comments / Editing / Share cluster — invasive vcl
  decoration override)
- Status bar item reorder to exact Word order
- Aptos default body font + default page settings (margins, line
  spacing, paragraph spacing) — both gated on either a code patch
  to `sw/source/core/swdoc/docnew.cxx` or a default template
- Microsoft Fluent UI System Icons full bundle
- Styles gallery widening (StylesPreview widget is C++-internal)
- Action button label overrides (Voice/Editor would say "Dictate"/"Editor"
  on the button face; currently icons-only with group label below)

### 3.5 Phases 5–6 — Calc + Impress

Same recipe as Phases 3 + 4, applied to Calc (→ Excel) and Impress
(→ PowerPoint). Logger infrastructure is already
application-agnostic (raw + semantic + lifecycle); per-app additions
needed:

- `CommandMap` entries for app-specific `.uno:*` commands
- `OutcomeSnapshot` branch for `sheet::XSpreadsheetDocument` /
  `presentation::XPresentationDocument`
- UI redesign analogous to Phase 4

### 3.6 Phase 7 — Docker distribution

Multi-stage Dockerfile:

- **Build stage**: full LibreOffice build toolchain, runs `make`,
  produces `instdir/`.
- **Runtime stage**: minimal Linux base with X11/Xvfb wrappers,
  pre-built `instdir/`, `rllogger` enabled by default writing to a
  volume mount.
- **Agent interface**: TBD — likely a thin gRPC or HTTP shim around
  `instdir/program/soffice` + the existing `uitest` framework for
  action input.

Image is the **only thing that ships to downstream RL agents**.
Source tree never leaves the repo.

---

## 4. Decision log

### 4.1 Phase 2 cancelled

See §3.2. The docker image ships pre-built `instdir/` containing only
binaries and config; source-tree organization is invisible
downstream. Restructuring purely for developer ergonomics doesn't
clear the cost bar (cross-module path rewrites in many `.mk`
fragments, hybrid half-moved states during the transition, divergence
from vanilla LibreOffice layout that all upstream documentation
assumes).

### 4.2 Phase 3 V1 → V1.1 expansion before merge

After V1 (steps 1–10) was complete on `phase3/writer-logger`, a
side-by-side scan of the cua-bench TypeScript reference surfaced
four gaps that would limit RL training value:

- Dispatch arguments not introspected — `.uno:Color` showed
  `argCount: 3` with no color value.
- Outcome snapshot held counts but not cursor / selection / format.
- Logger required an explicit env var, hurting day-to-day UX.
- No consolidator — consumers had to read three JSONL files.

V1.1 closed all four (steps 11–14) before the merge into `dev`. The
merge therefore contains a logger that is materially complete for
Writer RL training, not a thin V1.

### 4.3 Build location: WSL ext4, not `/mnt/c`

NTFS-through-9P is roughly 10× slower than ext4 for the many-small-
files workload a LibreOffice build produces. The owner's WSL home
(`/home/<user>/lo-dev`) is the canonical build root. Logs from
default-activated `rllogger` land in `~/.lo-rl-logs/` which is
likewise on ext4; Windows-side access is via
`\\wsl.localhost\Ubuntu\home\<user>\.lo-rl-logs\`.

### 4.4 Commit author attribution

Per owner request: no `Co-Authored-By: Claude` (or any AI-attribution)
footer in commit messages. This is captured in
[`CLAUDE.md`](../../CLAUDE.md) §1.

---

## 5. Cross-references

- [`AGENTS.md`](../../AGENTS.md) — canonical agent guide, workflow,
  build commands, conventions
- [`CLAUDE.md`](../../CLAUDE.md) — Claude-specific behavior rules
  (commit attribution, skill usage, debugging discipline)
- [`docs/USAGE.md`](../USAGE.md) — day-to-day operational commands
  (launching soffice, inspecting logs, exporting)
- [`PHASE3_LOGGER_DESIGN.md`](PHASE3_LOGGER_DESIGN.md) — full design
  + step-by-step verification log for the Writer logger
- [`WRITER_CALC_EXTRACTION.md`](WRITER_CALC_EXTRACTION.md) — Phase 1
  module-deletion analysis

### Paused / reference branches (not merged)

- `chore/strip-to-writer-calc-impress` — earlier strip attempt,
  16 commits, ~5900 files deleted. Build not verified. Reference
  only — do not cherry-pick.
- `refactor/apps-core-folder-split` — earlier folder-split attempt
  on top of the above. Build not fully greened. Reference only.

These remain on GitHub for the analysis embedded in their commit
messages; do not use as a delta-source.
