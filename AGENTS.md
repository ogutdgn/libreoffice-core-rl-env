# AGENTS.md

> Canonical guide for AI coding agents (Claude Code, Cursor, Copilot,
> Codex, etc.) working in this repository. **Read this first.**

---

## 1. What this repository is

This is a **fork of LibreOffice core**, being shaped into a runtime
environment for downstream RL agent experimentation (Computer Use
Agents). The end goal is to keep only **Writer, Calc, and Impress**,
add a structured user-action **logger** for RL training/replay, and
**redesign the UI** to mimic MS Word / Excel / PowerPoint so that
agent skills transfer cleanly between Office and this open stack.

Fork: <https://github.com/ogutdgn/libreoffice-core-rl-env>

### Roadmap at a glance

The work proceeds in tightly ordered stages — finish each before
starting the next:

1. **Strip** — delete unused modules, restructure folders for
   readability. Build verified after every group. (Phase 1, current.)
2. **Logger** — structured event log of every user action (menu,
   toolbar, keyboard) per app, in order: Writer → Calc → Impress.
   Builds on the existing hook points listed in §4 "Existing event /
   logger infrastructure".
3. **UI redesign** — Writer → Word, Calc → Excel, Impress →
   PowerPoint visual / interaction parity. Same order as the logger.
4. **Distribution** — Docker multi-stage image with pre-built
   `instdir/` baked in. RL agents pull the image; they don't rebuild.

### Two ways this fork will be used

1. **Development** (the owner + you, on a single workstation): edit
   source, build, run `instdir/program/soffice` to verify.
2. **Distribution** (later): the Docker image from stage 4 above.

This document is about #1. #2 is downstream.

---

## 2. Branch graph (current state)

```
master                                ←  vanilla LibreOffice (b96243ffd)
dev                                   ←  bootstrap + docs only, vanilla build verified
phase1/<id>-<slug>                    ←  one per Phase 1 group (forked from dev, merged back)
chore/strip-to-writer-calc-impress    ←  earlier attempt (paused, reference only)
refactor/apps-core-folder-split       ←  earlier attempt (paused, reference only)
pre-strip-backup (tag)                ←  rollback safety
```

Active working branch: `dev`. Each Phase 1 step happens on its own
`phase1/...` subbranch and is fast-forward merged into `dev` after
the owner reviews and approves. See §3 "Branch flow for phase work".

The two "paused" branches contain prior work where the strip + folder
restructure was attempted in one big push. That approach was abandoned
because changes were committed without verified builds between them,
making it impossible to bisect failures. Knowledge from those branches
is preserved here:

- [`docs/architecture/WRITER_CALC_EXTRACTION.md`](docs/architecture/WRITER_CALC_EXTRACTION.md)
  — deep analysis of which LibreOffice modules can be removed and why.
  Cherry-picked from the `chore/strip-to-writer-calc-impress` branch.

If you ever need the prior plan documents, they exist on those
branches: `PLAN.md` and `EXECUTION-MAP.md` on
`refactor/apps-core-folder-split`. **Do not copy them here** — they
describe an abandoned approach.

---

## 3. The disciplined workflow we now follow

**Build-driven, not plan-driven.** Each change must be verified by a
build cycle before the next change is made.

```
   ┌─────────────────────────────────────────────────────────┐
   │ 1. Edit source (or apply a delete)                       │
   │ 2. Build (full `make` or `make sw sc sd` — §6 patterns)  │
   │ 3. Smoke test set (headless + GUI launch — §6)           │
   │ 4. If green: commit, push                                │
   │ 5. If red: investigate / rollback, do NOT proceed         │
   │ 6. Go to 1                                               │
   └─────────────────────────────────────────────────────────┘
```

Do **not** stack multiple uncommitted changes hoping the build will
sort it out at the end. Every red light gets resolved before any
new change.

### Why this matters

The first attempt (paused branches) made 22 commits across two
branches before any successful end-to-end build. When build failures
appeared, it was unclear which commit broke what. This approach
inverts that: build first, change second.

### Branch flow for phase work

Each Phase 1 group (and any future phase sub-task) lives on its own
subbranch off `dev`. Convention: `phase<N>/<id>-<short-slug>`.

```
dev
├── phase1/1A-peer-apps
├── phase1/1B-language-bridges
├── phase1/1C-mobile-platform
├── phase1/1D-help
├── phase1/1E-legacy-filters
├── phase1/1F-tests-extensions
└── phase1/1G-opencl
```

