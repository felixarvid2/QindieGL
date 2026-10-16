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

## Open: window turns white after the first frame
With Remix active the game window shows one dark raster frame (the Anachronox intro) and then turns solid
white (255,255,255) for good. Checked with screenshots cropped to the window:
- same in fullscreen and in windowed mode (`+set u_vid_fullscreen 0 +set vid_fullscreen 0 +set u_gl_mode 3 +set gl_mode 3`),
- same with stock Remix settings and no mod (not vinevi1's rtx.conf / mod.usda),
- same with `rtx.enableRaytracing = False`, so it is not ray-traced shading,
- Remix' own UI (`rtx.showUI = 2`, log says "Remix UI activated") never becomes visible.
So Remix is rendering (GPU busy, camera found, message channel established) but its swapchain output does
not reach the screen after the swapchains are recreated at startup (QindieGL creates the device twice;
log shows IMMEDIATE -> FIFO -> FIFO swapchains). The X11 tree of the window has no child windows.
Not yet tried: Proton-GE, gamescope, avoiding the device re-creation, `PROTON_ENABLE_WAYLAND=1`.
Without Remix the same QindieGL build renders normally under Proton's DXVK.

## Debug tips
- Use the `debugoptimized` Remix zip + `logLevel = Trace`, `logApiCalls = True`, `logAllCommands = True`
  in `.trex/bridge.conf`; bridge32.log/bridge64.log show the last command (UID) each side processed.
- No gdb: read thread stacks from /proc/<pid>/task/*/syscall + /proc/<pid>/mem (SIGSTOP first), and
  `strace -p <tid>` for what a thread polls (`ls -l /proc/<pid>/fd/<n>`).
- `cosmic-screenshot --interactive=false --save-dir=<dir>` takes a screenshot on COSMIC.
