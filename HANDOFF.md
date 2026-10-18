# Handoff: Anachronox + RTX Remix on Linux (for Codex)

Project context for the two repositories:
- **felixarvid2/anoxrtx**: the Remix mod (configs, USD layers, tools, installer). Fork of vinevi1/anoxrtx.
  Local checkout: `/home/felix/Documents/jensen2/anoxrtx` (branch `main`).
- **felixarvid2/QindieGL**: the OpenGL→D3D9 wrapper that makes the game Remix-compatible, with a Linux
  cross-build. Fork of whisperglen/QindieGL. Local checkout: `/home/felix/Documents/jensen2/QindieGL` plus
  git worktrees `QindieGL-master` (normally used, branch `master`), `-flash`, `-maplights`, `-ml2`, `-texdump`.

Follow the owner's language in conversation. Repo text, commits and code are in English
(`RESULTS.md`, `PLAN.md`, `TODO.md` in anoxrtx are Swedish).

## Current status and next work — 2026-10-18

Read [STATUS.md](STATUS.md) first. The ordered feature plan is
[anoxrtx PLAN.md](https://github.com/felixarvid2/anoxrtx/blob/main/PLAN.md), with
[wrapper responsibilities](https://github.com/felixarvid2/QindieGL/blob/master/ROADMAP.md).
Console input/save loading is fixed and merged (#27); map-light identity
correction is merged (QindieGL #21). Next: diagnose initial post-save-load
lighting while adding repeatable screenshot regression. Then extend the
existing debug overlay, event lighting, BSP/PVS, dynamic emission, PBR
classification, anchors, environment transitions and finally rain/wetness.
Keep current lighting/material defaults until comparisons justify changes.

The original checkout directories may lag GitHub. Inspect refs/worktrees;
this documentation update uses persistent `anoxrtx-roadmap` and
`QindieGL-roadmap` worktrees. External-model routing PRs anoxrtx #28 and
QindieGL #23 remain separate open proposals at this snapshot.

## Working agreements
- After finished work: push a branch, open a PR, merge it (`gh pr create` + `gh pr merge --merge`). The owner
  expects this; ask only for things outside the two repos.
- Never commit game data (textures, DDS, BSPs, generated layers). Tools generate them from the user's files.
- QindieGL code changes require the Linux build to pass with 385 exports. Documentation-only changes require diff/link review:
  `cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake && cmake --build build-linux -j8`
  `~/.local/llvm-mingw/bin/llvm-objdump -p build-linux/opengl32.dll | awk '/Ordinal +RVA +Name/{f=1;next} f&&NF==3{n++} END{print n}'`
- Do not use `PROTON_ENABLE_WAYLAND` outside the nested test session: it crashed the owner's desktop once.
- `pkill -f <pattern>` can kill your own shell when the pattern appears in the command line; use `[x]`
  bracket patterns or check pids.
- The machine has 14 GB RAM and an 8 GB RTX 5060 Laptop GPU. The game (Remix bridge ~5 GB) and ComfyUI (FLUX)
  together run out of memory. Confirm ComfyUI is stopped before game tests
  (`~/ComfyUI/start-comfyui.sh` starts it).
- Back up game files before replacing them (the installer does this into `<game>/anoxrtx-backup/`).

## Machine and game
- Game: `~/.steam/debian-installation/steamapps/common/Anachronox` (Steam app 242940), GE-Proton 11-9.
  Proton prefix: `~/.steam/debian-installation/steamapps/compatdata/242940/pfx`. Wine sees the game as
  `S:\steamapps\common\Anachronox`.
- Steam launch options (localconfig.vdf, app 242940):
  `__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only __GLX_VENDOR_LIBRARY_NAME=nvidia VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json DXVK_LOG_LEVEL=info WINEDLLOVERRIDES="opengl32=n,b;d3d9=n,b" %command%`
  Steam must be shut down (`steam -shutdown`) before editing localconfig.vdf, or it overwrites the change.
- Last verified installation (2026-10-18; recheck before runtime work): QindieGL `master` light-identity fix (source `642a5e5`, merged `99881f0`), **Remix Plus 1.5.1** runtime (stock Remix 1.5.2 is
  kept as `.trex.remix152` + `d3d9.dll.remix152`), Chuncki's 4x texture pack (`anoxdata/textures`,
  `TEXTURES.dat` renamed `.orig`, the pack's `ref_gl.dll`), mod at `rtx-remix/mods/anoxrt` with sublayers
  hd_sky, liquids, emissive, models_hd, pbr, proctex. QindieGL.ini `[game.anox]`: remixapi, maplights,
  maplights_scale 100, dlights, proctex, smoothnormals, camera detection. `anox_customise.ini` next to anox.exe.
  The verified fixed DLL remains installed after the 2026-10-17 saved-view test. Its fingerprint and
  previous-DLL backup are recorded in `~/.cache/anoxrtx/backups/maplight-identity-install.json`.
- Tools: llvm-mingw is expected in `~/.local/llvm-mingw`; `tools/install.sh`
  manages a persistent Python environment in `~/.cache/anoxrtx/venv`. Confirm
  its existence and dependencies (Pillow, numpy, xxhash, usd-core, python-xlib)
  before use. The old `/tmp/claude-1000/.../venv` path is historical and must
  not be relied on. Console automation also requires `xkbcomp`, `xset` and
  enabled autorepeat. Real-ESRGAN ncnn is downloaded by the installer.
  Confirm current GitHub authentication before publishing.

## How to test in the game (anoxrtx `tools/anoxrig.py`)
The rig drives the game in a nested X server so the owner's desktop is not disturbed:
1. Start `Xwayland :7 -geometry 1920x1200 -decorate -noreset` if not running.
2. Shut Steam down, prefix the launch options with `DISPLAY=:7 ` (optionally append ` +map <name>` after
   `%command%` to start straight into a map, e.g. `+map sunstrip`, `+map hephlava`), start the game with
   `anoxrig.py start`.
3. `anoxrig.py shot NAME` (screenshot to `tools/shots/`), `to-rowdys` (New Universe → Rowdy's bar, ~4 min),
   `timeline PFX T...`, `key`, `chord`, `sendkey`, `move`, `click`, `log`, `stop`.
4. Afterwards: stop the game, shut Steam down, remove `DISPLAY=:7 ` and `+map ...` again, kill the Xwayland
   on :7, restart Steam (`DISPLAY=:1 steam -silent`). Always restore this, also when a test fails (use a trap).
- Logs: `<game>/QindieGL.log` (MapLights, DLights, ProcTex, RTX.conf lines), `<game>/rtx-remix/logs/`
  (`remix-dxvk.log`, `bridge32.log` – "The RTX Remix Runtime has encountered an unexpected issue" = Remix crash).
- Rig update (2026-10-17): `start [seconds]` checks stable visible game-window content rather than
  requiring the stock runtime's `Camera cut` message. It reports camera confirmation separately,
  preserves logs, and exits nonzero on timeout. Visible content is not proof a level is loaded;
  a black intro may need `key Escape` from another terminal.
- `key`, `chord`, and `hold SECS KEY...` focus the game. Use `hold 2 w` for movement; short taps are
  not a navigation command. In a nested Rowdy's test, wrong focus prevented movement, while focusing
  the game and holding keys moved the logged camera. Keys are released on completion/interruption.
  Remix overlay/capture hotkeys and repeatable routes still need separate work.
- Save loading (2026-10-17): `list-saves` lists local `PROFILE/SLOT` selectors, maps, descriptions,
  and SHA-256 fingerprints. `load-save steamuser/1 [seconds]` uses the game's console to load a
  slot, verifies a nonce-bounded console dump, waits for the scene to render, and records settled
  position/yaw. It preserves save/config files and restores developer mode. Console dumps are
  temporary. Requires a running game with initial material loading finished; see the
  [test locations](https://github.com/felixarvid2/anoxrtx/blob/main/docs/test-locations.md). Steam `-applaunch` arguments were not forwarded by this launch wrapper,
  and the normal autoexec file did not execute, so neither is used by the rig.
- Same-map restores need console closure before the camera settles. This game's `LoadGame2: Aborted`
  message is emitted even after restoration; fresh filename/lump/read-error evidence and final
  camera position are used instead. A map-light hot reload alone is not save-load evidence.
- Console input verified 2026-10-18: the game interprets US physical keys even
  when Xwayland reports Swedish symbols. The rig resolves XKB key names with
  `xkbcomp`, retains 120 ms typing and repeat-rate-aware held Backspace, and
  rejects unavailable maps/repeat settings before input. Both saves passed with
  the default waiting budget, correct poses, console closure and developer-mode
  restoration. A full stale prompt, disabled repeat, and SIGINT/SIGTERM key
  release were checked. See [investigation and recovery](https://github.com/felixarvid2/anoxrtx/blob/main/docs/console-input-check.md).
  Keep worktrees and runtime recovery backups on persistent storage.

## What exists (see README.md in each repo for details)
QindieGL options (all in `[game.anox]` of QindieGL.ini unless noted):
- `BorderlessFullscreen` ([Settings]), `smoothnormals`, `DumpTextures` (Remix hashes + TGA dumps).
- `maplights`: static Remix API lights per map from `rtx-remix/maplights/<map>.lights` (written by
  anoxrtx `tools/bsp2lights.py`, `format raw`), Quake 2 light styles, sun, spotlights. Light formula and Remix
  variables per map in `anox_customise.ini` (`[maplights.global|<map>]`, `[rtxconf.default|<map>]`); per-light
  hand fixes in `<map>.override`; live reload when these files are saved; camera position logged on reload.
  Map changes are applied 120 frames late. Code: `code/rmx_maplights.cpp`, hook in `idtech3_mixup/hooking.cpp`.
- `dlights`: hooks the non-exported `R_RenderView` in ref_gl.dll (found via its "NULL worldmodel" string,
  at +0x176bd); Anachronox refdef_t has num_dlights/dlights at +0x74/+0x78; drawn as Remix sphere lights.
- `proctex`: hooks anoxgfx.dll `TEX_BindTexture` + `TEX_GetTexName` to name GL textures; textures matching
  `proctex_names` (default `proc_lava`) are frozen to a flat colour from fnv1a32(name), giving a stable Remix
  hash (computed offline by anoxrtx `tools/proctex_layer.py`; QindieGL logs the real hash).
- Remix API: works with stock Remix and Remix Plus (Plus inserts functions in `remixapi_Interface`; QindieGL
  maps them by layout in `code/d3d_global.cpp`).

anoxrtx tools (`tools/`): `install.sh`/`install.py` (whole setup from the user's files, steps can be run alone),
`toggle.sh`, `adat.py`, `bspinfo.py`, `bsp2lights.py`, `lights_near.py`, `remixhash.py`, `make_layers.py`
(sky + liquids), `emissive_layer.py`, `pbr_layer.py`, `hd_layer.py`, `flux_refine.py` (ComfyUI FLUX posters),
`proctex_layer.py`, `rehash_textures.py` (+ `maps/chuncki_texmap.tsv`), `lightcal.py`, `anoxrig.py`.

## Pitfalls already solved (do not undo)
- `rtx.conf`: `rtx.dlfg.enable = False`, `rtx.dlfg.enablePresentMetering = False` (Remix hangs in Present under
  Wine), `rtx.useNewGuiInputMethod = False` (white window under Wine), `rtx.integrateIndirectMode = 0`
  (NRC cannot start under Wine; without this line any runtime `SetConfigVariable` re-enables NRC and crashes
  Remix), `rtx.upscalerType = 1` + `rtx.qualityDLSS = 5` (DLAA; DLSS works under GE-Proton).
- `.trex/bridge.conf`: `presentSemaphoreEnabled = False`, `exposeRemixApi = True`.
- Remix Plus first-start dialog blocks input and defaults crash/usage reports to NVIDIA to on. It is answered
  (data collection off, owner's choice) in `pfx/drive_c/users/steamuser/AppData/Local/NVIDIA/RTXRemix/user_settings/<xxh3(game root)>.conf`.
  Never use `rtx.automation.disableBlockingDialogBoxes`: it grants the consent.
- Numos sky (`rtx.skyMode = 1`, Remix Plus) must stay per map: globally it replaces the space/city skies.
  It is on for sunstrip, starport, comhills, baltown, baltownr (maps with an ArghRad sun), with the map light sun
  off there.
- Texture hashes: Remix hash = XXH3_64 of mip 0 in D3D format (BGRA, alpha 255 without alpha). The texture pack
  changes level texture hashes; the mod's hand-made materials are remapped with `rehash_textures.py`.

## Fog (done after the first version of this file)
The game's GL fog reaches Remix, but `rtx.enableFog` alone shows almost nothing. `rtx.volumetrics.enableFogRemap`
and `enableFogColorRemap` are now on in `rtx.conf` and in `[rtxconf.default]` of `anox_customise.ini` (so a map can
turn them off). Tested: bricksb gets the original teal haze, sunstrip a light haze, the alley at the start of
`+map rowdys` gets a grey milky haze (flatter; owner to judge). whitecaves-b (densest fog) hangs on "One moment"
when started with `+map`, so it is untested. Tuning knobs: `rtx.volumetrics.fogRemap*` bounds.

## Not yet verified in the game
- Lava freeze (`proctex`): look in Levant for "ProcTex: ... frozen ..., Remix texture hash 0x561447B2DFA181FD"
  (proc_lava) / `0x26C0B031CCFA2266` (proc_lavafloe) in QindieGL.log. Focused movement works; a verified lava location/route is still missing.
- Dynamic lights in combat (`dlights`). QindieGL PR #6 (flashblend) is already merged; evaluate the later dlights path first and avoid duplicate illumination.
- Rowdy's normal maps and five sign emissions have visible effects in two saved views
  ([2026-10-17 check](https://github.com/felixarvid2/anoxrtx/blob/main/docs/rowdys-material-check.md)); retain defaults. Close-up checks for bright
  rims on sharp colour edges still remain (`pbr_layer.py --strength`, default 2).
- Numos sun rotation convention (shadow direction) on the five outdoor maps.
- Map light brightness in environments other than Rowdy's.
- Initial post-load light state still needs investigation: the first saved view's initial scale-1
  frames looked like scale 0 until explicit reload, while the second view's initial, same-value
  reload and final scale-1 frames agreed. Both views show clear 0/1 contribution after confirmed
  reloads; retain defaults. See the [saved-view check](https://github.com/felixarvid2/anoxrtx/blob/main/docs/saved-view-lighting-check.md).

## Further ideas (priority is defined by the current plan)
1. In-game menu for lights/overrides (QindieGL already has ImGui code for other games: `qdx_imgui_*`).
2. Weather per map with Remix Plus presets (rain/snow where the game has them).
3. Remix variables driven by game state (cutscenes, underwater).
4. Anchor meshes, conservative culling control, complete `rtx.uiTextures`/particle/beam lists.
5. Add more fixed test locations and repeatable routes; focused movement (`hold`) and save loading now work.