Per subbranch: agent creates the branch, applies the delete + build
referenced cleanup, runs full `make` + the smoke test set, pushes.
Owner reviews on GitHub, fast-forward merges into `dev`, then the
next subbranch is forked from the updated `dev`. A subbranch that
fails verification is discarded (`git branch -D`), not patched up
in-place — `dev` stays clean.

---

## 4. The plan, simply stated

| Phase | What | Status |
|---|---|---|
| **0** | Verify vanilla master builds on owner's WSL setup | ✓ done — `942e4161c` |
| **1** | Incremental module deletions (1A–1G, build verified each) | **current** |
| **2** | (Optional) folder restructure into `apps/` + `core/` | after 1 stays clean |
| **3** | Writer: structured user-action logger | future |
| **4** | Writer UI redesign (→ MS Word visual/interaction parity) | future |
| **5** | Calc: logger + UI redesign (→ MS Excel) | future |
| **6** | Impress: logger + UI redesign (→ MS PowerPoint) | future |
| **7** | Docker multi-stage image for distribution | future |

We are at the start of Phase 1. Phase 0 is green: vanilla build
produces `instdir/program/soffice` and headless conversion roundtrips
work. Each Phase 1 subbranch must stay green by the same standard
before being merged into `dev`.

### Phase 1 — modules to delete (in suggested order)

From the analysis in
[`docs/architecture/WRITER_CALC_EXTRACTION.md`](docs/architecture/WRITER_CALC_EXTRACTION.md):

| Group | Modules | Risk | Notes |
|---|---|---|---|
| **1A** Peer apps | `starmath`, `basctl`, `dbaccess`, `reportbuilder`, `reportdesign`, `forms`, `sdext`, `swext` | Low | Other office apps + DB UI |
| **1B** Language bridges | `jurt`, `jvmaccess`, `jvmfwk`, `javaunohelper`, `ridljar`, `bean`, `cli_ure`, `net_ure`, `rust_uno`, `jsuno`, `pyuno`, `scripting` | Low | Non-C++ UNO bindings |
| **1C** Mobile / platform | `android`, `ios`, `osx`, `apple_remote`, `winaccessibility` | Low | Target is Linux only |
| **1D** Help system | `helpcompiler`, `xmlhelp` | Low | `--without-help` covers most |
| **1E** Legacy filters | `hwpfilter`, `lotuswordpro` | Low | Korean HWP, Lotus formats |
| **1F** Tests + extensions | `qadevOOo`, `smoketest`, `nlpsolver`, `librelogo`, `remotebridges` | Low | Old QA + extension demos. `libreofficekit` and `uitest` **deliberately preserved** — see "Preserved for Phase 2" below. |
| **1G** OpenCL | `opencl` | Low | Calc GPU acceleration |

Each group = one commit (or a small handful), with full `make`
+ smoke test in between. Module-level builds (`make sw sc sd`)
are NOT sufficient here because they only verify the sw/sc/sd
subtree — cross-module breakage in e.g. `cui`, `framework`, `oox`
caused by a deletion would be missed. Full `make` after the
initial Phase 0 build is fast (5-15 min incremental) because
externals and most LinkTarget artifacts are preserved.

### Do NOT delete (verified mandatory)

`basic`, `vbahelper`, `chart2`, `connectivity` (for `dbtools`),
`avmedia`, `slideshow`, `animations`, `canvas`, `cppcanvas`.

These are linked by `sw`/`sc`/`sd` directly. Removing them requires
patching app source code. Out of scope.

### Preserved for Phase 2 (RL logger / action recording)

The end goal of this fork is an RL agent runtime. Phase 2 will
add structured logging of user actions and a UI redesign to
mimic MS Office. Two modules originally listed under 1F are
preserved because the next phase may build on them:

- `libreofficekit` (LOK) — C/C++ embedding API with rendering
  and document-state callbacks. Used by Online and Mobile
  clients. Plausible foundation for an RL agent's observation
  channel (current doc state, render output).
- `uitest` — Python-driven UI automation framework. Can locate
  widgets by name and drive clicks/typing. Plausible foundation
  for an RL agent's action interface.

Both will be re-evaluated when Phase 2 begins. Until then, do
not delete and do not refactor them out.

