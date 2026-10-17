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

## Model routing and external helpers

Use external models as the default workers to reduce Codex usage:

| Work | Model |
| --- | --- |
| Routine source exploration, log analysis, documentation and small patch proposals | NVIDIA `z-ai/glm-5.3-flash` |
| Substantial implementation, difficult debugging and design proposals | NVIDIA `z-ai/glm-5.3` |
| Focused review of substantial changes and alternative failure explanations | NVIDIA `deepseek-ai/deepseek-v4.1-flash` |
| Bulk summaries and simple fact extraction | Gemini `gemini-3.1-flash-lite` |
| Local tools, applying reviewed patches, tests and Git operations | GPT-6 Luna |
| Concrete unresolved questions and consequential final review | GPT-6.1 Sol |
| Disabled unless Felix explicitly changes this preference | GPT-6 Astra |

The local NVIDIA helper and instructions are in
`~/.codex/skills/external-models/SKILL.md`; Gemini uses
`~/.codex/skills/gemini-triage/SKILL.md`. These are external API helpers,
not native Codex subagents. They propose patches or findings; local execution
retains control of files, game sessions, Steam and Git. Never accept a model's
claim that checks ran without running them locally.

Felix authorized selected project code, unpublished changes and relevant logs
as external context. Send task-relevant excerpts, not the conversation history
or entire repositories. Credentials stay in `~/Documents/glm-nvidia.md`,
`~/Documents/deepseek-nvidia.md` and `~/Documents/gemini-api.md`; never include
them in prompts, arguments, logs or git. Keep game assets, saves and unrelated
private files outside task packets.

NVIDIA usage is free-only: verify the configured endpoint/model before activation;
never enable billing, purchase credits or route to paid partner endpoints.
The API has no per-request free-only flag. Gemini's project was confirmed on
2026-10-17 to have billing disabled. Stop a provider for the task on quota,
authentication or unavailable-model errors; do not rotate keys to bypass limits.
Try another verified free helper, then Luna if needed; no automatic Sol escalation.

Keep full responses locally and return concise summaries, artifact paths and
actual check results. Default packets are at most 64 KB; analysis output is
capped at 2,000 tokens and patch output at 8,000. Limit a bounded task to three
NVIDIA generation requests across models. Reuse successful results when task,
model and inputs are unchanged. Review substantial changes with DeepSeek when
useful; do not duplicate reviews or repeat checks without a concrete reason.
Report measured provider usage; do not invent Codex savings percentages.

Keep task artifacts, worktrees and runtime recovery backups on persistent
storage. The 2026-10-17 laptop restart was caused by battery depletion, as
confirmed by Felix. Check external power before game tests, keep runtime
ownership serialized and restore the owner's settings on exit.

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
