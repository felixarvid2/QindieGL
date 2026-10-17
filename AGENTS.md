# Agent instructions: QindieGL

## Project and context

This fork is a C++ OpenGL-to-D3D9 wrapper for Windows games, cross-compiled on
Linux for Wine/Proton. Its Anachronox integration is paired with
[felixarvid2/anoxrtx](https://github.com/felixarvid2/anoxrtx).

Before working, read `CLAUDE.md` and `linux/README.md`. For Anachronox work, also
read the companion repository's `HANDOFF.md`, `TODO.md`, and relevant README
sections. `TODO.md` is a backlog, not proof of current implementation status:
the handoff records later work such as dynamic lights, procedural lava textures,
Remix Plus compatibility, and working DLSS. Confirm claims against the current
checkout and runtime evidence before starting or marking work complete.

Check `git status`, the current branch, and `git worktree list` before editing.
There are several QindieGL worktrees; do not assume this checkout is `master` or
that another worktree contains the same code. Preserve unrelated changes.

Use English for code, commits, and repository documentation. Follow the user's
language in conversation; the handoff records Swedish as their usual preference.

## How to use agents

The roles below adapt `CLAUDE.md` and `.claude/agents/*.md`. Those files configure
Claude; other agent tools should use equivalent roles with their available
capabilities. Do not assume Claude model names are available in another tool.

| Role | Assign it | Expected result |
| --- | --- | --- |
| Explore | Read-only file/symbol searches and questions about existing behavior | Relevant `path:line` references, short excerpts, and missing evidence |
| Mechanic | Exact, fully specified mechanical edits | Minimal diff, changed files, and check results; escalate design questions |
| Implementer | A feature or fix with a clear specification | Implementation following nearby patterns, relevant checks, and in-game verification needs |
| Architect / main agent | Architecture, trade-offs, unknown crashes, or difficult rendering bugs | Evidence-backed diagnosis, realistic options, and a recommended fix |

Claude's original mapping is Explore/Mechanic → Haiku, Implementer → Sonnet,
Architect/main → Opus. Treat this as a division of responsibilities, not a
requirement to switch models in other tools.

- Give Explore a question and scope, rather than a predetermined file list.
- Give Implementer the behavior to change, constraints, ownership, and how to
  verify it. Give Mechanic the exact edit.
- Delegate independent work in parallel when useful. Give each editing agent
  distinct file ownership; serialize overlapping changes and shared build output.
- Keep architectural decisions, integration, and final verification with the
  main agent. Subagents report changes, checks, uncertainties, and remaining work;
  they do not commit, push, or merge unless explicitly assigned that action.
- Review every returned diff and actual check output before accepting the work.
  A subagent's success message alone is not verification.

## Model routing and Gemini triage

For Codex work, use this model split requested by Felix:

| Work | Model |
| --- | --- |
| Read-only summaries of selected logs, documentation comparisons, fact extraction from supplied excerpts | Gemini `gemini-3.1-flash-lite` through the local `gemini-triage` skill |
| Routine exploration, validation orchestration, documentation, and exact mechanical edits | GPT-6 Luna |
| Substantial implementation, difficult debugging, and focused final review when needed | GPT-6.1 Sol |
| Disabled for this project unless Felix explicitly changes this preference | GPT-6 Astra |

After usage-limit feedback on 2026-10-17, prefer Gemini and Luna for bounded
work before escalating a concrete unresolved question to Sol. Keep returns
concise and evidence-based. Reuse verified results when inputs are unchanged;
repeat broad captures or reviews only when a new change or failure warrants it.

Gemini is an external API helper, not a native Codex subagent. It receives only
explicitly selected public project excerpts or sanitized logs, has no tools or
write access, and cannot validate builds. Codex reviews its findings and checks
source references before acting. Use local search for simple lookups.

The owner's local helper is
`~/.codex/skills/gemini-triage/scripts/triage.py`; consult that skill's `SKILL.md`
for invocation. The credential stays in `~/Documents/gemini-api.md`, outside the
repos, and must never appear in prompts, command arguments, URLs, logs, or git.
Do not upload game assets or private data as context. Free-tier data handling
follows Google's applicable terms.

Felix confirmed the API project uses the Free tier with billing disabled on
2026-10-17. Free pricing depends on that project setting; the API has no
per-request free-only flag. Do not enable billing. Pin the selected model, keep
requests bounded, and stop on quota errors; never silently retry or switch to a
paid service/model. If the helper or quota is unavailable, use the existing
Codex roles. The helper is a local installation, not a repo dependency.

## Implementation boundaries

- Wrapper code lives in `code/`; game hooks in `idtech3_mixup/`; Linux build
  support in `linux/` and `CMakeLists.txt`. Do not edit third-party `extern/` code.
- Keep shared code compatible with MSVC and the 32-bit MinGW target. Preserve
  compiler-specific hook/assembly paths. A Linux build does not prove MSVC works.
- Keep risky game hooks behind settings and validate signatures/layouts against
  the game version. Separate verified causes from hypotheses.
- Coordinate wrapper/config/file-format changes with anoxrtx's generators and
  installer. Check compatibility with both stock Remix and Remix Plus where
  their API layouts are involved.
- Never commit secrets, built DLLs, game data, extracted textures, DDS files,
  BSPs, or generated asset layers.

## Verification

For code or build changes, run from the worktree being edited:

```sh
cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake
cmake --build build-linux -j8
~/.local/llvm-mingw/bin/llvm-objdump -p build-linux/opengl32.dll | awk '/Ordinal +RVA +Name/{f=1;next} f&&NF==3{n++} END{print n}'
```

The build must pass and the DLL must retain 385 exports. Report any missing
toolchain or failed check explicitly. Documentation-only edits need diff and
link/path review, without rebuilding the DLL.

For rendering changes, specify and run the relevant in-game check when possible;
record the map, settings, logs, and before/after screenshots. Follow anoxrtx's
`HANDOFF.md` for the nested Xwayland test rig and restore Steam/display settings
even on failure. Never enable `PROTON_ENABLE_WAYLAND` outside that nested session.
Back up installed game files before replacement. Do not run the Remix game and
ComfyUI/FLUX together on the documented 8 GB GPU.

Report implementation, build/export verification, MSVC verification, and visual
verification separately. Built but visually unverified work stays unverified.
Follow the handoff's branch/PR workflow when shipping implementation work; the
main agent owns integration and must inspect the final diff before committing.