### Existing event / logger infrastructure (reference for Phase 2)

The hook points already in LibreOffice that Phase 2 logging
work may build on:

| Mechanism | Location | Role |
|---|---|---|
| `SfxDispatcher` / `SfxRequest` | `sfx2/source/control/dispatch.cxx` | Every menu/toolbar action flows through here as a slot dispatch. Primary hook candidate. |
| `XDispatchProvider` / `XStatusListener` | `framework/source/dispatch/` | UNO command dispatch layer with interceptor pattern; stable API for third-party listeners. |
| Macro recorder (`DispatchRecorder`) | `framework/source/services/dispatchrecorder.cxx` | Already records user actions as BASIC code. Pipeline can be repurposed for a structured (JSON) event log. |
| VCL event listeners | `vcl/source/window/` | Lower-level: keystrokes, mouse, focus. Use only if `SfxDispatcher` granularity is insufficient. |
| `SAL_INFO` / `SAL_WARN` | `sal/log.hxx` | Compile-time debug logging only. **Not** suitable for runtime user-action log. |

---

## 5. Environment

### Owner's setup

- Host OS: Windows 11
- Dev env: WSL2 + Ubuntu 24.04
- IDE: Cursor / VS Code (Windows-side, sees `/mnt/c/.../libreoffice-core`)
- Build location: **WSL native filesystem** (`/home/<user>/...`), NOT `/mnt/c`
  — NTFS-through-9P is ~10x slower than ext4 for many-small-files
  workloads
- Source-of-truth: GitHub remote (`origin`); owner pulls in WSL,
  Claude pushes from Windows

### Build dependencies installed in WSL Ubuntu

```sh
sudo apt install -y \
    build-essential autoconf automake bison flex libtool pkg-config \
    perl nasm python3 python3-dev zlib1g-dev \
    libfontconfig-dev libfreetype-dev libxslt1-dev libxml2-dev \
    libxt-dev libxrandr-dev libxinerama-dev libssl-dev \
    libcairo2-dev libcups2-dev libgtk-3-dev \
    gperf libkrb5-dev libnss3-dev zip unzip \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    meson ninja-build
```

NOPASSWD apt sudoers is also set up in WSL so Claude can install
additional packages as new build deps are discovered:

```sh
echo "$(whoami) ALL=(ALL) NOPASSWD: /usr/bin/apt, /usr/bin/apt-get" \
  | sudo tee /etc/sudoers.d/claude-apt
sudo chmod 0440 /etc/sudoers.d/claude-apt
```

---

## 6. Build commands

### First-time setup (vanilla / Phase 0)

```sh
cd ~
git clone https://github.com/ogutdgn/libreoffice-core-rl-env.git lo-dev
cd lo-dev
git checkout dev

# Clean PATH first or configure may mis-detect this as a
# Windows-as-helper build (see §10 pitfall #4).
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# autogen.sh forwards all flags to configure and persists them in
# `autogen.lastrun`. Do NOT call `./configure` separately afterwards —
# autogen.sh already runs configure for you. Calling autogen.sh with
# no args will run configure with no flags and fail on the Java check.
./autogen.sh \
    --without-java \
    --without-help \
    --disable-libcmis \
    --disable-firebird-sdbc \
    --disable-postgresql-sdbc \
    --disable-mariadb-sdbc \
    --disable-online-update \
    --disable-extension-update \
    --disable-pdfimport

# First-time build: `make` (no args) runs the full bootstrap → fetch →
# build-tools → all-modules chain. `make sw sc sd` SKIPS that chain and
# fails on missing prereqs like `Executable/concat-deps` and
# `oox/generated/misc/namespaces.txt`. Use module-level builds only AFTER
# this initial full make has succeeded — see §6 "Iteration after Phase 0".
make 2>&1 | tee build.log
```

Verified configure-flag names (some upstream renames caught us during
prior attempts):

| Don't use | Use |
|---|---|
| `--disable-java` | `--without-java` |
| `--disable-cmis` | `--disable-libcmis` |
| `--disable-nlpsolver` | (no flag — `--enable-ext-nlpsolver` is opt-in, leave it off) |
| `--disable-help` | `--without-help` |

If the build hits `OGLTrans` link errors, add `--disable-opengl`.
If the build hits `helplinker` errors when modules referencing
XMLHELP are involved, add `--disable-xmlhelp`.

### Iteration patterns after Phase 0

