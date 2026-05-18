# Phase 3 — Writer Logger Design (V1)

> Status: approved 2026-05-18. Implementation tracked on `phase3/writer-logger`.
> Reference: `cua-bench/apps/figma/mock/src/logger/` (raw/semantic/outcome 3-tier pattern).

## 1. Purpose

Capture a structured, queryable event log of every user action inside
LibreOffice Writer so that downstream RL agents can:

- Train on (state, action) pairs derived from real user sessions
- Replay sessions deterministically to reproduce bugs or evaluate runs
- Score "did the agent reach the target document state?" rubrics

The logger ships with the binary and is opt-in via an environment
variable. When disabled, the module loads but installs no hooks — zero
runtime overhead.

## 2. Three-tier event model

Borrowed verbatim from the cua-bench Figma mock logger. Each tier
answers a different question:

| Tier | Question | Source |
|---|---|---|
| **Raw** | "What just happened at the input layer?" | VCL window events |
| **Semantic** | "What did the user *mean* by that?" | SfxDispatcher command dispatches |
| **Outcome** | "What does the document look like right now?" | LibreOfficeKit (LOK) state queries |

### 2.1 Raw

Captured via `Application::AddEventListener` (VCL global). One handler
sees every key press, mouse button event, focus change, and VCL
gesture/command event.

Filtered to:

- `VclEventId::WindowKeyInput`, `WindowKeyUp`
- `VclEventId::WindowMouseButtonDown`, `WindowMouseButtonUp`
- `VclEventId::WindowMouseMove` (rate-limited to ~60 Hz)
- `VclEventId::WindowGetFocus`, `WindowLoseFocus`
- `VclEventId::WindowCommand` (wheel, swipe, gesture)

Skipped: paint, layout, internal mouse enter/leave, window move.

Event shape:

```jsonc
{
  "eventId": "raw-0001",
  "type": "key.down",
  "timestamp": 1721327891234,    // epoch ms
  "sessionTime": 12345,           // ms since session start
  "target": {
    "windowName": "SwViewWin",
    "widgetType": "ScrollWindow"
  },
  "fields": {
    "keyCode": 66,
    "char": "B",
    "physicalCode": "KeyB"
  },
  "modifiers": {
    "shift": false, "ctrl": true, "alt": false, "meta": false
  }
}
```

Identification of the target widget uses `Window::GetText()` and the
runtime class name. A later iteration may adopt `Window::set_id()` to
give RL-agent-friendly stable IDs (the analogue of `data-id` in the
Figma mock).

### 2.2 Semantic

Captured via a custom `XDispatchProvider` interceptor plus an
always-on `XDispatchRecorder` implementation. The dispatch recorder
pipeline (already in `framework/source/recording/`) calls
`recordDispatch(URL, args)` for every `.uno:*` command issued by a
menu click, toolbar button, keyboard shortcut, or programmatic
dispatch. We install a parallel recorder that runs unconditionally,
independent of the existing user-facing macro recorder.

No source patch in `sfx2/` or `framework/` is required. The hook is
purely additive via UNO service registration.

V1 command map (initial set, expandable):

| Category | UNO command(s) | Semantic event name |
|---|---|---|
| File | `.uno:Save`, `.uno:SaveAs`, `.uno:CloseDoc`, `.uno:Open` | `file_save`, `file_save_as`, `file_close`, `file_open` |
| Edit | `.uno:Cut`, `.uno:Copy`, `.uno:Paste`, `.uno:Undo`, `.uno:Redo` | `edit_cut`, `edit_copy`, `edit_paste`, `edit_undo`, `edit_redo` |
| Text format | `.uno:Bold`, `.uno:Italic`, `.uno:Underline`, `.uno:Strikeout` | `format_bold`, `format_italic`, `format_underline`, `format_strikeout` |
| Paragraph | `.uno:LeftPara`, `.uno:CenterPara`, `.uno:RightPara`, `.uno:JustifyPara` | `paragraph_align_<dir>` |
| Font | `.uno:CharFontName`, `.uno:FontHeight` | `format_font_change`, `format_size_change` |
| Insert | `.uno:InsertGraphic`, `.uno:InsertObject`, `.uno:InsertTable` | `insert_image`, `insert_object`, `insert_table` |
| Navigation | `.uno:GoUp`, `.uno:GoDown`, cursor moves | `cursor_move` |
| Selection | `.uno:SelectAll` | `select_all` |

Event shape:

