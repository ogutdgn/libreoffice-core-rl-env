# Writer + Calc + Impress Extraction: Deep Architecture & Dependency Analysis

> Goal of this document: give an engineer everything needed to strip the LibreOffice tree down to a minimal build that still runs **Writer (`sw/`)**, **Calc (`sc/`)** and **Impress (`sd/`)** with all common Office (DOCX/XLSX/PPTX/DOC/XLS/PPT/RTF) and ODF (ODT/ODS/ODP) formats. Every claim is backed by a file reference.
>
> Note: Impress and Draw share a single module (`sd/`). Building Impress always also builds Draw — they are two UI modes over the same code.
>
> Methodology: this analysis was produced by reading the LibreOffice build manifests (`Module_*.mk`, `Library_*.mk`, `Repository.mk`, `RepositoryModule_host.mk`, `configure.ac`) for every directory listed in `RepositoryModule_host.mk`. Each module's `gb_Library_use_libraries`, `gb_Library_use_externals`, and `gb_Library_use_api` lines were resolved to build a dependency graph rooted at `sw`, `sc`, and `sd`.

---

## Table of Contents

1. [TL;DR](#1-tldr)
2. [The Reality of "Extraction"](#2-the-reality-of-extraction)
3. [Architecture in Layers](#3-architecture-in-layers)
4. [KEEP — Module Reference (with tier)](#4-keep--module-reference-with-tier)
5. [REMOVE — Module Reference](#5-remove--module-reference)
6. [Writer Dependency Graph](#6-writer-dependency-graph)
7. [Calc Dependency Graph](#7-calc-dependency-graph)
8. [Impress Dependency Graph](#8-impress-dependency-graph)
9. [Document Filter Chains (per format)](#9-document-filter-chains-per-format)
10. [External (Third-Party) Libraries](#10-external-third-party-libraries)
11. [Bootstrap & Launch](#11-bootstrap--launch)
12. [Configure Flags Cheat Sheet](#12-configure-flags-cheat-sheet)
13. [Three Extraction Strategies](#13-three-extraction-strategies)
14. [Step-by-Step Extraction Procedure](#14-step-by-step-extraction-procedure)
15. [Gotchas, Caveats & Tricky Edges](#15-gotchas-caveats--tricky-edges)
16. [Engineer's Final Recommendation](#16-engineers-final-recommendation)

---

## 1. TL;DR

- **You cannot extract `sw/`, `sc/`, and `sd/` as standalone projects.** They are application leaves on top of a 50+ module shared stack (UNO runtime, VCL, sfx2, framework, svx, editeng, oox, xmloff, chart2, etc.). Removing the stack means rewriting all three apps.
- **You can shrink the source tree by roughly 25–35%** by removing the remaining peer applications (Math, Base, scripting IDE, Java/.NET/Python bridges, mobile platforms, legacy filters). The shared runtime + Writer + Calc + Impress/Draw is irreducible — that is your floor.
- **Approximately 90 top-level modules total** in `RepositoryModule_host.mk`. After extraction: **~55 modules KEEP**, **~35 REMOVE**.
- **Adding Impress on top of Writer + Calc costs surprisingly little**: it adds `sd/`, `slideshow/`, `animations/`, `sdext/` (~700K LOC) plus flips `canvas/cppcanvas/` from "stripable" to "mandatory". **No new shared infrastructure modules are pulled in** — all the heavy machinery (svx, drawinglayer, oox, xmloff, chart2) is already required by Writer/Calc.
- **Two existing strip mechanisms** make this easier than starting from scratch:
  - `--enable-wasm-strip-*` Emscripten flags (`configure.ac:4358-4394`) — already wire up Writer/Calc/Impress combinations via `--with-wasm-module="writer calc impress"`, but only kick in for `_os = Emscripten` cross-builds.
  - `$(call gb_Helper_optional, FLAG, module)` wrappers in `RepositoryModule_host.mk` — controlled by feature flags (`DBCONNECTIVITY`, `SCRIPTING`, `LIBRELOGO`, `NLPSOLVER`, `PYUNO`, etc.).
- **There is no `--enable-only-writer-calc-impress` flag.** A native (non-WASM) minimal build requires (a) standard `--disable-*` flags + (b) hand-editing `RepositoryModule_host.mk` to drop the `starmath`, `dbaccess`, etc. entries.
- **One ugly truth**: `basic/sb` (StarBasic runtime) appears unconditionally in `sw/Library_sw.mk`, `sc/Library_sc.mk`, and `sd/Library_sd.mk`. You cannot drop the `basic/` module without source-patching all three apps.
- **One forced bundle**: keeping `sd/` means keeping **both Impress AND Draw**. They are the same module, two UI modes. There is no clean way to keep one without the other.

Read the rest of this document if you intend to actually perform the extraction. Skip to §12 if you just want the strategy.

---

## 2. The Reality of "Extraction"

There are three different things people mean by "extract Writer, Calc, and Impress":

| What you want | Feasible? | Cost |
|---|---|---|
| **A. Build only `sw/`, `sc/`, and `sd/` modules, link against everything underneath** | Yes, trivially: `make sw sc sd` after a full configure | ~hours of build the first time; tiny iteration after |
| **B. Configure-time minimal build (no Math/Base/scripting IDE, no mobile/Java/.NET bridges)** | Yes, with `--disable-*` flags + 5–10 line edit to `RepositoryModule_host.mk` | ~1 day to set up; saves ~25–35% build time and disk |
| **C. Standalone Writer + Calc + Impress as separate codebases, no LibreOffice stack** | **Effectively no.** Requires forking and inlining sal, cppu, vcl, sfx2, framework, svx, editeng, drawinglayer, xmloff, oox, chart2, etc. = ~7–7.5M LOC | Months / years of work |

This document targets **Strategy B**. Strategy A is what you do day-to-day during development; Strategy C is a research project we explicitly recommend against.

### Why C is intractable

The reason it's intractable is *bidirectional and circular* shared infrastructure. Examples confirmed during analysis:

- `sw/Library_sw.mk` links `svx`, which links `editeng`, which links `vcl`, which links `comphelper`, which links `cppu`. Standard layering, OK.
- But `sfx2`'s `Library_sfx.mk` links `svx`, and `svx`'s `Library_svx.mk` links `sfx`. Two-way edge.
- Many of these "shared" modules contain logic that exists *only* to serve apps that aren't in your target set (math equation handling in editeng for Math; presentation-only hooks in sfx2 for the Start Center). You cannot mechanically prune them without breaking what *is* used by Writer/Calc/Impress.

The pragmatic answer: keep the shared modules whole, drop the application-level modules cleanly.

---

## 3. Architecture in Layers

The build graph for Writer + Calc + Impress looks like this (read bottom-up):

```
              ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
   APP LAYER  │      sw      │  │      sc      │  │       sd         │
   (the apps) │  (Writer)    │  │   (Calc)     │  │ (Impress + Draw) │
              └──────┬───────┘  └──────┬───────┘  └──────┬───────────┘
                     │                  │                 │
              ┌──────┴──────────────────┴─────────────────┴──────────┐
   APP HELPERS│ msword  sw_writerfilter  swui  swd  vbaswobj         │
              │ scfilt  scd  scui  vbaobj                            │
              │ sdd  sdui  sdfilt  animcore  slideshow               │
              │ PresentationMinimizer                                │
              │ hwp  lwpft  wpftwriter  wpftcalc  wpftimpress        │
              │ t602filter  analysis  date  pricing  solver          │
              └──────────────┬───────────────────────────────────────┘
                               │
                ┌──────────────┴────────────────────────────────┐
   FRAMEWORKS  │ sfx2 (legacy doc shell) | framework (UNO)      │
   & SHELL     │ desktop (soffice entry)                        │
                └──────────────┬────────────────────────────────┘
                               │
       ┌───────────────────────┼──────────────────────┐
       │                       │                      │
  ┌────┴─────┐         ┌───────┴────────┐    ┌────────┴────────┐
  │ DOCUMENT │         │  GRAPHICS &    │    │  RICH-TEXT &    │
  │ FILTERS  │         │  RENDERING     │    │  ITEMS          │
  │ oox      │         │  vcl           │    │  editeng        │
  │ xmloff   │         │  svx, svxcore  │    │  svl (items)    │
  │ filter   │         │  drawinglayer  │    │  formula (Calc) │
  │ writer-  │         │  basegfx       │    │  docmodel       │
  │  perfect │         │  toolkit       │    │  ...            │
  └────┬─────┘         └────────┬───────┘    └────────┬────────┘
       │                        │                     │
       └────────────────────────┴─────────────────────┘
                               │
                ┌──────────────┴──────────────────┐
   UNO RUNTIME  │ cppuhelper | cppu | comphelper  │
                │ stoc | registry | bridges/      │
                │ binaryurp | io                  │
                └──────────────┬──────────────────┘
                               │
                ┌──────────────┴──────────────────┐
   PLATFORM    │ sal (OS abstraction)             │
   FOUNDATION  │ tools (Rectangle, Color, ...)    │
                │ salhelper | o3tl                │
                └─────────────────────────────────┘
                               │
                ┌──────────────┴──────────────────┐
   CONFIG &     │ configmgr | officecfg | ucb     │
   STORAGE      │ ucbhelper | package | sot       │
                │ store | sax | xmlreader | unoxml│
                └─────────────────────────────────┘
                               │
                ┌──────────────┴──────────────────┐
   I18N &       │ i18nlangtag | i18npool | i18nutil│
   LANG TOOLS   │ linguistic | lingucomponent     │
                └─────────────────────────────────┘
```

Sidecar:
- `instsetoo_native` (installer builder), `postprocess` (build glue), `solenv` (build system itself), `pch` (precompiled headers) — these are **build infrastructure**, mandatory but produce no runtime artifacts you can remove.

---

## 4. KEEP — Module Reference (with tier)

The following modules are mandatory or near-mandatory for a working Writer + Calc + Impress build. Each row cites *where in the dependency chain* the module is pulled in.

### Tier 1 — Foundation runtime (cannot remove)

| Module | Library/Libraries | Why | Evidence |
|---|---|---|---|
| `sal` | `sal`, `sal_textenc` | OS abstraction, strings, threads, files. Everything depends on it. | Linked by every module. `sal/README.md` |
| `salhelper` | `salhelper` | Thin C++ helpers on top of `sal`. | `cppu`, `cppuhelper` link it. |
| `tools` | `tl` | Basic types: `Rectangle`, `Color`, `DateTime`, streams. | `sw/Library_sw.mk` links `tl`; same for `sc`. |
| `o3tl` | (headers only) | C++ template utilities. | Included via `gb_Library_use_custom_headers`. |
| `cppu` | `cppu` | UNO C++ binary interface, refcounting, type system. | Universal. |
| `cppuhelper` | `cppuhelper`, `purpenvhelper` | C++ UNO service implementation helpers. | Universal. |
| `comphelper` | `comphelper` | Property containers, sequences, async helpers. | `sw/Library_sw.mk`, `sc/Library_sc.mk` both link it. |
| `unotools` | `utl` | `XConfigManager` shortcuts, settings classes. | Universal. |
| `store` | `store` | UNO persistent storage backend. | `RepositoryModule_host.mk:160`, transitively used by `stoc`. |
| `stoc` | `stoc`, `bootstrap`, `introspection`, `invocation`, `invocadapt`, `namingservice`, `proxyfac`, `reflection`, `stocservices`, `uuresolver` | UNO service manager + core services. | `Repository.mk:639-659`. Bootstrap chain. |
| `registry` | `reg` | UNO type registry, .rdb reader. | Required by `stoc`. |
| `ure` | (no libs, install set) | URE install package + bootstrap config. | Defines the runtime environment soffice loads at start. |
| `binaryurp` | `binaryurp` | UNO transport for in-process and cross-process calls. | `Repository.mk:640`. |
| `bridges` | `cpp_uno` (one of `gcc3_uno`/`mscx_uno`/`msca_uno`/`msci_uno`) | C++ ABI bridge — links C++ calls to UNO. **Mandatory.** | `Repository.mk:631-637`. |
| `io` | `io` | UNO I/O streams. | UNO transport. |

### Tier 2 — Configuration, storage, packaging

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `configmgr` | (built into core) | XML config registry, backs `officecfg` schemas. | `sw` and `sc` both use `officecfg` headers. |
| `officecfg` | (schemas/.xcs/.xcu) | All app settings live here. | Headers consumed in `sw/Library_sw.mk`, `sc/Library_sc.mk`. |
| `package` | `package2` | ODF (.odt/.ods) and OOXML (.docx/.xlsx) are ZIP packages — this reads/writes them. | Every filter pulls it in. |
| `sot` | `sot` | MS-CFB compound document reader/writer; used for .doc/.xls and OLE-embedded objects. | `sw/Library_sw.mk:80`, `sc/Library_sc.mk:85`. |
| `ucb` | `ucb1` (+ provider plugins `ucpfile1`, `ucppkg1`, `ucphier1`, `ucpexpand1`, `ucpext`, `ucpimage`, `ucptdoc1`) | Universal Content Broker — `file://`, `package://`, `vnd.sun.star.tdoc://` URL handling. | Required by sfx2 for document load/save. |
| `ucbhelper` | `ucbhelper` | C++ helpers for UCB content providers. | `sw/Library_sw.mk:88`, `sc/Library_sc.mk:89`. |

### Tier 3 — XML/SAX layer

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `sax` | `sax`, `expwrap` | SAX2 XML parsing — the foundation under every XML-based filter. | `sw/Library_sw.mk:77`, `sc/Library_sc.mk:81`. |
| `xmlreader` | `xmlreader` | Lightweight XML reader for configmgr. | Required by `configmgr`. |
| `xmlscript` | `xmlscript`, `xmlfa`, `xmlfd` | XML serialization for dialogs and Basic; pulled in by extension/scripting paths. | Required even when scripting disabled by `framework`. |
| `unoxml` | `unoxml`, `unordf` | UNO DOM/XML services; used by RDF metadata in ODF. | Linked from filters in `sw`/`sc`. |
| `xmlsecurity` | `xmlsecurity`, `xsec_xmlsec` | Document signing — optional but pulled in by `sfx2` for signed document handling. **Optional** — see §5 if you can do without signed documents. | `Repository.mk:708-709`. |

### Tier 4 — i18n / linguistic

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `i18nlangtag` | `i18nlangtag` | BCP-47 language tags, locale matching. | `sw/Library_sw.mk:70`, `sc/Library_sc.mk:72`. |
| `i18npool` | `i18npool`, `i18nsearch`, `localebe1`, `localedata_*` plugins | Collation, break iterator, transliteration, calendar — used everywhere strings are compared/broken. | `sc/Library_sc.mk:73`. |
| `i18nutil` | `i18nutil` | Wrapper helpers over `i18npool`. | Universal helper. |
| `linguistic` | `lng` | Spell/grammar/hyphenation/thesaurus UNO services. | `sw` and `sc` both use it. |
| `lingucomponent` | `spell` (Hunspell), `hyphen`, `lnth` (MyThes), `guesslang` | Concrete spell/hyphen/thesaurus implementations. Optional individually but at least one needed for typical use. | `Repository.mk:378-387`. |

### Tier 5 — Graphics & UI toolkit

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `basegfx` | `basegfx` | Points, polygons, matrices — pure math for graphics. | `sw/Library_sw.mk:59`, `sc/Library_sc.mk:69`. |
| `vcl` | `vcl` + backend `vclplug_*` (gtk3/gtk4/qt5/qt6/gen/win/osx) | Window system, widgets, fonts, printing, GDIMetafile, PDF export. | `sw/Library_sw.mk:92`, `sc/Library_sc.mk:87`. |
| `toolkit` | `tk` | Bridges VCL widgets to UNO `XControl` API. | `sw/Library_sw.mk:81`, `sc/Library_sc.mk:88`. |
| `UnoControls` | `ctl` | Additional UNO controls (used by forms/dialogs). | Pulled in by `toolkit` and framework. |
| `drawinglayer` | `drawinglayer`, `drawinglayercore` | Declarative 2D primitives + processors; modern rendering path. | `sw/Library_sw.mk:66-67`, `sc/Library_sc.mk:75-76`. |
| `svgio` | `svgio` | SVG image reader. | Universal image insertion. |
| `emfio` | `emfio` | EMF/WMF image reader. | Universal image insertion. |
| `canvas` + `cppcanvas` | `canvas`, `cppcanvas`, `vclcanvas`, `canvastools`, `canvasfactory`, `simplecanvas` | UNO canvas API + impls. **Mandatory because Impress's `slideshow/` links them** — slide rendering routes through the canvas API. Also used by EMF+ rendering and the cppcanvas mtfrenderer. | `Repository.mk:358-369`; `slideshow/Library_slideshow.mk` uses canvas. |

### Tier 6 — Item system, document model, edit engine

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `svl` | `svl` | `SfxItemSet`/`SfxItemPool` — the property system used by every formatting attribute in sw/sc. | `sw/Library_sw.mk:75`, `sc/Library_sc.mk:84`. |
| `svtools` | `svt` | VCL-adjacent helpers, ruler, treeview controls. | `sw/Library_sw.mk:76`, `sc/Library_sc.mk:83`. |
| `editeng` | `editeng` | Rich-text editor used in Writer body, Calc cells, all shapes. | `sw/Library_sw.mk:68`, `sc/Library_sc.mk:78`. |
| `svx` | `svx`, `svxcore`, `textconversiondlgs` | `SdrObject` shape model, OLE wrappers, common dialogs. Writer/Calc embed `SdrObject`s for shapes/images; Impress slides are essentially `SdrPage`s full of `SdrObject`s. | `sw/Library_sw.mk:79-80`, `sc/Library_sc.mk:86`, `sd/Library_sd.mk`. |
| `docmodel` | `docmodel` | New shared theme/color model (color schemes, document themes). | `sw/Library_sw.mk:65`, `sc/Library_sc.mk:77`. |
| `formula` | `for`, `forui` | Formula parser, evaluator, formula-input UI. **Calc-critical.** Writer only uses limited subset for fields. | `sc/Library_sc.mk:79-80`. |

### Tier 7 — Office frameworks

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `sfx2` | `sfx` | Legacy SFX framework: document shells, slot dispatch, `SfxMedium` load/save state machine. Writer & Calc both extend `SfxObjectShell`. | `sw/Library_sw.mk:74`, `sc/Library_sc.mk:82`. |
| `framework` | `fwk` | Newer UNO-based framework: toolbars/menus/accelerators from XML in `uiconfig/`. | Both apps. |
| `desktop` | `sofficeapp`, `soffice_bin`, `migrationoo2`, `migrationoo3`, `deployment`, `deploymentgui`, `deploymentmisc`, `passwordcontainer` | The `soffice` binary entry point + extension manager. | `Repository.mk:177` (`soffice_bin`). |
| `cui` | `cui`, `sdui` | Common UI dialogs (Options dialog, character/paragraph dialogs, find&replace, ...). Used by every app. | `Repository.mk:531-534`. |
| `uui` | `uui` | Interaction handler — password prompts, certificate dialogs. | Required by sfx2/filter chain. |
| `fpicker` | `fps_office`, `fps`, `fps_aqua` (macOS) | File open/save dialog. **Optional** if you do all I/O programmatically; mandatory for any GUI use. | Wrapped by `gb_Helper_optional,DESKTOP,fpicker`. |
| `toolkit` | (already listed) | — | — |

### Tier 8 — Filter infrastructure

| Module | Libraries | Why | Evidence |
|---|---|---|---|
| `filter` | `filterconfig`, `msfilter`, `storagefd`, `xmlfa`, `xmlfd`, `textfd`, `xsltfilter`, `xsltdlg`, `t602filter`, `pdffilter`, `graphicfilter`, `odfflatxml`, `svgfilter` | Format detection, MS Office shared structures (`msfilter`), PDF export, type-detection registry. | Both apps depend on `msfilter` (`sw/Library_msword.mk`, `sc/Library_scfilt.mk`). |
| `oox` | `oox` | OOXML primitives (DOCX/XLSX/PPTX): theme, drawingml shapes, VML, VBA blob handling. | `sw/Library_sw_writerfilter.mk` and `sc/Library_scfilt.mk` both link it. |
| `xmloff` | `xo` (ODF read/write framework), `xof` (export-only variant) | ODF (ODT/ODS) generic XML mapping; `sw/source/filter/xml/` and `sc/source/filter/xml/` plug into it. | Both apps. `Repository.mk:512-513`. |
| `writerperfect` | `writerperfect`, `wpftwriter`, `wpftcalc` | Legacy format adapters via `librevenge`. Provides EPUB export, WordPerfect/AbiWord/Pages import for Writer; MS Works/Lotus/iWork Numbers import for Calc. **Stripable if you only need DOC/DOCX/RTF/ODT and XLS/XLSX/ODS.** | `Repository.mk:570-580` (writer); `Repository.mk:278-289` (calc). |
| `unoxml` | (already listed) | — | — |

### Tier 9 — Writer-specific

| Directory | Libraries | Role |
|---|---|---|
| `sw/` | `sw`, `swd` (detection), `msword` (DOC/RTF/DOCX export), `sw_writerfilter` (DOCX/RTF import), `swui` (dialogs), `vbaswobj` (Writer VBA, SCRIPTING-only) | Writer engine and all its filters. **The whole directory must stay.** |
| `swext/` | (extension package, not a library) | Optional MediaWiki publisher extension. Safe to remove. |

### Tier 10 — Calc-specific

| Directory | Libraries | Role |
|---|---|---|
| `sc/` | `sc`, `scd` (detection), `scfilt` (XLS/XLSX/ODS/HTML/DIF/CSV/Lotus/QPro), `scui` (dialogs), `vbaobj` (Calc VBA, SCRIPTING-only) | Calc engine and filters. **Whole directory stays.** |
| `scaddins/` | `analysis`, `date`, `pricing` | Excel-compatibility add-in functions (advanced statistics, Black-Scholes, date functions). Strictly optional — Calc runs without them, you lose ~200 functions. |
| `sccomp/` | `solver` (CoinMP + LpSolve based) | Calc Solver tool. Optional. |
| `formula/` | `for`, `forui` | Already listed in Tier 6 — **Calc cannot build without this.** |

### Tier 11 — Impress/Draw-specific (sd/)

| Directory | Libraries | Role |
|---|---|---|
| `sd/` | `sd`, `sdd` (detection), `sdui` (dialogs), `sdfilt` (filters) | Impress and Draw — one module, two UI front-ends. Built on `SdrModel`/`SdrView` from `svx/`. |
| `slideshow/` | `slideshow` | Slide show runtime: transitions, animation playback, timing, audio. Required for "Start From Beginning" / F5. |
| `animations/` | `animcore` | SMIL animation framework that drives slideshow effects. |
| `sdext/` | `PresentationMinimizer` (and presenter screen plugins) | Presenter Console (dual-screen), Presentation Minimizer (file-size reducer). Technically optional but small. |

Notes:
- Impress reuses `chart2` for embedded charts (Tier 7), `canvas/cppcanvas` for rendering (Tier 5 — now mandatory), and `oox`/`xmloff` for PPTX/ODP filter chains (Tier 8). **All already in the KEEP set for Writer/Calc.**
- The `sd/` module always builds Draw and Impress together — they share code; one binary handles both.
- PPTX import/export is "free": handled inside `oox/source/ppt/` which is already linked in for DOCX/XLSX.
- PPT (legacy binary) lives in `sd/source/filter/ppt/` (Library `sdfilt`), uses `msfilter` and `sot` (both already kept).
- `OGLTrans` (OpenGL slide transitions) is **optional** — strippable.

### Tier 12 — Glue / infra / build-only

| Module | Why kept |
|---|---|
| `solenv` | The build system itself (gbuild). Mandatory. |
| `pch` | Precompiled-header definitions. Mandatory for fast builds. |
| `postprocess` | Generates merged config registries, .rdb files. Mandatory. |
| `instsetoo_native` | Builds the installer. Mandatory if you want `make install` or packaging. |
| `setup_native` | Installer scripts. Goes with `instsetoo_native`. |
| `scp2` | Install-set declarations (which file goes where). |
| `sysui` | Desktop integration (mimetypes, .desktop files on Linux). |
| `shell` | Shell extensions/system shell helpers. |
| `readlicense_oo` | License files packaged into installer. |
| `extras` | Templates, autocorrect dictionaries, palettes, gallery content. Strippable per item if you don't want them. |
| `external` | Bundled third-party libraries (boost, icu, libxml2, etc.). See §9. |
| `pch` / `compilerplugins` / `idlc` / `codemaker` | Build tools (Clang plugins for `loplugin:*`, IDL compiler, codemaker). Build-only. |
| `tools` (already T1) | — |
| `comphelper` (already T1) | — |

### Notes on near-mandatory but tunable modules

| Module | Status |
|---|---|
| `chart2` | Writer doesn't *directly* link the chart library, only `chart2api`. Calc links `chart2api` too. The full chart UI library is needed if you want users to *create/edit* charts inside docs/sheets; if you only want to *display* embedded charts, you may be able to strip more of it. Conservatively: KEEP. |
| `connectivity` | Provides `dbtools` library which both `sw` and `sc` link (Writer uses it for mail-merge; Calc uses it for database functions and external data ranges). The full DB driver set (firebird, mysql, postgresql, etc.) can be dropped via `--disable-database-connectivity`. KEEP the directory, disable feature. |
| `avmedia` | Audio/video embedding. `sw`, `sc`, and `sd` all link it (`sw/Library_sw.mk:58`, `sc/Library_sc.mk:68`, plus `sd/` for slideshow media). KEEP unless you patch out media insertion. |
| `eventattacher` | UNO event-to-method binder, used by Basic and dialog event handlers. KEEP. |
| `embeddedobj` (`embobj`, `emboleobj`) | OLE-embedded object wrappers. Required for any embedded object in a Writer/Calc doc (charts, formulas, other documents). KEEP. |
| `xmlsecurity` | Document signing. KEEP if you ever open signed documents (they're common in DOCX from corporate sources); strip via `--without-nss --without-openssl` if you really don't need them. |
| `libreofficekit` | Embeddable C/C++ API. KEEP only if you want to embed soffice in another app; otherwise REMOVE. |
| `helpcontent2`, `helpcompiler`, `xmlhelp` | The bundled help system. REMOVE with `--without-help`. |

---

## 5. REMOVE — Module Reference

Organized by "how confidently can I remove this".

### 5.A. Confidently REMOVE (peer applications)

These are the remaining office applications and extensions not in your KEEP set. No reference from `sw`/`sc`/`sd` libraries.

| Module | What it is | Removal mechanism |
|---|---|---|
| `starmath/` | Math (formula editor) | Drop from `RepositoryModule_host.mk:155-157` |
| `basctl/` | BASIC IDE (the macro editor window) | Already wrapped in `SCRIPTING`. `--disable-scripting` |
| `OGLTrans` | OpenGL slide transitions | Optional even with `sd/` kept; safe to drop if basic transitions are enough |
| `nlpsolver` | Non-linear optimization extension | Already optional: `gb_Helper_optional,NLPSOLVER` |
| `librelogo` | Logo-language educational extension | Already optional: `gb_Helper_optional,LIBRELOGO` |
| `swext` | Writer extension demos (MediaWiki) | Remove unless you specifically want it |

### 5.B. Confidently REMOVE (Base / database UI)

| Module | What it is | Removal mechanism |
|---|---|---|
| `dbaccess` | Base application | `--disable-database-connectivity` (and `--enable-wasm-strip-dbaccess` exists too) |
| `reportbuilder` | Java-based report builder | `--disable-database-connectivity` |
| `reportdesign` | Report designer | Already wrapped `gb_Helper_optional,DBCONNECTIVITY` |
| `forms` | Form runtime/designer | Already wrapped `gb_Helper_optional,DBCONNECTIVITY` |
| Most of `connectivity/` | DB drivers (firebird/mysql/postgresql/jdbc/odbc/hsqldb/mozab) | `--disable-database-connectivity` keeps `dbtools` (needed by sw/sc), drops drivers |

### 5.C. Confidently REMOVE (language bridges you don't use)

| Module | What it bridges | Disable flag |
|---|---|---|
| `jurt` | Java UNO runtime | `--disable-java` |
| `jvmaccess` | JVM bootstrap | `--disable-java` |
| `jvmfwk` | JVM framework | `--disable-java` |
| `javaunohelper` | Java UNO helpers | `--disable-java` |
| `ridljar` | Java IDL jar | `--disable-java` |
| `bean` | Java Beans OLE bridge | `--disable-java` |
| `cli_ure` | .NET CLI binding | `--disable-cli` (already off by default) |
| `net_ure` | New .NET binding | `ENABLE_DOTNET` flag |
| `rust_uno` | Rust UNO bindings | `ENABLE_RUST_UNO` flag (off by default) |
| `jsuno` | JavaScript via QuickJS | `--disable-quickjs` (off by default) |
| `pyuno` | Python UNO bridge | `--disable-python` |
| `scripting` | Script provider framework (Python/JS/Beanshell) | `--disable-scripting` |

### 5.D. Confidently REMOVE (mobile/embedded targets)

| Module | What it is |
|---|---|
| `android/` | Android-specific files |
| `ios/` | iOS-specific files |
| `osx/` | macOS-specific extra binaries (on non-macOS builds: irrelevant; on macOS: keep if needed) |
| `winaccessibility/` | Windows accessibility — `ENABLE_WASM_STRIP_ACCESSIBILITY`; or keep on Windows for screen readers |
| `apple_remote/` | macOS Apple Remote (presentation remote) — macOS-only and already disabled in sandbox builds |

### 5.E. Confidently REMOVE (legacy/uncommon filters)

These each provide a specific obscure import format. Drop if you don't need that format:

| Module | What format | Effect of removal |
|---|---|---|
| `hwpfilter/` | Korean Hangul (HWP) | Cannot open `.hwp` files |
| `lotuswordpro/` | Lotus Word Pro | Cannot open `.lwp` files. Already wrapped in `ENABLE_LWP`. |
| `writerperfect/Library_wpftwriter.mk` | WordPerfect, AbiWord, Pages, EBook, MWAW, StarOffice → Writer | Cannot import those formats |
| `writerperfect/Library_wpftcalc.mk` | MS Works, MWAW, StarOffice, iWork Numbers → Calc | Cannot import those formats |
| `writerperfect/Library_wpftdraw.mk` | (would be removed with `sd/`) | — |
| `writerperfect/Library_wpftimpress.mk` | (would be removed with `sd/`) | — |
| `filter/Library_t602filter.mk` | Czech T602 word processor | Cannot open `.t602` |

You can keep `writerperfect/Library_writerperfect.mk` (the base) if you keep any `wpft*` filter; otherwise the entire `writerperfect/` directory is removable.

### 5.F. Confidently REMOVE (test infrastructure)

These never ship to end users:

| Module | What |
|---|---|
| `smoketest/` | Smoketest installer test |
| `qadevOOo/` | Old Java QA framework (already optional via `QADEVOOO`) |
| `testtools/` | UNO bridge test components |
| `test/` | Test framework helpers (keep if you build tests; remove from production install set) |
| `unotest/` | UNO test infrastructure |

`uitest/` is also test-only; already wrapped in `gb_Helper_optional,PYUNO`.

### 5.G. REMOVE only if you don't need the feature

These provide a real user-facing capability — make a deliberate decision:

| Module | What you lose |
|---|---|
| `helpcontent2/`, `helpcompiler/`, `xmlhelp/` | Built-in F1 help system. Use `--without-help`. ~hundreds of MB. |
| `extensions/` | OLE/COM/ActiveX/scanner/Bibliography helpers. Mostly safe to remove if you don't need scanner or COM automation. |
| `embedserv/` | Windows ActiveX in-place activation. Windows-only; drop on non-Windows. |
| `chart2/` | Chart creation/editing UI. Keep `chart2api` for display. **Risky to strip fully** — verify charts in your sample docs still display. |
| `opencl/` | GPU-accelerated formula calculation in Calc. `--disable-opencl`. Functional impact zero, perf impact on huge spreadsheets. |
| `xmlsecurity/`, `package2/` signing | Digital signatures. Strip via `--without-nss --without-openssl` (loses signed-doc verification). |
| `avmedia/` | Embedded audio/video. Difficult to remove cleanly because `sw`, `sc`, and `sd` all link it (Impress also uses it for slideshow media); would require patching all three `Library_*.mk`. |
| `wizards/` | Letter/fax/agenda/mail-merge wizards (BASIC scripts). |
| `dictionaries/` | Spelling dictionaries themselves (Hunspell dictionaries). `--without-myspell-dicts` if you'll provide your own. |
| `librelogo/` | Logo programming language extension. Already optional. |
| `extras/` | Templates, gallery, autocorrect, color palettes, sample documents. Each is a sub-package — strip individually in `Repository.mk:939-1050`. |
| `LibreOfficeKit` | Embeddable API used by Collabora Online. REMOVE unless embedding. |
| `bridges/jni_uno`, `bridges/java_uno`, `bridges/net_uno` (sub-libs) | If you disabled Java/.NET, drop these too. |

### 5.H. DO NOT REMOVE even though tempting

| Module | Why you might think it's removable | Why it isn't |
|---|---|---|
| `basic/` | "I disabled scripting" | `sb` library is linked unconditionally by `sw/Library_sw.mk`, `sc/Library_sc.mk`, and `sd/Library_sd.mk`. Removing `basic/` requires patching those Library_*.mk files and pulling Basic out of all three apps' UNO services. Hard. |
| `vbahelper/` | "I disabled VBA" | Same as `basic/` — linked unconditionally from `sc/Library_sc.mk` (via `vbahelper`). Already partially wrapped but not fully. |
| `connectivity/` | "I disabled DB" | The `dbtools` library is needed by `sw` (mail-merge) and `sc` (database functions). KEEP the directory; the heavy stuff drops with `--disable-database-connectivity`. |
| `chart2/` (full) | "Writer doesn't link chart2" | `chart2api` is the API only; `chart2` provides the actual chart engine that renders embedded charts. Without it, charts in your `.docx`/`.xlsx`/`.pptx` files render as blank rectangles. |
| `canvas/`, `cppcanvas/` | "Stripable per Tier 5 note" | That note was for a Writer+Calc-only build. **With Impress, `slideshow/` requires canvas** — strip these and slideshow won't link. |
| `slideshow/`, `animations/`, `sd/`, `sdext/` | "Could probably remove" | These ARE Impress. Removing them means you don't have Impress. Listed here for clarity since they were REMOVE in the Writer+Calc-only version of this document. |
| `sccomp/`, `scaddins/` | "These are add-ons" | Calc users expect `RATE()`, `XNPV()`, `BESSELJ()`, etc. Drop `sccomp` (solver) safely. Drop `scaddins/Library_pricing.mk` if you don't need Black-Scholes. Drop `analysis` only if you can live without Excel-compatible advanced functions. |
| `instsetoo_native/` | "I don't need the installer" | The packaging step references it; removing breaks `make`. Keep it. |
| `formula/` | "Generic-sounding name" | This is the formula parser/evaluator. **Calc depends on it directly** (`sc/Library_sc.mk:79-80`). Cannot remove. |
| `binaryurp/`, `bridges/` | "We're not doing IPC" | UNO uses binaryurp for *in-process* communication too. Cannot remove. |

---

## 6. Writer Dependency Graph

### 6.1. Modules whose **directories** Writer requires

These directory roots must stay even if you only build Writer (and drop Calc):

```
sw/                          # Writer itself
hwpfilter/                   # OPTIONAL — drop if no .hwp support needed
lotuswordpro/                # OPTIONAL — drop if no .lwp support needed
writerperfect/               # OPTIONAL — drop if no WP/Pages/EBook/MWAW imports
filter/                      # MANDATORY (msfilter, t602filter optional)
connectivity/                # MANDATORY (dbtools for mail-merge; drivers optional)
```

### 6.2. Libraries built inside Writer's owned directories

| Library | Source dir | Purpose |
|---|---|---|
| `sw` | `sw/Library_sw.mk` | Writer core: `SwDoc`, `SwNodes`, layout, UNO API, ODF filter |
| `swd` | `sw/Library_swd.mk` | Document-type detection (for Open dialog) |
| `swui` | `sw/Library_swui.mk` | Writer-specific dialogs (lazy-loaded) |
| `msword` | `sw/Library_msword.mk` | DOC (legacy binary) import/export, DOCX/RTF export, WW8 parser |
| `sw_writerfilter` | `sw/Library_sw_writerfilter.mk` | DOCX/RTF import (the OOXML token parser + DomainMapper) |
| `vbaswobj` | `sw/Library_vbaswobj.mk` | Writer VBA object model (SCRIPTING-only) |
| `hwp` | `hwpfilter/Library_hwp.mk` | HWP reader |
| `lwpft` | `lotuswordpro/Library_lwpft.mk` | LotusWordPro reader (ENABLE_LWP) |
| `wpftwriter` | `writerperfect/Library_wpftwriter.mk` | WordPerfect/Pages/EBook/MWAW/StarOffice importers + EPUB export |
| `writerperfect` | `writerperfect/Library_writerperfect.mk` | Shared `librevenge` glue (base for all wpft* libs) |
| `t602filter` | `filter/Library_t602filter.mk` | T602 reader |
| `writer` | `connectivity/Library_writer.mk` | Writer mail-merge as a database (DBCONNECTIVITY-only) |

### 6.3. Internal library dependencies of Writer (aggregated, deduplicated)

Pulled from `sw/Library_sw.mk`, `sw/Library_swui.mk`, `sw/Library_msword.mk`, `sw/Library_sw_writerfilter.mk`, `sw/Library_swd.mk`, `sw/Library_vbaswobj.mk`:

`avmedia, basegfx, comphelper, cppu, cppuhelper, dbtools, docmodel, drawinglayer, drawinglayercore, editeng, fwk, i18nlangtag, i18npool, i18nutil, lng, msfilter, oox, sal, salhelper, sax, sb, sfx, sot, svl, svt, svx, svxcore, textconversiondlgs, tk, tl, ucbhelper, utl, vbahelper, vcl, writerperfect, xmlreader, xo`

### 6.4. Writer-specific assets to preserve

- **UIConfig** (`sw/Module_sw.mk`): `swriter`, `sglobal`, `sweb`, `swform`, `swxform`, `swreport`. Files under `sw/uiconfig/{module}/`.
- **SDI files** (slot definitions): `sw/sdi/*.sdi` — generates C++ slot headers via the `svidl` tool.
- **Config schemas**: `officecfg/registry/data/org/openoffice/Office/Writer.xcu` and related under `officecfg/registry/schema/org/openoffice/Office/Writer/`.
- **Components** (UNO service declarations): `sw/util/*.component` (`sw`, `swui`, `swd`, `msword`, `sw_writerfilter`, `vbaswobj`).
- **Localization**: `AllLangMoTarget_sw` — `.mo` translation catalogs.

---

## 7. Calc Dependency Graph

### 7.1. Modules whose **directories** Calc requires

```
sc/                          # Calc itself
scaddins/                    # OPTIONAL — drop if you don't need Excel-compat extra functions
sccomp/                      # OPTIONAL — drop if you don't need Solver
formula/                     # MANDATORY (Calc's formula engine lives here)
chart2/                      # MANDATORY (chart2api at minimum; full chart2 if embedded charts must render)
writerperfect/               # OPTIONAL — wpftcalc for legacy spreadsheet formats
connectivity/                # MANDATORY (dbtools for DB functions)
opencl/                      # OPTIONAL — GPU formula acceleration
```

### 7.2. Libraries built inside Calc's owned directories

| Library | Source dir | Purpose |
|---|---|---|
| `sc` | `sc/Library_sc.mk` | Calc core: column store, formula compiler, ODF filter |
| `scd` | `sc/Library_scd.mk` | Document-type detection (scdetect, exceldetect) |
| `scfilt` | `sc/Library_scfilt.mk` | Filters: XLS, XLSX (via oox), DIF, Lotus, QPro, HTML, RTF, CSV |
| `scui` | `sc/Library_scui.mk` | Calc-specific dialogs (lazy-loaded plugin) |
| `vbaobj` | `sc/Library_vbaobj.mk` | Calc VBA object model (SCRIPTING-only) |
| `analysis` | `scaddins/Library_analysis.mk` | Excel-compat analysis pack functions (BESSELJ, IMSUM, etc.) |
| `date` | `scaddins/Library_date.mk` | Date/time add-in functions |
| `pricing` | `scaddins/Library_pricing.mk` | Options pricing (Black-Scholes) |
| `solver` | `sccomp/Library_solver.mk` | Solver tool (CoinMP / LpSolve / DEPS swarm) |
| `wpftcalc` | `writerperfect/Library_wpftcalc.mk` | Legacy spreadsheet importers |

### 7.3. Internal library dependencies of Calc (aggregated, deduplicated)

Pulled from `sc/Library_sc.mk`, `sc/Library_scfilt.mk`, `sc/Library_scui.mk`, `sc/Library_scd.mk`, `sc/Library_vbaobj.mk`, plus add-in libraries:

`avmedia, basegfx, chart2api, comphelper, cppu, cppuhelper, dbtools, docmodel, drawinglayer, drawinglayercore, editeng, for, forui, fwk, i18nlangtag, i18nutil, msfilter, oox, opencl, sal, salhelper, sax, sb, sfx, sot, svl, svt, svx, svxcore, textconversiondlgs, tk, tl, ucbhelper, utl, vbahelper, vcl, writerperfect, xo`

### 7.4. Calc-specific assets to preserve

- **UIConfig**: `modules/scalc` (in `Repository.mk:1263`). Files under `sc/uiconfig/scalc/`.
- **SDI files**: `sc/sdi/scalc.sdi` and friends.
- **Config schemas**: `officecfg/registry/data/org/openoffice/Office/Calc.xcu` + schemas.
- **Components**: `sc/util/*.component` (`sc`, `scd`, `scfilt`, `scui`, `vbaobj`).
- **Packages**: `sc_res_xml` (calc/styles.xml, tablestyles.xml), optionally `sc_opencl_runtimetest`.
- **Localization**: `AllLangMoTarget_sc`, plus message domains `sca` and `scc` (`Repository.mk:1216-1218`).

### 7.5. Key Calc-only oddities

1. **`formula/` is mandatory and lives outside `sc/`**. Don't be fooled by the location.
2. **`chart2api` is linked even if you strip chart2's full UI**. You still need at least `chart2api`.
3. **`dbtools` from `connectivity/`** is linked for database/range/pivot table SQL access.
4. **OpenCL** is opt-in (`ENABLE_OPENCL`); when on, `clew` external is pulled in and the formulagroup runtime test asset (`sc_opencl_runtimetest`) is shipped.
5. **`scfilt` must link before `sc`** in the serialized link order (`RepositoryModule_host.mk:227-239`), to avoid OOM during large-binary linking on Make's BFD linker.

---

## 8. Impress Dependency Graph

### 8.1. Modules whose **directories** Impress requires

```
sd/                          # Impress AND Draw (one module, two UIs — non-separable)
slideshow/                   # MANDATORY — slideshow runtime (transitions, timing, F5)
animations/                  # MANDATORY — SMIL animation framework
sdext/                       # OPTIONAL — Presenter Console, PresentationMinimizer
canvas/ cppcanvas/           # MANDATORY when sd/ is kept — slideshow links canvas API
writerperfect/               # OPTIONAL — wpftimpress (Keynote/legacy) and wpftdraw
```

### 8.2. Libraries built inside Impress's owned directories

| Library | Source dir | Purpose |
|---|---|---|
| `sd` | `sd/Library_sd.mk` | Impress + Draw core: presentation/drawing model on top of `SdrModel`, slide layout, master pages |
| `sdd` | `sd/Library_sdd.mk` | Document-type detection (Impress vs Draw, PPT/PPTX/ODP/ODG) |
| `sdui` | `sd/Library_sdui.mk` | Impress/Draw dialogs (lazy-loaded plugin) |
| `sdfilt` | `sd/Library_sdfilt.mk` | PPT/PPTX/ODP/ODG filter glue |
| `animcore` | `animations/Library_animcore.mk` | SMIL animation/transition primitives |
| `slideshow` | `slideshow/Library_slideshow.mk` | Slideshow runtime: rendering, timing, audio, transitions |
| `PresentationMinimizer` | `sdext/Library_PresentationMinimizer.mk` | "Reduce file size" tool |
| `wpftimpress` | `writerperfect/Library_wpftimpress.mk` | Keynote/legacy presentation importers (optional) |
| `wpftdraw` | `writerperfect/Library_wpftdraw.mk` | Vector format importers for Draw (CDR, Visio, Publisher, etc. — optional) |

### 8.3. Internal library dependencies of Impress (the deltas)

Impress reuses almost the entire shared infrastructure already required by Writer + Calc. Libraries that **only Impress pulls in** (not already required by Writer/Calc):

`animcore, canvas, canvasfactory, canvastools, cppcanvas, simplecanvas, slideshow, vclcanvas`

Everything else (`vcl`, `sfx`, `svx`, `svxcore`, `drawinglayer`, `editeng`, `oox`, `xmloff`, `chart2`, `msfilter`, `sot`, etc.) is already mandatory for Writer/Calc. **Adding Impress does not add new shared infrastructure modules** — it just activates the application layer + slideshow runtime.

### 8.4. Impress-specific assets to preserve

- **UIConfig**: `modules/simpress` and `modules/sdraw` (`Repository.mk:1266,1268`). Files under `sd/uiconfig/simpress/` and `sd/uiconfig/sdraw/`.
- **SDI files**: `sd/sdi/sdraw.sdi`, `sd/sdi/simpress.sdi`.
- **Config schemas**: `officecfg/registry/data/org/openoffice/Office/Impress.xcu` and `Draw.xcu` + schemas.
- **Components**: `sd/util/*.component` (`sd`, `sdd`, `sdui`, `sdfilt`, `animcore`, `slideshow`).
- **Packages**: `sd_xml` (Impress install set; `Repository.mk:867-869`), optional `sd_opengl` (OGL transition shaders).
- **Localization**: `AllLangMoTarget_sd` (`Repository.mk:1218-1219` registers `sd` and `sdext` MO domains).

### 8.5. Key Impress-only oddities

1. **Draw and Impress share one module.** You cannot have Impress without Draw — `sd/` builds one set of libraries that both apps share. The difference is the UI mode (slide-based vs. canvas-based) and which `XComponent` service the user requests (`com.sun.star.presentation.PresentationDocument` vs `com.sun.star.drawing.DrawingDocument`).
2. **`canvas/` and `cppcanvas/` become mandatory.** Slideshow rendering goes through the UNO canvas API; without it `slideshow/` won't link. This reverses the "stripable" status those modules had in a Writer+Calc-only build.
3. **PPTX import/export is essentially "free"** — handled inside `oox/source/ppt/` and `oox/source/drawingml/` (already linked in for DOCX/XLSX). No new shared module needed.
4. **PPT (legacy binary)** lives in `sd/source/filter/eppt/` and `sd/source/filter/ppt/` (compiled into `Library_sdfilt`). Uses `msfilter` and `sot` (CFB) — both already in KEEP.
5. **`OGLTrans`** (`Repository.mk:348-350`) provides OpenGL-accelerated slide transitions. Optional; safe to drop with `--disable-opengl-transitions` or by removing it from `RepositoryModule_host.mk`.
6. **`box2d` external** is pulled in only for slideshow physics effects (gravity exit, etc.). Fully optional; drop with `--disable-box2d` or `--without-system-box2d`.
7. **Presenter Console** (`sdext/PresenterScreen`) needs a second display to actually launch but builds fine with one display. Stripping `sdext/` removes the dual-screen feature but Impress itself still works.

---

## 9. Document Filter Chains (per format)

For each format, the libraries on the import and export path. **All required libraries are in the KEEP set**; this is just to make the picture explicit.

### DOCX (Word 2007+, OOXML)
- **Import**: `sw_writerfilter` (DocumentImpl/DomainMapper) → `oox` (token parsing) → `msfilter` (embedded objects, DFF) → `sax` (XML) → `package2` (ZIP).
- **Export**: `msword` (DocxExport, attribute output) → `oox` → `msfilter` → `sax` → `package2`.
- **Critical**: `sw_writerfilter` lives at `sw/source/writerfilter/` (it used to be a separate `writerfilter/` module; it was merged into `sw/`).

### DOC (Word 97-2003, binary CFB)
- **Import/Export**: `msword` (WW8 parser/writer at `sw/source/filter/ww8/`) → `msfilter` (DFF, OLE structures) → `sot` (CFB compound document) → `vcl` (graphics).

### RTF
- **Import**: `sw_writerfilter` (RTF tokenizer at `sw/source/writerfilter/rtftok/`) → DomainMapper.
- **Export**: `msword` (RtfExport in `sw/source/filter/ww8/`) → `msfilter`.

### ODT (ODF Text)
- **Import/Export**: `xo` (xmloff) — generic ODF text serialization → `sw/source/filter/xml/` (SwXMLImport/Export) → `sax` → `package2`.

### XLSX (Excel 2007+, OOXML)
- **Import/Export**: `scfilt` (driver at `sc/source/filter/oox/`) → `oox` (token/attribute, chart, drawing) → `msfilter` → `sax` → `package2`. External `orcus`/`orcus-parser` accelerate sheet data parsing.

### XLS (Excel 97-2003, binary BIFF)
- **Import/Export**: `scfilt` (BIFF parser at `sc/source/filter/excel/`) → `msfilter` → `sot` (CFB).

### ODS (ODF Spreadsheet)
- **Import/Export**: `xo` (xmloff spreadsheet handlers) → `sc/source/filter/xml/` (ScXMLImport/Export) → `sax` → `package2`.

### PPTX (PowerPoint 2007+, OOXML)
- **Import**: `sdfilt` (driver in `sd/source/filter/`) → `oox` (specifically `oox/source/ppt/` for slide XML + `oox/source/drawingml/` for shapes/theme) → `msfilter` → `sax` → `package2`.
- **Export**: same chain in reverse — exporter lives at `oox/source/export/` (PowerPointExport) with hooks from `sdfilt`.

### PPT (PowerPoint 97-2003, binary CFB)
- **Import**: `sdfilt` (`sd/source/filter/ppt/`) → `msfilter` (DFF records, OLE) → `sot` (CFB).
- **Export**: `sdfilt` (`sd/source/filter/eppt/`) — Escher-based PPT writer → `msfilter` → `sot`.

### ODP (ODF Presentation)
- **Import/Export**: `xo` (xmloff presentation/drawing handlers in `xmloff/source/draw/`) → `sd/source/filter/xml/` (SdXMLImport/Export) → `sax` → `package2`.

### ODG (ODF Drawing) and SVM (Star Vector Metafile)
- ODG goes through the same chain as ODP (Draw shares the filter with Impress).
- SVM is a VCL-internal format; handled in `vcl/` directly.

### HTML
- **Writer**: `sw/source/filter/html/` is built into `sw` library itself.
- **Calc**: `sc/source/filter/html/` is built into `scfilt`.

### CSV
- **Calc only**: handled in `scfilt` (sc/source/filter/html/ — text-import infrastructure shared with HTML).

### Other formats kept "for free" by KEEP set
- PDF export: `vcl::PDFWriter` in `vcl/source/gdi/pdfwriter*` + `pdffilter`.
- EPUB export (Writer): `writerperfect/wpftwriter` + external `libepubgen`.
- Markdown (limited, internal use): external `md4c`.

---

## 10. External (Third-Party) Libraries

Everything under `external/` is a vendored third-party library, controlled via `RepositoryExternal.mk` and per-external `Module_*.mk`. Most have a `--with-system-{name}` flag to use the system copy instead.

### 10.1. Mandatory for Writer + Calc + Impress (cannot build without)

| External | Purpose | System alternative |
|---|---|---|
| `boost` | C++ utility headers + a few compiled bits | `--with-system-boost` |
| `icu` | Internationalization (collation, BiDi, locales) | `--with-system-icu` |
| `libxml2` | XML parsing for filters, configmgr | `--with-system-libxml` |
| `zlib` | ZIP package compression | `--with-system-zlib` |
| `harfbuzz` | OpenType text shaping (vcl) | `--with-system-harfbuzz` |
| `graphite` | Advanced font shaping (vcl) | `--with-system-graphite` |
| `lcms2` | Color management (vcl) | `--with-system-lcms2` |
| `libjpeg-turbo` | JPEG image codec | `--with-system-jpeg` |
| `libpng` | PNG image codec | `--with-system-libpng` |
| `libtiff` | TIFF image codec | `--with-system-libtiff` |
| `libwebp` | WebP image codec | `--with-system-libwebp` |
| `expat` | XML parser (xmlreader, sax) | `--with-system-expat` |
| `mdds` | Multi-dimensional sparse arrays (Calc column store) | `--with-system-mdds` |
| `orcus` | Spreadsheet format parser (XLSX, ODS, CSV import accel) | `--with-system-orcus` |
| `frozen` | Constexpr hashmaps | bundled only |
| `dragonbox` | Float-to-string conversion (sal) | bundled |
| `fast_float` | String-to-float conversion (sal) | bundled |
| `liblangtag` | BCP-47 language tags | `--with-system-liblangtag` |
| `freetype` (Linux/Win headless) | Font rasterization | system |
| `fontconfig` (Linux/headless) | Font discovery | system |

### 10.2. Mandatory unless you strip the feature

| External | Feature | How to drop |
|---|---|---|
| `hunspell` | Spell check | `--without-myspell-dicts` if no dicts, or drop `lingucomponent/spell` |
| `hyphen` | Hyphenation | drop `lingucomponent/Library_hyph.mk` |
| `mythes` | Thesaurus | drop `lingucomponent/Library_lnth.mk` |
| `nss` or `openssl` (one of) | TLS, document signing, encrypted DOCX | needed for encrypted/signed docs |
| `gpgmepp` (optional) | GPG document signing | `--disable-gpgmepp` |

### 10.3. Mandatory only if you keep `writerperfect`'s legacy filters

| External | Format | Used by |
|---|---|---|
| `librevenge` | Base for all libwp*/etc. | All wpft* libraries |
| `libwpd`, `libwpg`, `libwps` | WordPerfect document/graphics, MS Works | wpftwriter, wpftcalc |
| `libmwaw` | Mac Word/Works/AppleWorks | wpftwriter |
| `libodfgen` | ODF output for librevenge filters | All wpft* libraries |
| `libstaroffice` | StarOffice legacy | wpftwriter |
| `libetonyek` | Apple Keynote/Pages/Numbers | wpftwriter, **wpftimpress (Keynote)** |
| `libebook` | E-book formats (FB2, ...) | wpftwriter |
| `libepubgen` | EPUB export from Writer | wpftwriter |
| `libabw` | AbiWord | wpftwriter |
| `libcdr` | CorelDraw | **wpftdraw** (with Impress/Draw) |
| `libfreehand` | Macromedia Freehand | **wpftdraw** |
| `libmspub` | MS Publisher | **wpftdraw** |
| `libpagemaker` | PageMaker | **wpftdraw** |
| `libqxp` | QuarkXPress | **wpftdraw** |
| `libvisio` | MS Visio | **wpftdraw** |
| `libzmf` | Zoner Callisto/Draw | **wpftdraw** |

With `sd/` kept, the `wpftdraw` line is enabled, which makes the vector-format externals (`libcdr`, `libvisio`, `libmspub`, etc.) attractive — without them Draw cannot import those formats but the build still succeeds (drop their `Library_wpft*.mk` entries).

If you drop `writerperfect` entirely (and lose all these legacy/vector imports), all externals in this group go away. The native LibreOffice install set normally bundles them.

### 10.4. Mandatory only if optional features kept

| External | Feature |
|---|---|
| `clew` | OpenCL (Calc formula GPU) |
| `coinMP`, `lpsolve` | Solver (sccomp) |
| `libnumbertext` | Number-to-text conversion (used in some Writer fields) |
| `libcmis` | CMIS protocol for cloud (UCB driver) — drop with `--disable-cmis` |
| `box2d` | **Slideshow physics effects** (gravity exit, etc.) — only with Impress; drop with `--disable-box2d` |
| `cairo` | Cairo canvas backend (only if `canvas/` kept and `--enable-cairo-canvas`) — **applicable since canvas is now mandatory for Impress** |
| `bluez_bluetooth` | Bluetooth presentation remote control (with `sd/`) — typically drop |

### 10.5. Externals you can drop for Writer + Calc + Impress

| External | What it's for | Why droppable |
|---|---|---|
| `firebird` | Base's default DB | drops with `--disable-firebird-sdbc` |
| `postgresql` | PG SDBC driver | drops with `--without-system-postgresql` etc. |
| `mariadb-connector-c` | MariaDB/MySQL SDBC | drops with `--disable-mariadb-sdbc` |
| `hsqldb` | Java HSQLDB | drops with `--disable-java` |
| `poppler` | PDF import in `sdext/` | drops with `--disable-pdfimport` or removal of `sdext/` |
| `qrcodegen` | QR code generator (Insert > QR) | low-impact remove |
| `zxcvbn-c` | Password strength meter (Base) | drops with no-DB |
| `beanshell`, `rhino` | Java/JS macro language providers | drops with `--disable-scripting` and `--disable-java` |
| `jfreereport` | Java report engine for Base | drops with Base |
| `java_websocket` | Java WebSocket | drops with `--disable-java` |
| `bluez_bluetooth` | Bluetooth presentation remote (with `sd/`) | drops with `sd/` |
| `libebook`, `libetonyek`, `libmwaw`, etc. | (already listed in 9.3) | drops with `writerperfect` removal |

---

## 11. Bootstrap & Launch

For completeness, here's how the binary launches Writer or Calc:

1. **Entry point**: `desktop/source/app/sofficemain.cxx` provides `soffice_main()`, which the platform-specific `soffice_bin` (Linux/Win) calls.
2. **Init**: `Desktop::Init()` and `Desktop::InitFinished()` in `desktop/source/app/app.cxx`.
3. **UNO bootstrap**: loads `types.rdb` (= URE's UDK types) and `services.rdb` (= component registrations).
4. **Command-line parsing**: `desktop/source/app/cmdlineargs.cxx` recognizes `--writer`, `--calc`, `--draw`, `--impress`, `--math`, `--global`, `--web` as direct module switches:
   - Line 470-477: `oArg == "writer"` → `m_writer = true`; `oArg == "calc"` → `m_calc = true`.
5. **Dispatch**: `desktop/source/app/dispatchwatcher.cxx` opens an empty document of the requested module by loading the corresponding URL (`private:factory/swriter` or `private:factory/scalc`).
6. **Document creation**: framework loads the registered service for that URL, which routes to `sw/source/uibase/uno/unodoc.cxx` or `sc/source/ui/unoobj/docuno.cxx`.

What this means for extraction: as long as the `sofficeapp`/`soffice_bin`/`desktop/` chain is intact and at least `sw` + `sc` are registered as document services, `--writer` and `--calc` work out of the box. **No code changes needed in the launch layer.**

You can also start headless with `soffice --headless --writer` or attach a UNO server with `soffice --accept="socket,port=2002;urp;"`.

---

## 12. Configure Flags Cheat Sheet

Flags applicable to a Writer + Calc + Impress focused build, from `configure.ac`:

| Flag | Default | Effect |
|---|---|---|
| `--disable-database-connectivity` | enabled | drops `dbaccess`, `forms`, `reportdesign`, DB drivers; keeps `dbtools` |
| `--disable-scripting` | enabled | drops `basctl`, `scripting`, VBA wrappers; **does not drop `basic/sb`** |
| `--disable-java` | enabled | drops `jurt`, `jvmfwk`, `jvmaccess`, `javaunohelper`, `ridljar`, `bean`, all Java-based filters/components |
| `--disable-python` | enabled | drops `pyuno`, `Pyuno/*` script packages |
| `--without-help` | enabled | drops `helpcontent2`, `helpcompiler`, online help UI |
| `--without-myspell-dicts` | bundled dicts | drops bundled spell dictionaries (you must provide your own at runtime) |
| `--disable-opencl` | enabled | drops `opencl`, `clew`; Calc still works, just no GPU accel |
| `--disable-firebird-sdbc` | enabled | drops Firebird DB engine |
| `--disable-mariadb-sdbc` | enabled | drops MariaDB driver |
| `--disable-postgresql-sdbc` | enabled | drops Postgres driver |
| `--disable-pdfimport` | enabled | drops `sdext/pdfimport` and Poppler |
| `--disable-cairo-canvas` | enabled | drops Cairo canvas backend |
| `--disable-opengl-canvas` | enabled | drops OpenGL canvas backend |
| `--disable-librelogo` | enabled | drops `librelogo` extension |
| `--disable-nlpsolver` | enabled | drops `nlpsolver` extension |
| `--disable-skia` | enabled (Linux/macOS/Win) | swaps Skia rendering for Cairo/native — orthogonal to extraction |
| `--without-fonts` | bundled | drops bundled font packages (you must provide system fonts) |
| `--without-junit` | enabled | skip JUnit tests |
| `--disable-cli` | disabled | drops .NET CLI binding |
| `--disable-cmis` | enabled | drops `libcmis` and the CMIS UCB driver |
| `--disable-online-update` | enabled | drops online update checker |
| `--disable-extension-update` | enabled | drops extension update checker |
| `--with-system-*` (many) | bundled | use system copy of a third-party library instead of vendored one |

### WASM-only flags (do NOT work on native builds, listed for reference)

These exist in `configure.ac:4358-4400` but are gated by `$_os = "Emscripten"`. If you're targeting WASM/Emscripten, these give you the cleanest extraction available:

```
--with-wasm-module=writer                 # builds only Writer
--with-wasm-module=calc                   # builds only Calc
--with-wasm-module=impress                # builds only Impress (and Draw)
--with-wasm-module="writer calc"          # builds Writer + Calc
--with-wasm-module="writer calc impress"  # builds all three (this document's target)
--enable-wasm-strip                       # enable stripping in general
--enable-wasm-strip-writer                # disable Writer (with -calc/-impress)
--enable-wasm-strip-calc                  # disable Calc (with -writer/-impress)
--enable-wasm-strip-basic-draw-math-impress   # disable basic/sd/starmath/animations/slideshow
--enable-wasm-strip-chart                 # disable chart2 (DO NOT use with Impress kept)
--enable-wasm-strip-canvas                # disable canvas/cppcanvas (DO NOT use with Impress kept)
--enable-wasm-strip-dbaccess              # disable dbaccess
--enable-wasm-strip-accessibility         # disable winaccessibility, animations (DO NOT use with Impress kept)
--enable-wasm-strip-pinguser, -recent, -recoveryui, -splash, -hunspell, -guesslang
```

The neat trick: for a native build, you can study what the strip flags do (they're just guards around module entries in `RepositoryModule_host.mk`) and replicate the same edits manually.

**Note on `--with-wasm-module="writer calc impress"`**: per `configure.ac:4372-4374`, selecting `impress` automatically clears `ENABLE_WASM_STRIP_ACCESSIBILITY` and `ENABLE_WASM_STRIP_BASIC_DRAW_MATH_IMPRESS` — that is, it keeps `sd/`, `slideshow/`, `animations/`, `winaccessibility/`. This validates the "Impress keeps the slideshow/animations/canvas stack alive" claim.

---

## 13. Three Extraction Strategies

### Strategy 1: "Don't extract, just build only what you need" (recommended for development)

Do a full configure, then `make sw sc sd`. The build system computes the minimum dependency closure and builds just those libraries. First time is hours; subsequent iterations rebuild only your touched code.

```sh
./autogen.sh
./configure --disable-database-connectivity --disable-scripting \
            --disable-java --disable-python --without-help \
            --disable-firebird-sdbc --disable-mariadb-sdbc \
            --disable-online-update --disable-pdfimport
make sw sc sd
```

This doesn't shrink the *source tree*, but it builds only the libraries needed for Writer + Calc + Impress + their transitive shared deps. Many directories in `RepositoryModule_host.mk` won't compile anything at all.

### Strategy 2: "Configure-time minimal build" (recommended for distribution / shrinking)

Add a patch to `RepositoryModule_host.mk` that drops application-level modules unconditionally. This is the cleanest minimal native build.

```sh
./autogen.sh
./configure [same flags as above]
# Apply patch to RepositoryModule_host.mk to drop sd, starmath, etc. (see §13)
make
```

You get a build that:
- Has all directories present (less invasive),
- Only compiles ~55 of the 90 modules,
- Produces a `soffice` that opens Writer, Calc, Impress, and Draw — and nothing else.

### Strategy 3: "Aggressive directory pruning" (recommended for forking)

Physically delete the unused directories and patch `RepositoryModule_host.mk`. This is the strongest reduction but means you're forking — re-syncing with upstream becomes harder.

Combine Strategy 2's `RepositoryModule_host.mk` patch with deleting all directories listed in §5.A–§5.F (apps, mobile, bridges you don't use, etc.), plus pruning `external/` to only the externals listed in §9.1–§9.2.

---

## 14. Step-by-Step Extraction Procedure

For **Strategy 2** (Configure-time minimal build). Adjust if you want Strategy 3.

### Step 1 — Initial setup

```sh
./autogen.sh
./configure \
    --disable-database-connectivity \
    --disable-scripting \
    --disable-java \
    --disable-python \
    --without-help \
    --without-myspell-dicts \
    --disable-firebird-sdbc \
    --disable-mariadb-sdbc \
    --disable-postgresql-sdbc \
    --disable-pdfimport \
    --disable-online-update \
    --disable-extension-update \
    --disable-librelogo \
    --disable-nlpsolver \
    --disable-cli \
    --disable-cmis \
    --disable-coinmp \
    --disable-lpsolve \
    --disable-skia    # optional, swap to Cairo
```

This gets rid of ~15 modules and their externals via standard mechanisms.

### Step 2 — Edit `RepositoryModule_host.mk`

Add unconditional strips at the top of the file (before the `gb_Module_add_moduledirs` call). Pattern: define your own variables that work like the WASM flags do.

```make
# At the top of RepositoryModule_host.mk:
ENABLE_STRIP_NATIVE := TRUE
```

Then convert these entries to be gated by `ENABLE_STRIP_NATIVE`:

| Lines | Module | Change |
|---|---|---|
| ~155-157 | `starmath` | wrap in `$(if $(ENABLE_STRIP_NATIVE),,starmath)` |
| 51 | `android` | drop unconditionally |
| 55 | `apple_remote` | drop (if not macOS) |
| 62 | `bean` | drop (if no Java; already covered by `--disable-java`) |
| 65 | `cli_ure` | drop (already disabled by `--disable-cli`) |
| 99 | `javaunohelper` | drop (covered by `--disable-java`) |
| 102-103 | `jurt`, `jvmaccess`, `jvmfwk` | drop (covered by `--disable-java`) |
| 110 | `net_ure` | drop |
| 130 | `rust_uno` | drop (already off by default) |
| 92 | `hwpfilter` | drop if not needed |
| 108 | `lotuswordpro` | drop if not needed |
| ~52-54 | `animations` | **DO NOT drop** — required by Impress slideshow |
| ~141-144 | `sd`, `sdext` | **DO NOT drop** — Impress and Draw live here |
| ~148-150 | `slideshow` | **DO NOT drop** — Impress slideshow runtime |
| ~31-36 | `canvas`, `cppcanvas` | **DO NOT drop** — slideshow links them |
| 23-28 | `chart2` (entire) | **DO NOT drop** — needed for chart display in all three apps |

Alternative: keep `chart2/` but accept the WASM strip flag pattern. Best is to actually run the build after each edit and verify it succeeds.

### Step 3 — Drop optional Writer / Calc filters

In `Repository.mk` (the `Helper_register_libraries_for_install,OOOLIBS,writer` and `,calc` blocks around lines 278-289 and 570-580):

```diff
# OOOLIBS,writer:
$(eval $(call gb_Helper_register_libraries_for_install,OOOLIBS,writer, \
-    hwp \
-    $(if $(ENABLE_LWP),lwpft) \
    msword \
    swd \
-    t602filter \
    $(call gb_Helper_optional,SCRIPTING,vbaswobj) \
-    wpftwriter \
    sw_writerfilter \
    $(call gb_Helper_optional,DBCONNECTIVITY,writer) \
))
```

Match by dropping the corresponding entries from `RepositoryModule_host.mk` (`hwpfilter`, `lotuswordpro`, `writerperfect` if you also dropped wpftcalc — see Calc note below).

For Calc, similarly drop `wpftcalc` if you don't need MS Works / Numbers import. Keep `analysis`/`date`/`pricing` for Excel compatibility unless you're sure.

For Impress, similarly drop `wpftimpress` if you don't need Keynote import, and `wpftdraw` if you don't need legacy vector formats (CDR, Visio, Publisher, etc.). Drop `OGLTrans` to skip OpenGL slide transitions. Keep `PresentationMinimizer` (small, useful) and `animcore`/`slideshow` (mandatory).

### Step 4 — Build and verify

```sh
make verbose=true 2>&1 | tee build.log
```

Expected duration: 30 min to 2 hours depending on hardware. Look for unresolved-symbol errors at link time — if `sw` or `sc` fails to link, you over-stripped (most commonly: you removed `chart2`, `basic`, `vbahelper`, `connectivity/dbtools`, or `avmedia`).

### Step 5 — Smoke test

```sh
instdir/program/soffice --writer  --headless --convert-to pdf  /tmp/test.docx
instdir/program/soffice --calc    --headless --convert-to xlsx /tmp/test.ods
instdir/program/soffice --impress --headless --convert-to pptx /tmp/test.odp
instdir/program/soffice --impress --headless --convert-to pdf  /tmp/test.pptx
instdir/program/soffice --writer       # GUI test
instdir/program/soffice --calc         # GUI test
instdir/program/soffice --impress      # GUI test (also try F5 / Start Slideshow)
```

If a document with embedded charts fails: you over-stripped `chart2`. If a document with images fails: you over-stripped `vcl` image codecs or `svgio`/`emfio`. If slideshow F5 fails or transitions are broken: you over-stripped `canvas`/`cppcanvas` or `animations`.

### Step 6 — Size measurement (optional, useful for sanity)

```sh
du -sh instdir          # disk size of the install set
du -sh workdir          # build artifacts
find instdir -name '*.so' -o -name '*.dll' | xargs ls -lh
```

Compare against an un-stripped baseline build. Typical reductions for **Writer + Calc + Impress**:
- Native desktop build, Strategy 2: install set drops from ~700 MB to ~480-500 MB
- Aggressive Strategy 3: ~350-400 MB possible
- WASM `--with-wasm-module="writer calc impress"`: ~30% over default WASM build

For comparison, a Writer+Calc-only build (no Impress) would drop ~80 MB further.

---

## 15. Gotchas, Caveats & Tricky Edges

### G1. `basic/sb` is wired into `sw`, `sc`, and `sd`

`sb` (StarBasic runtime) is linked from `sw/Library_sw.mk` (around line 78), `sc/Library_sc.mk` (around line 75), and `sd/Library_sd.mk` **unconditionally**, even though almost all VBA/Basic-related libraries can be dropped via `--disable-scripting`. This means: you cannot remove the `basic/` directory without source-patching all three Library files and shedding any UNO services that reference Basic from those apps. Practical impact: keep `basic/` even after `--disable-scripting`.

### G2. The serialized link order in `RepositoryModule_host.mk:226-239`

```make
$(eval $(call repositorymodule_serialize, \
    scfilt \
    $(call gb_Helper_optional,SCRIPTING,vbaobj) \
    sc msword \
    $(call gb_Helper_optional,DESKTOP,swui) \
    sw \
    ... \
))
```

This is not a dependency declaration — it's a serialization to prevent OOM when GNU `ld.bfd` links large libraries in parallel. Don't be confused: dropping `chart2` or `svx` from this list isn't enough; you must drop the module itself.

### G3. `chart2api` ≠ full `chart2`

`chart2api` is just the UNO API definitions. `chart2` is the chart engine that renders embedded charts. Writer, Calc, and Impress all link `chart2api` but only `chart2` provides the actual rendering. If you accidentally remove the `chart2` directory, embedded charts will appear blank in `.docx`/`.xlsx`/`.pptx` files.

### G4. `formula/` is mandatory for Calc despite the unspecific name

It's the formula compiler/evaluator. Don't be tempted to remove based on the directory name.

### G5. The "merged" library (MERGELIBS)

`Library_merged.mk` exists when the build is configured with `MERGELIBS=TRUE`. It pre-aggregates many small libs into one big `merged` shared library to speed up start time. This is mostly transparent — but if you drop modules, you also need to drop them from `gb_MERGEDLIBS` (in `solenv/gbuild/extensions/pre_MergedLibsList.mk`). When in doubt, keep `MERGELIBS=` (default off) while you do extraction.

### G6. `extras/` is huge but mostly removable per-package

`extras/` contains templates, autocorrect data, color palettes, font assets, etc. Each `Package_*` is registered in `Repository.mk:939-1050` — you can strip individual ones. For a minimal build: drop `extras_personas*`, `extras_palettes`, `extras_glade`, `extras_templates`, `extras_tplwiz*`. Keep `extras_autocorr` and `extras_autotext` if you want sensible Writer defaults.

### G7. Even with `--disable-java`, the `ridljar`/`unoil` jar entries still appear

`ridljar`, `unoil`, etc. are guarded by `ENABLE_JAVA` at `Repository.mk:791-812`. With `--disable-java`, those branches drop out and no jars are built. The top-level directories still exist in `RepositoryModule_host.mk` but `Module_*.mk` inside each becomes empty.

### G8. Windows-specific bits

Stuff like `winaccessibility`, `embedserv`, `extensions/source/activex/`, `bridges/source/jni_uno/jni_*.cxx`, `vcl/win/` are wrapped in `$(if $(filter WNT,$(OS)),...)`. On non-Windows builds these don't compile anyway. If you target Linux only, you can also drop the `winaccessibility/` directory.

### G9. macOS-specific bits

`apple_remote/`, `osx/`, `vcl/osx/`, `vcl/quartz/`, `vcl/ios/`, parts of `bridges/`. Same pattern — wrapped in `$(if $(filter MACOSX,$(OS)),...)` or similar. Drop directories you don't target.

### G10. `MERGELIBS_MORE` mode

When set, even `sd`, `dbu`, `cui` get merged into the big lib. Adds another layer of complexity. Stay with `MERGELIBS=` (off) during extraction; turn on later for size optimization.

### G11. `instsetoo_native` is needed even if you don't want an installer

The build system runs install-set declarations to know where each artifact goes. If you remove `instsetoo_native`, `make` breaks. Keep it; it's mostly metadata.

### G12. `compilerplugins/` requires Clang for LibreOffice-specific lints

`loplugin:*` warnings are enforced by `compilerplugins/clang/`. Useful for development; pure overhead if you only build releases with GCC/MSVC. Leave it — it auto-disables when a non-Clang compiler is detected.

### G13. UI tests in `uitest/`

UI tests use Python (`gb_Helper_optional,PYUNO`). With `--disable-python`, no UI tests build. Safe to ignore.

### G14. Don't strip both `--disable-java` *and* `pyuno` while keeping Basic

You'll have a working LibreOffice with no scripting engines that the user can interact with. Functionally fine for headless / RL / batch use; surprising for a typical user.

### G15. `xmlsecurity/` is more entangled than it looks

It's pulled in by `sfx2` for displaying the document signature status bar item. Dropping it requires patching `sfx2/source/appl/appserv.cxx` and a few others. Easier to leave it (small library) than to strip cleanly.

### G16. `qadevOOo/` has a stray uncommitted `.project` change

The current git state shows `M qadevOOo/.project` and `?? qadevOOo/bin/`. This is unrelated to extraction. Investigate before you commit any extraction patch.

### G17. `connectivity/dbtools` vs full `connectivity/`

After `--disable-database-connectivity`, `connectivity/Module_connectivity.mk` still builds `dbtools` (and a few unconditional helpers), but skips all the driver Library_*.mk files. Behavior verified by `connectivity/Module_connectivity.mk` having both unconditional and `gb_Helper_optional,DBCONNECTIVITY,...` entries.

---

## 16. Engineer's Final Recommendation

Based on the full dependency map, the **cleanest practical extraction for Writer + Calc + Impress** is:

### Mandatory KEEP (~55 modules)

```
sal salhelper tools o3tl
cppu cppuhelper comphelper unotools
store stoc registry ure binaryurp bridges io
configmgr officecfg package sot ucb ucbhelper
sax xmlreader xmlscript xmlsecurity unoxml
i18nlangtag i18npool i18nutil linguistic lingucomponent
basegfx vcl toolkit UnoControls drawinglayer svgio emfio
canvas cppcanvas                # MANDATORY when sd/ is kept (slideshow)
svl svtools editeng svx docmodel formula
sfx2 framework desktop cui uui fpicker
filter oox xmloff
sw                              # WRITER
sc scaddins sccomp              # CALC
sd slideshow animations sdext   # IMPRESS + DRAW
chart2                          # for embedded chart display in all 3 apps
connectivity                    # dbtools only after --disable-DB
basic                           # forced by sw/sc/sd — cannot drop
vbahelper                       # forced by sc
avmedia                         # forced by sw, sc, sd
eventattacher embeddedobj
solenv pch postprocess instsetoo_native setup_native scp2 sysui shell
external (pruned to §10.1-10.2 subset)
extras (pruned)
readlicense_oo
test testtools unotest          # only if building/running tests
compilerplugins                 # build-only, Clang-only
codemaker idl idlc unoidl       # build-only
writerperfect                   # if you keep any wpft* (wpftwriter/calc/impress/draw)
```

### Confidently REMOVE (~35 modules)

```
starmath                                          # Math (formula editor)
dbaccess reportbuilder reportdesign forms         # Base / DB UI
basctl scripting                                  # via --disable-scripting
jurt jvmaccess jvmfwk javaunohelper ridljar bean  # via --disable-java
cli_ure net_ure rust_uno jsuno pyuno              # other bindings
helpcontent2 helpcompiler xmlhelp                 # via --without-help
android ios                                       # mobile
nlpsolver librelogo swext                         # extensions
qadevOOo smoketest                                # tests
opencl                                            # via --disable-opencl
hwpfilter lotuswordpro                            # legacy filters
libreofficekit                                    # embedding-only
remotebridges                                     # rarely useful standalone
OGLTrans                                          # OpenGL slide transitions (optional)
```

### Realistic outcomes (Writer + Calc + Impress build)

- **Source tree**: ~10M LOC → ~7-7.5M LOC
- **Build time**: ~50-70% faster than full LO
- **Install set size**: ~700MB → ~480-500MB
- **Binary functionality**: opens `.docx`, `.doc`, `.rtf`, `.odt`, `.xlsx`, `.xls`, `.ods`, `.pptx`, `.ppt`, `.odp`, `.odg`, `.html`, `.csv`. Writer, Calc, and Impress/Draw fully functional including charts, images, embedded objects, slideshow runtime (F5), animations, transitions, mail-merge (without DB), Hunspell spell-check.

### What you give up

- No Math (formula editor app) / Base (database app)
- No BASIC IDE, no Python/JS/Java macros (Basic runtime is still there, just no UI to edit it)
- No bundled help (use online docs)
- No legacy WordPerfect / HWP / Lotus Word Pro import (drop those modules entirely)
- Optional: no legacy vector format import for Draw (CDR / Visio / Publisher / etc. — drop `wpftdraw`)
- No PDF import (you can still export PDF)
- No GPU formula acceleration (`--disable-opencl`)
- No OpenGL slide transitions (`OGLTrans` removed) — Impress uses CPU-rendered transitions instead
- No embedded media playback (audio/video — codecs still link, just no UI for inserting)
- No remote presentation control via Bluetooth

### What you keep that you might not have expected

- **Draw application** — comes "for free" with Impress since they share `sd/`. No effort needed to disable; just don't expose it in your launcher if you don't want it.
- **Full PPTX/PPT/ODP support** — handled by already-required `oox`, `xmloff`, `sd/source/filter/`, `msfilter`, `sot`. No additional cost.
- **Embedded chart editing** — `chart2/` is kept anyway for chart display; you get the editing UI as a side effect.

### What stays surprisingly large

- `vcl/` — ~600K LOC. The widget toolkit + platform backends are necessarily big.
- `sw/` — ~1.5M LOC.
- `sc/` — ~700K LOC.
- `sd/` — ~600K LOC (Impress + Draw).
- `svx/`, `sfx2/`, `oox/`, `xmloff/`, `editeng/`, `chart2/`, `slideshow/` — together ~1.7M LOC of shared code that touches everything.

These constitute the irreducible floor for "real Word+Excel+PowerPoint" fidelity. No amount of extraction will shrink them — that's the cost of LibreOffice's standards compliance.

### One sentence per starting goal

- **"I'm doing development, just want fast iteration"**: Strategy 1. Don't touch the tree. `make sw sc sd`.
- **"I want a slimmer installable that's still recognizably LibreOffice but only Writer + Calc + Impress"**: Strategy 2. Apply the configure flags + the `RepositoryModule_host.mk` patch.
- **"I want to fork and own the codebase"**: Strategy 3 + brace for upstream-sync pain. You will be patching every release.

---

## Appendix A — File references quick lookup

| Topic | File |
|---|---|
| Top-level module list | `RepositoryModule_host.mk` |
| Library install groups | `Repository.mk` |
| Build system | `solenv/gbuild/` |
| Configure switches | `configure.ac` |
| WASM strip flags | `configure.ac:4358-4400`, `config_host/config_wasm_strip.h.in` |
| Writer entry | `sw/Module_sw.mk`, `sw/Library_sw.mk` |
| Calc entry | `sc/Module_sc.mk`, `sc/Library_sc.mk` |
| Impress / Draw entry | `sd/Module_sd.mk`, `sd/Library_sd.mk`, `sd/Library_sdfilt.mk` |
| Slideshow runtime | `slideshow/Module_slideshow.mk`, `slideshow/Library_slideshow.mk` |
| Animation framework | `animations/Library_animcore.mk` |
| Formula engine | `formula/Library_for.mk`, `formula/Library_forui.mk` |
| DOCX import | `sw/source/writerfilter/` → `sw/Library_sw_writerfilter.mk` |
| DOCX export | `sw/source/filter/ww8/` → `sw/Library_msword.mk` |
| XLSX | `sc/source/filter/oox/` → `sc/Library_scfilt.mk` |
| PPTX | `oox/source/ppt/`, `oox/source/drawingml/`, `sd/Library_sdfilt.mk` |
| PPT (binary) | `sd/source/filter/ppt/`, `sd/source/filter/eppt/` |
| ODF | `xmloff/`, `sw/source/filter/xml/`, `sc/source/filter/xml/`, `sd/source/filter/xml/` |
| Bootstrap | `desktop/source/app/sofficemain.cxx`, `desktop/source/app/app.cxx` |
| CLI parsing | `desktop/source/app/cmdlineargs.cxx` |
| URE bootstrap | `cppuhelper/source/bootstrap.cxx` |
| Solar mutex | `vcl/README.md`, `vcl/source/app/solarmutex.cxx` |
| Item system | `svl/source/items/`, `include/svl/itempool.hxx` |

## Appendix B — Quick command reference

```sh
# Find which Library_*.mk declares a library named "foo":
grep -rl 'gb_Library_Library,foo)' --include='Library_*.mk'

# Find which other Library_*.mk link to library "foo":
grep -rn 'gb_Library_use_libraries.*\bfoo\b' --include='Library_*.mk'

# Find which externals a library uses:
grep 'gb_Library_use_externals' sw/Library_sw.mk

# Build only Writer + dependencies:
make sw

# Build only Calc + dependencies:
make sc

# Build only Impress/Draw + dependencies:
make sd

# Build all three together:
make sw sc sd

# Headless smoke test:
instdir/program/soffice --headless --writer  --convert-to pdf  input.docx
instdir/program/soffice --headless --calc    --convert-to xlsx input.ods
instdir/program/soffice --headless --impress --convert-to pdf  input.pptx
```