Two patterns depending on what changed:

**Pattern A — edits scoped to sw/sc/sd source** (UI tweaks, app-side
changes, slot handlers): `make sw sc sd` is sufficient because the
edit cannot affect build artifacts outside that subtree.

```sh
git pull origin dev
make sw sc sd                # 5-30 min depending on scope
instdir/program/soffice --writer    # GUI smoke (or use headless set below)
```

**Pattern B — phase work (deletions, build-system / cross-module
changes)**: use full `make` because breakage can surface anywhere
in the tree, not just sw/sc/sd.

```sh
git checkout -b phase1/1X-... dev
# delete modules + clean Repository.mk / RepositoryModule_host.mk / etc.
make 2>&1 | tee build.log     # 5-15 min incremental
# smoke test set (below)
git push -u origin phase1/1X-...
# owner reviews + merges into dev
```

### Smoke test set (used between phase groups)

Verifies that the binary is still launchable AND that the filter
stack still functions. Both layers needed — opening confirms
runtime init, conversion confirms filters didn't break.

```sh
# Headless filter check — each app's converter
echo "smoke" > /tmp/t.txt
instdir/program/soffice --headless --convert-to pdf /tmp/t.txt --outdir /tmp
file /tmp/t.pdf   # expect: PDF document, version 1.x

# Real-format roundtrip — DOCX/XLSX/PPTX import → PDF
# (use a small fixture file once we have one in the repo)
# instdir/program/soffice --headless --convert-to pdf fixture.docx --outdir /tmp

# GUI launch — owner-only, needs WSLg/X11
instdir/program/soffice --writer    # blank doc opens, close
instdir/program/soffice --calc
instdir/program/soffice --impress
```

Opening without crash = process init + VCL/cairo/SFX2 alive.
Headless conversion success = filter pipeline alive. Together
they cover module-deletion verification. Full CppUnit / UITest
runs are deferred to Phase 2 logger work.

### When `make` insists on re-running autogen

Sometimes after a `git pull` Make sees timestamp changes and wants
to re-run autogen.sh + configure. To skip:

```sh
touch config_host.mk Makefile.gbuild
make sw sc sd
```

### Module-level operations

```sh
make sw.clean        # clean only sw's artifacts
make sw.check        # run sw unit tests
make sw.subsequentcheck    # integration tests
make sc.clean / sc.check / etc.
```

---

## 7. Code conventions (high level)

LibreOffice has 30+ years of accumulated conventions. The most
important ones:

| Topic | Rule |
|---|---|
| C++ standard | C++17. Compiler baseline: GCC 13 / Clang 18 / MSVC 2022. |
| `#include` | `"..."` only for same-dir files; otherwise `<...>`. |
| Strings | Internal: `rtl::OUString` (UTF-16). Never `std::string` for UNO-exposed text. |
| Numbers | `sal_Int32`, `sal_uInt32`, `sal_Bool` for UNO interfaces. |
| Naming | Members: `mFoo`, pointers: `pFoo`, locals: `aFoo`, UNO refs: `xFoo`. Classes: `PascalCase`. |
| Headers | Public: `include/<module>/`. Internal: `<module>/inc/`. |
| `SolarMutex` | Acquire with `SolarMutexGuard` in UNO entry points touching VCL. Never call out (event listeners) while held. |
| UNO components | Register in `<module>/util/<lib>.component`. |
| SDI slots | `<module>/sdi/*.sdi`, compiled by `svidl` to C++ headers. |
| Comments | Default: none. Only when WHY is non-obvious. |

### Owner's modification scope (typical work)

For UI changes (e.g., making Writer look like MS Word):

- **Yes**: `sw/uiconfig/swriter/menubar/`, `sw/uiconfig/swriter/toolbar/`,
  `sw/source/uibase/`, `sw/sdi/*.sdi`
- **Sometimes**: `sw/source/ui/`, `sfx2/source/notebookbar/`,
  `cui/source/dialogs/`, `framework/source/uifactory/`
- **Rare**: `vcl/source/control/` (custom widgets)
- **Never**: `external/`, `bridges/source/*_uno/`

Most UI XML changes need **no rebuild** — restart `soffice` and see.

---

## 8. Conventional Commits