```jsonc
{
  "schemaVersion": 1,
  "sessionId": "20260518-120134-pid12345",
  "eventId": "sem-0042",
  "timestamp": 1721327891234,
  "documentUrl": "file:///home/user/draft.odt",
  "rawEventIdRange": ["raw-0099", "raw-0103"],
  "name": "format_bold",
  "args": { "state": true },
  "trigger": "shortcut"
}
```

**Trigger detection.** A semantic event carries `trigger ∈
{shortcut, toolbar, menu, context_menu, dialog, programmatic}`.
Detection is heuristic, based on the most recent raw event(s) and
`SfxRequest::GetCallMode()`:

- Last raw event was `key.down` with a modifier → `shortcut`
- Last raw event was `mouse.click` on a window classed as toolbar/menubar → `toolbar`/`menu`
- Last raw event was `mouse.click` shortly after a `contextmenu` raw event → `context_menu`
- Came in via UNO/script with no preceding raw event → `programmatic`

Expected ≥85% accuracy on the V1 command set; mislabeled triggers do
not corrupt the log, just the trigger field.

**rawEventIdRange.** The range of raw event IDs whose handling
produced this semantic event. The first ID is the raw event that
started the current gesture; the last is the most recent raw event at
emit time. Multiple semantic events emitted from the same gesture
(e.g. selection change + style change from a single click) share the
same range, mirroring the cua-bench `prevGestureBoundary` mechanism.

### 2.3 Outcome

Built on demand each flush tick via LibreOfficeKit (LOK) state
queries plus direct `SwView` access. No history is kept; each flush
overwrites the file.

Event shape:

```jsonc
{
  "schemaVersion": 1,
  "sessionId": "20260518-120134-pid12345",
  "capturedAt": 1721327891234,
  "document": {
    "url": "file:///home/user/draft.odt",
    "modified": true,
    "currentApp": "Writer"
  },
  "cursor": { "pageNum": 3, "paragraphIdx": 12, "charOffset": 42 },
  "selection": {
    "hasSelection": true,
    "text": "selected text...",
    "range": [142, 162]
  },
  "format": {
    "font": "Liberation Serif",
    "size": 12,
    "bold": true, "italic": false
  },
  "counts": { "paragraphs": 47, "words": 2103, "characters": 11852 },
  "summary": { "semanticEventCount": 142, "rawEventCount": 8741 }
}
```

LOK is preserved in this fork specifically because Phase 2 (logger)
was forecast to build on it. See `AGENTS.md` §4 "Preserved for Phase 2".

## 3. Module structure

```
rllogger/
├── Library_rllogger.mk
├── Module_rllogger.mk
├── Makefile
├── README.md
├── inc/
│   ├── rllogger/
│   │   └── rllogger.hxx          (public: env-var probe + install entry)
│   ├── EventTypes.hxx            (raw/semantic/outcome struct definitions)
│   ├── RawCapture.hxx            (VCL listener installer)
│   ├── SemanticEmitter.hxx       (dispatch interceptor)
│   ├── OutcomeSnapshot.hxx       (LOK-based snapshot builder)
│   ├── Buffer.hxx                (lock-protected deques)
│   ├── Persist.hxx               (background writer thread)
│   ├── Session.hxx               (lifecycle: ID, dirs, start/end events)
│   └── CommandMap.hxx            (.uno:* → semantic name table)
├── source/
│   ├── rllogger.cxx              (entry point — env probe, hook install)
│   ├── EventTypes.cxx
│   ├── RawCapture.cxx
│   ├── SemanticEmitter.cxx
│   ├── OutcomeSnapshot.cxx
│   ├── Buffer.cxx
│   ├── Persist.cxx
│   ├── Session.cxx
│   └── CommandMap.cxx
└── util/
    └── rllogger.component         (UNO service registration)
```

## 4. Activation and persistence

**Activation.** A single environment variable, `LO_RL_LOG_DIR`,
toggles the logger. If unset, the module loads but the entry
function returns early and installs no hooks. If set, a session
directory is created under that path.

**Session directory layout.**

```
$LO_RL_LOG_DIR/
└── 20260518-120134-pid12345/
    ├── raw.jsonl
    ├── semantic.jsonl
    └── outcome.jsonl
```

Session ID: `${YYYY-MM-DD}-${HH-MM-SS}-pid${PID}`. Sortable, unique
per process, debuggable.

**Persistence format.** JSON Lines (one event per line). Append-only.
Crash-resilient — a partial trailing line can be discarded by the
reader without losing earlier events.

**Flush.** Producer threads push to a lock-protected `std::deque`.
A dedicated writer thread drains the queue every 250 ms (matching the
cua-bench cadence) and on shutdown. The main thread never performs
file I/O.

## 5. Threading

Three threads relevant to the logger:

