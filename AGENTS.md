# AGENTS.md

> Canonical guide for AI coding agents (Claude Code, Cursor, Copilot,
> Codex, etc.) working in this repository. **Read this first.**

---

## 1. What this repository is

This is a **fork of LibreOffice core**, being shaped into a runtime
environment for downstream RL agent experimentation (Computer Use
Agents). The end goal is to **keep only Writer, Calc, and Impress**
and to be able to modify their UI (especially Writer) freely.

Fork: <https://github.com/ogutdgn/libreoffice-core-rl-env>

### Two ways this fork will be used

1. **Development** (the owner + you, on a single workstation): edit
   source, `make sw sc sd`, run `instdir/program/soffice` to verify.
2. **Distribution** (later): Docker multi-stage image with pre-built
   `instdir/` baked in. RL agents pull the image; they don't rebuild.

This document is about #1. #2 is downstream.

---

## 2. Branch graph (current state)

```
master                                ←  vanilla LibreOffice (b96243ffd)
dev                                   ←  YOU ARE WORKING HERE (currently identical to master)
chore/strip-to-writer-calc-impress    ←  earlier attempt (paused, reference only)
refactor/apps-core-folder-split       ←  earlier attempt (paused, reference only)
pre-strip-backup (tag)                ←  rollback safety
```

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
   │ 2. make sw sc sd       — incremental build (5-30 min)    │
   │ 3. instdir/program/soffice --writer  — smoke test         │
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

---

## 4. The plan, simply stated

| Phase | What | Duration | Who |
|---|---|---|---|
| **0** | Verify vanilla master builds on the owner's WSL setup | ~45-90 min | owner |
| **1** | Incremental delete: ~6-8 groups of related modules, build between each | ~half day | both |
| **2** | (Optional) folder restructure into `apps/` + `core/` | ~half day | both, only if Phase 1 stays clean |
| **3** | Docker multi-stage build setup for distribution | ~few hours | both |

We are between Phase 0 and Phase 1 right now. Phase 0 must be
"green" before Phase 1 begins.

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
| **1F** Tests + extensions | `qadevOOo`, `smoketest`, `nlpsolver`, `librelogo`, `libreofficekit`, `remotebridges`, `uitest` | Low | Old QA + extension demos |
| **1G** OpenCL | `opencl` | Low | Calc GPU acceleration |

Each group = one commit (or a small handful), with `make sw sc sd`
+ smoke test in between.

### Do NOT delete (verified mandatory)

`basic`, `vbahelper`, `chart2`, `connectivity` (for `dbtools`),
`avmedia`, `slideshow`, `animations`, `canvas`, `cppcanvas`.

These are linked by `sw`/`sc`/`sd` directly. Removing them requires
patching app source code. Out of scope.

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

./autogen.sh

./configure \
    --without-java \
    --without-help \
    --disable-libcmis \
    --disable-firebird-sdbc \
    --disable-postgresql-sdbc \
    --disable-mariadb-sdbc \
    --disable-online-update \
    --disable-extension-update \
    --disable-pdfimport

make sw sc sd 2>&1 | tee build.log
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

### Iteration after Phase 0

```sh
git pull origin dev          # get latest changes
make sw sc sd                # incremental build (~5-30 min depending on scope)
instdir/program/soffice --writer    # smoke test
instdir/program/soffice --calc
instdir/program/soffice --impress
```

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
3. **Pre-commit hook fails to spawn** on Windows MSYS git: replace
   symlinks in `.git/hooks/` with real copies of files in `.git-hooks/`.
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
