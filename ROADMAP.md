# Anachronox wrapper roadmap — 2026-10-18

The complete ordered plan and acceptance criteria live in
[anoxrtx PLAN.md](https://github.com/felixarvid2/anoxrtx/blob/main/PLAN.md).
[STATUS.md](STATUS.md) distinguishes implemented code from visual verification.
This document assigns the QindieGL side; it does not describe shipped features.

| Order | QindieGL work | Companion work / release condition |
| --- | --- | --- |
| 0 | Diagnose initial map-light state after save restore using the fixed identity path | Repeat both saved views before reload; explain/fix dim initial state without changing light defaults |
| 1 | Supply bounded lifecycle/counter logs needed for screenshot regression | anoxrtx owns capture manifests, comparisons, tolerances and local baselines |
| 2 | Extend existing `win_imgui.cpp` overlay with map, state, light/API and culling diagnostics | Verify visibility/input under Proton and an overlay-free capture path |
| 3 | Read verified Anachronox lightstyle/event/game-time data; synchronize animated/switchable lights | Extend exporter/format together; retain START_OFF and event identities; verify on/off, pause, restore and map transitions |
| 4 | Measure submission/culling early, then introduce conservative Anachronox BSP/PVS policy | Prove shadows/reflections/indirect lighting and CPU/GPU/VRAM cost; version any BSP metadata with tools |
| 5 | Prove supported runtime material-state path and synchronize emission with light events | anoxrtx authors mappings/layers; verify visual emission and lighting without duplication or stale state |
| 6 | Support classifier only if an observed runtime identification gap requires it | Offline PBR rules/exceptions/reports belong in anoxrtx; this track can run independently after capture baselines |
| 7 | Stable static anchor mesh prototype, IDs/transforms and resource lifecycle; moving parents later | Per-map authoring data/generator and proven replacement matching; survive restart/reload/map change |
| 8 | Apply observed environmental states through supported runtime variables, with priorities and baseline restoration | Per-map fog/state presets; verify interpolation, pause/save/map behavior and runtime fallback |
| 9 | Integrate outdoor weather and wet material states after those foundations | One rain map first, indoor shelter, drying and frame-time/VRAM checks; stock/Plus support explicit |

## Delivered follow-up

Startup device replacement now releases map/dynamic light resources before
teardown and schedules the existing delayed map-light rebuild on the new
device. Controlled Rowdy's evidence is in the
[runtime report](https://github.com/felixarvid2/anoxrtx/blob/main/docs/startup-lighting-check.md).
The Linux build retains 385 exports. anoxrtx now provides verified capture
manifests and an offline comparator; 61 Python tests pass. Broader map/runtime
and MSVC coverage remain separate. Next feature work is the Anachronox overlay
extension, followed by game-driven event lighting.

## Design constraints established by source inspection

- `code/rmx_maplights.cpp` already animates styles 1–11 at 10 Hz using wall
  time. Switch styles 32–62 are treated as on. Add real state/game-time input;
  do not reimplement timer flicker as the requested event feature.
- `idtech3_mixup/hooking.cpp` captures frame dlights through `R_RenderView`.
  Confirm Anachronox's extended layouts for any new fields, keep hooks opt-in,
  and preserve combat dlights while checking duplicate flashblend illumination.
- The exporter currently drops START_OFF lights. Coordinate a compatible
  format extension with anoxrtx; keep stable authoring IDs separate from the
  fresh process-lifetime Remix identities needed by the merged reload fix.
- Current map-light submission has no BSP/PVS filter. Heretic 2's PVS code is
  not an Anachronox/Linux implementation. Geometry omitted by the game cannot
  be recovered by filtering wrapper lights. Camera-only PVS is insufficient
  for path-traced shadows, reflections and indirect illumination.
- Procedural freezing in `code/d3d_texture.cpp` enables stable replacement
  hashes; it does not implement event-driven material emission. Material/mesh
  API pointers alone do not prove supported update/anchor behavior.
- Per-map config in `code/rmx_gen.cpp`, GL fog passthrough and existing ImGui
  are foundations to extend. Introduce one observed state source shared by
  lights/materials/fog, with explicit reset on save/map lifecycle changes.

## Verification and scope

Each wrapper code change needs a Linux cross-build and 385-export check,
targeted behavior tests, and the relevant controlled in-game comparison.
Report MSVC, stock Remix and Remix Plus coverage separately. Documentation-only
changes need diff and link/path checks. Follow [HANDOFF.md](HANDOFF.md) for
serialized nested-session runtime tests and restoration; keep current Wine
fixes, telemetry choices and per-map Numos scope. Never commit game assets,
generated content or DLLs. Aspect ratio/resolution, UI/particle classification
and MSVC verification remain maintenance tasks in the companion TODO.
