---
name: release-note-since-previous-release
description: "Generate a Release Note entry since the previous release."
disable-model-invocation: true
---

Related skill: `release-note`.


## Tooling Requirements

- Agent tools are allowed for this prompt.
- Git and history operations shall be performed with GitKraken/MCP git tooling (for example: git status/log/diff/tag lookup/blame via the available GitKraken git tool groups).
- For commit collection and baseline resolution, prefer GitKraken git tooling over any terminal command.

## Tooling Prohibitions

- Do not execute external shell/runtime commands (for example: `node`, `python`, `cmd`, `powershell`, `bash`, `sh`).
- Do not use terminal-based git commands; use GitKraken/MCP git tooling instead.
- Do not use helper scripts for parsing/validation; rely on workspace file reads/edits and agent-native tooling only.

## Required Workflow

1. Resolve the baseline tag as the most recent reachable tag; if that tag points at `HEAD`, use the immediately previous tag as the baseline.
2. Collect commits once from the resolved baseline tag to `HEAD`.
3. Create `Release.md` at the workspace root (overwrite if it already exists) containing a GitHub release note for the new version.


## Rules

- Do not invent changes; only use evidence from git history.
- Keep wording concise and factual.
- Use the latest reachable tag by default; only when that latest tag points to `HEAD`, use the immediately previous reachable tag.
- Prefer tag baseline (`<lastVersion>..HEAD`) over blame/boundary baseline.


## Output

- Apply file edits directly (`Release.md`).
