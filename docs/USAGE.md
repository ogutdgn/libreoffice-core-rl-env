# Usage

Day-to-day commands for running the built fork and inspecting the
log output. For full build setup, branch flow, and conventions see
[`AGENTS.md`](../AGENTS.md). For the roadmap see
[`docs/architecture/ROADMAP.md`](architecture/ROADMAP.md).

---

## Running soffice

After a successful build (`make` in the WSL workspace), launch from
`instdir/program/`:

```sh
cd ~/lo-dev
instdir/program/soffice --writer            # open Writer
instdir/program/soffice --calc              # open Calc
instdir/program/soffice --impress           # open Impress
instdir/program/soffice --writer --norestore  # skip recovery dialog
```

The logger runs automatically — no env var needed.

---

## Where logs go

Each soffice run creates a session directory under a platform default:

| OS | Default base |
|---|---|
| Linux / macOS | `$HOME/.lo-rl-logs/` |
| Windows | `%LOCALAPPDATA%\lo-rl-logs\` (or `%USERPROFILE%\.lo-rl-logs\`) |
| fallback | `<system temp>/lo-rl-logs/` |

Session directory name format: `YYYY-MM-DD-HHMMSS-pid<PID>`. The
most recent 50 directories are kept; older ones are pruned at startup.

Inside one session:

```
~/.lo-rl-logs/2026-05-18-180510-pid920771/
├── raw.jsonl       # VCL events: keys, mouse, focus, etc. (append-only)
├── semantic.jsonl  # .uno:* dispatches with args, trigger, range (append-only)
└── outcome.jsonl   # Current doc state, overwritten every 250 ms
```

---

## Inspecting logs

Find the most recent session:

```sh
SESSION=$(ls -t ~/.lo-rl-logs | head -1)
echo "$SESSION"
cd ~/.lo-rl-logs/$SESSION
```

Quick event distribution:

```sh
grep -oP '"type":"[^"]+' raw.jsonl | sort | uniq -c | sort -rn
```

Live tail while soffice is still open:

```sh
tail -f semantic.jsonl
```

Pretty-print one outcome snapshot:

```sh
jq . outcome.jsonl
```

Filter semantic events by trigger (e.g. only keyboard shortcuts):

```sh
jq 'select(.trigger=="shortcut")' semantic.jsonl
```

Filter by command:

```sh
jq 'select(.name=="format_bold")' semantic.jsonl
```

---

## Consolidating to one JSON

For RL training / replay pipelines that expect one document per
session:

```sh
SESSION=$(ls -t ~/.lo-rl-logs | head -1)
rllogger/util/rllogger-export.py ~/.lo-rl-logs/$SESSION -o session.json
```

Output shape (matches cua-bench's `exportLog()`):

```json
{
  "schemaVersion": 1,
  "sessionId":     "2026-05-18-180510-pid920771",
  "exportedAt":    1779148999311,
  "raw":           [<every raw event>],
  "semantic":      [<every semantic event>],
  "outcome":       <last outcome snapshot, or null>
}
```

Tolerates a torn trailing line in raw / semantic (possible after a
hard kill of soffice) — earlier lines are unaffected.

---

## Common workflows

### Smoke test the logger in a clean directory

```sh
cd ~/lo-dev && rm -rf /tmp/rl-test
LO_RL_LOG_DIR=/tmp/rl-test instdir/program/soffice --writer --norestore
# do something in Writer, close it
SESSION=$(ls -t /tmp/rl-test | head -1)
cat /tmp/rl-test/$SESSION/semantic.jsonl
```

### Disable the logger entirely

```sh
LO_RL_LOG_DISABLE=1 instdir/program/soffice --writer
```

Zero-overhead path: no session dir, no hooks.

### Redirect logs elsewhere

```sh
LO_RL_LOG_DIR=/path/to/training-data instdir/program/soffice --writer
```

Useful for CI runs that want logs in a known location.

### Headless smoke (CI-friendly)

```sh
LO_RL_LOG_DIR=/tmp/rl-test \
  timeout 15 instdir/program/soffice --headless --terminate_after_init --norestore
# Should exit 0 with session_start + session_end in semantic.jsonl.
```

---

## Accessing logs from Windows

WSL files live at `\\wsl.localhost\Ubuntu\home\<user>\.lo-rl-logs\`
and are directly browsable from Windows Explorer or VS Code:

```powershell
code \\wsl.localhost\Ubuntu\home\ogutd\.lo-rl-logs
```

No NTFS-through-9P slowdown for reads from Windows; writes still
happen on WSL ext4.

---

## Rebuilding after a logger change

The logger lives in `rllogger/` plus a 5-line wiring patch in
`desktop/source/app/sofficemain.cxx`. Incremental builds:

```sh
make rllogger          # rebuild rllogger lib only
make rllogger desktop  # also relink libsofficeapp.so
```

`make sw sc sd` does NOT pick up logger changes — `sofficemain` is
in `desktop`, not the app modules.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `instdir/program/soffice` not found | Build didn't finish or you're outside `~/lo-dev` |
| Session dir created but logs empty | Logger crashed during `initialize` — check stderr for `rllogger:` lines |
| `semantic.jsonl` has events but `trigger: "menu"` for shortcuts | Stale binary — rebuild with `make rllogger desktop` |
| `outcome.jsonl` empty in headless smoke | Expected — `--terminate_after_init` exits before the 250 ms timer fires |
| `Permission denied` running `rllogger-export.py` | `chmod +x rllogger/util/rllogger-export.py` (should already be 755 in git) |
