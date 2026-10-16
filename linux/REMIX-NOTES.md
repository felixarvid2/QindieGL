# RTX Remix under Wine/Proton (Anachronox, 32-bit game)

Findings from running the Remix bridge (remix-1.5.2) with this QindieGL build on Proton Experimental,
NVIDIA RTX 5060 laptop + AMD iGPU, driver 595, COSMIC/Wayland (XWayland).

## Fixed: server hang inside Present
Symptom: client logs `Still waiting on the Present semaphore`, server (`NvRemixBridge.exe`) stuck in
`IDirect3DSwapChain9_Present`, `dxvk-queue` polling `/dev/nvidia0`, GPU idle (P8).
Cause: DLSS frame generation is on by default in Remix (`rtx.dlfg.enable = True`, plus hardware present
metering). Add to `rtx.conf` in the game folder:

    rtx.dlfg.enable = False
    rtx.dlfg.enablePresentMetering = False

(Disabling Reflex, DLSS (`rtx.upscalerType = 0`), ray tracing or vsync did not help on their own.)
After this the message channel handshake completes, Remix finds the camera (`Camera cut detected`) and
the server keeps rendering. `presentSemaphoreEnabled = False` in `.trex/bridge.conf` was used while
debugging; not re-tested with it enabled.

## Open: window stays white
With Remix active the game window is solid white (also with `rtx.enableRaytracing = False` and with
`rtx.showUI = 2`, so even Remix' own UI never appears). Without Remix the same QindieGL build shows the
Anachronox intro normally. Suspect: the 64-bit bridge server presents into a window owned by the 32-bit
game process; with Wine/XWayland that surface is never shown. Not yet tried: windowed mode, Proton-GE,
`PROTON_ENABLE_WAYLAND`, gamescope.

## Debug tips
- Use the `debugoptimized` Remix zip + `logLevel = Trace`, `logApiCalls = True`, `logAllCommands = True`
  in `.trex/bridge.conf`; bridge32.log/bridge64.log show the last command (UID) each side processed.
- No gdb: read thread stacks from /proc/<pid>/task/*/syscall + /proc/<pid>/mem (SIGSTOP first), and
  `strace -p <tid>` for what a thread polls (`ls -l /proc/<pid>/fd/<n>`).
- `cosmic-screenshot --interactive=false --save-dir=<dir>` takes a screenshot on COSMIC.