1. **Main (UI) thread.** Holds `SolarMutex`. Raw events captured here
   via VCL listener; semantic events emitted here via the dispatch
   interceptor. Both producers push to a queue under a short scoped
   lock (~μs) and return immediately. Outcome snapshots are built on
   the main thread under SolarMutex (LOK queries require it) every
   250 ms, then queued.
2. **Background writer thread.** Owned by the logger; drains the
   queue, writes JSONL lines. Never touches VCL, SfxDispatcher, or
   document state — only `std::ofstream` and string formatting.
3. **At-exit.** A `std::atexit` handler signals the writer to flush
   and join. A final `session_end` event with closing outcome
   snapshot is written.

## 6. Schema versioning

Each event carries `"schemaVersion": 1`. Policy (verbatim from
cua-bench):

- Adding a new event name (semantic) → no version bump
- Adding a new field with a sensible default → no version bump
- Changing the type or removing a field of an existing event →
  bump major

## 7. Implementation order

Each step is a single git commit on `phase3/writer-logger`, build-
verified and smoke-tested in WSL before the next step starts. Mirrors
the discipline established in Phase 1.

| # | Commit subject | Verification |
|---|---|---|
| 1 | `feat(rllogger): scaffold empty module` | `librllogger.so` appears in `instdir/program/` |
| 2 | `feat(rllogger): env-var activation + session ID` | `LO_RL_LOG_DIR=/tmp/x soffice` creates `/tmp/x/<sessionId>/` |
| 3 | `feat(rllogger): raw event capture (VCL listener)` | Typing `hello` produces 5 `key.down` lines in `raw.jsonl` |
| 4 | `feat(rllogger): background writer thread + buffer` | Burst typing (~100 chars/s) loses no events |
| 5 | `feat(rllogger): semantic dispatch interceptor (skeleton)` | `.uno:Bold` produces a `semantic.jsonl` line |
| 6 | `feat(rllogger): semantic command map (Writer V1 set) + trigger heuristic` | Ctrl+B → `name: "format_bold", trigger: "shortcut"`; toolbar Bold → `trigger: "toolbar"` |
| 7 | `feat(rllogger): outcome snapshot via LOK` | After typing 5 words, `outcome.jsonl` shows `counts.words = 5` |
| 8 | `feat(rllogger): rawEventIdRange linking + gesture batching` | Ctrl+B → semantic event's `rawEventIdRange` covers the `[key.down ctrl, key.down b, key.up b, key.up ctrl]` window |
| 9 | `feat(rllogger): session_start / session_end events + final outcome flush` | Each session's logs bracket with start and end events |
| 10 | `docs(agents): logger architecture + V1 usage` | AGENTS.md updated; this design doc cross-referenced |

## 8. Risk register

| Risk | Mitigation |
|---|---|
| File I/O on the UI thread blocks Writer | Dedicated writer thread; main thread only pushes to a deque |
| Writer thread races with VCL teardown | At-exit signals writer to flush + join before VCL shuts down |
| `SolarMutex` deadlock | Writer thread never calls into VCL/UNO; producers hold the deque mutex for microseconds |
| `LO_RL_LOG_DIR` points to a full disk | Writes are best-effort; failures logged once to stderr, then dropped silently |
| Sensitive data (passwords, document content) | V1 logs every keystroke and every doc string. Owner-only env-var activation makes this opt-in. A V2 follow-up may add `Window::IsPassword()` redaction. |
| Log size in long sessions | Typical session ≈ 1–10 MB. Log rotation is a V2 concern. |

## 9. Out of scope for V1

Calc and Impress get their own phases (5, 6) with the same recipe.
Until then the logger is a no-op for non-Writer document types
(semantic interceptor still records dispatches, but the command map
is Writer-focused and outcome snapshots may report sparse data).

Other deferred work:

- Replay tool (read log back, drive Writer headlessly)
- Mouse-move capture for drag analysis
- UNO API surface so external controllers can start/stop the logger
- JSON schema generation + validation tooling
- Background log compression (gzip on session end)
- VCL widget tagging via `Window::set_id()` for stable element IDs
- Password-field auto-redaction

## 10. Cross-references

- `cua-bench/apps/figma/mock/src/logger/` — reference implementation (TypeScript / browser)
- `AGENTS.md` §4 "Existing event / logger infrastructure" — survey of LO hook points
- `framework/source/recording/dispatchrecordersupplier.cxx` — existing macro-recorder pipeline that this design parallels
- `framework/inc/recording/dispatchrecorder.hxx` — the `XDispatchRecorder` interface we implement
- LibreOfficeKit (`libreofficekit/`) — preserved in Phase 1F for use here