All commits use [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <subject>
```

Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `test`, `perf`, `style`.

Scopes: `sw`, `sc`, `sd`, `vcl`, `sfx2`, `framework`, `build`, `docs`, etc.

### Examples

```
feat(sw): add custom toolbar slot for RL telemetry
fix(sc): correct cell selection event when scroll-wrapping
chore(strip): remove peer apps (Math, Base, BASIC IDE)
docs(architecture): document apps vs core split
```

### Forbidden

- **NEVER** add `Co-Authored-By: Claude …` or any AI-attribution footer.
- **NEVER** sign commits as someone other than the owner.
- **NEVER** force-push to `master`.
- **NEVER** skip the pre-commit hook (`--no-verify`) without first
  trying to fix the underlying issue. (Prior attempts had a Windows
  MSYS-specific symlink resolution issue with the LibreOffice
  pre-commit hook; if that recurs, replace the symlink in
  `.git/hooks/pre-commit` with a real copy of `.git-hooks/pre-commit`.)

---

## 9. Where to find things

| You want… | Go to… |
|---|---|
| Writer code | [`sw/`](sw/) — `sw/source/core/` for model, `sw/source/uibase/` for UI |
| Calc code | [`sc/`](sc/) — `sc/source/core/` for model, `sc/source/ui/` for UI |
| Impress/Draw code | [`sd/`](sd/) — `sd/source/core/`, `sd/source/ui/` |
| Common UI dialogs | [`cui/`](cui/) (Tools→Options, Find&Replace, etc.) |
| Office shell framework | [`sfx2/`](sfx2/), [`framework/`](framework/) |
| UI toolkit (widgets, fonts, rendering) | [`vcl/`](vcl/) |
| DOCX import | `sw/source/writerfilter/` |
| DOCX export | `sw/source/filter/ww8/` |
| XLSX import/export | `sc/source/filter/oox/` |
| PPTX shared bits | `oox/source/ppt/`, `sd/source/filter/` |
| App entry point | `desktop/source/app/sofficemain.cxx` |
| Command-line args | `desktop/source/app/cmdlineargs.cxx` |
| Config schemas | `officecfg/registry/` |
| Public headers | `include/<module>/` |
| Strip analysis (what to delete and why) | [`docs/architecture/WRITER_CALC_EXTRACTION.md`](docs/architecture/WRITER_CALC_EXTRACTION.md) |

---

## 10. Pitfalls and gotchas

1. **NTFS slowness via WSL**: build from `/home/$USER/`, not `/mnt/c/`.
2. **Stale workdir after big changes**: if cross-module changes
   produce strange "missing library" errors, `rm -rf workdir/{LinkTarget,CxxObject,Dep,CObject}` and rebuild. Externals (`workdir/UnpackedTarball/`) survive.
3. **Git hooks fail to spawn** on Windows MSYS git. The repo ships
   `.git/hooks/{pre-commit,commit-msg,post-merge}` as symlinks
   pointing to absolute Windows paths under `.git-hooks/`. MSYS git
   cannot follow them and aborts every commit. Workaround — for
   each affected hook, replace the symlink with a real copy:
   ```sh
   for h in pre-commit commit-msg post-merge; do
       rm ".git/hooks/$h"
       cp ".git-hooks/$h" ".git/hooks/$h"
       chmod +x ".git/hooks/$h"
   done
   ```
   This is per-clone (the `.git/` dir is not tracked), so each
   fresh clone repeats the fix once.
4. **Configure may detect WSL as "Windows-as-helper" build** if
   `$WSL_DISTRO_NAME` is set AND PATH contains `mingw64` (from git-bash
   forwarding). Workaround: clean PATH inside WSL bash:
   ```sh
   export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
   ```
5. **`SolarMutex`** (the LibreOffice big-kernel-lock): see `vcl/`
   README on lifecycle. Cross-module UNO calls re-enter VCL.
6. **`SwDoc` / `ScDocument` / `SdDrawDocument`** are not thread-safe.
7. **`SfxItemSet` is value-typed but `SfxItemPool` is shared.** Pool
   ownership matters; don't move `ItemSet`s between docs without care.
8. **Build system is gbuild, not CMake.** New source files need
   manual addition to `<module>/Library_<libname>.mk`, not
   auto-discovery.

---

## 11. Updating this file

When you learn something a future agent should know (a new
convention, a new gotcha, a removed/added module), update this file
and commit:

```
docs(agents): note new gotcha about <X>
```

Do not leave knowledge in chat memory only.
