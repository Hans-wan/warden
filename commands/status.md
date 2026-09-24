---
description: Show warden's view of this session — mode, active skills, counters, last denial
---

# warden status

Show the user what warden is seeing in this session.

1. Locate the warden binary. Try, in order:
   - `${CLAUDE_PLUGIN_ROOT}/bin/warden-launcher` (plugin install)
   - `warden` on PATH
   - `./build/warden` in the current project
   If none exists, tell the user warden is not installed and point them at
   the README install section. Stop there.
2. Run `warden status` from the project root (the directory containing
   `.warden/`). Pass the root explicitly if needed: `warden status .`.
3. Check whether the user's `~/.claude/settings.json` contains a `statusLine`
   key (read it if it exists). If it has no `statusLine` key, offer to set
   up the user's statusLine: ask permission, then edit
   `~/.claude/settings.json` to add

```json
{
  "statusLine": {
    "type": "command",
    "command": "warden statusline",
    "padding": 0
  }
}
```

(merging into existing settings if present — never overwrite unrelated keys;
if `warden` is not on PATH use the resolved plugin binary path from step 1
instead). Claude Code applies the statusline change on the next turn.
4. Present the results as a short readable summary: current mode and what it
   means, which skills are open and what they may do, the allow/ask/deny
   counters, and the last denial (if any) with a one-line explanation of why
   it was blocked. Plain language; no raw JSON dumps.
