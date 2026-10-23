# Medal under SKJ Wine

State 2026-10-23: installs, sign-in works, screen recording works (CPU encoder, 1280x720), F8 saves a clip with sound.

## What was needed
- **Fonts in the prefix** (`tools/prefix-prep.sh`): Medal's window is Electron 43 / Chromium 150. With no font in
  `windows/Fonts`, DirectWrite finds neither "Tahoma" nor "sans"; Chromium's `gfx::SystemFonts::Initialize` →
  `PlatformFontSkia::InitFromDetails` ("Could not find any font") → default font → `SystemFonts::Initialize` again,
  until the main thread's stack overflows (`virtual_setup_exception stack overflow`). Found by printing a stack from
  inside `windows.ui!uisettings2_get_TextScaleFactor` (called on every round) and reading strings near the return addresses.
- **.NET Framework 4.8** for the recorder. On Wine Mono it connected but never reported ready. The 4.8 installer skips
  everything while Wine Mono's registry says 4.8.1 is installed (`NDP\v4\Full Release`), and it does not ship
  `mscoree.dll` for Windows 10; that comes from .NET 4.0's `netfx_Core.mzz` (`_003_mscoree_dll_amd64/x86`).
- **websocket.dll** (`patches/wine/0012`): .NET Framework's `ClientWebSocket` runs on the WebSocket Protocol Component
  API; Wine had stubs. The recorder and Medal talk JSON-RPC over `ws://127.0.0.1:10603`.
- The recorder waits for Medal to answer `user`; Medal answers once someone is signed in.
- **Process-loopback audio** (`patches/wine/0013`): the recorder asks for `VAD\Process_Loopback`; without it
  recording stops with `SCOPE_ERR_FILE_NOT_FOUND` and Medal switches screen recording off again. Wine now gives the loopback
  of the default output, so the "game" and "Medal" tracks both hold all PC sound.
- **d2d1 multithread lock** (`patches/wine/0014`): the recorder copies desktop frames on one thread and scales them with
  Direct2D on another, on one D3D11 device. Without the lock DXVK crashed in `SwapDeviceContextState` ~3 s into recording
  (recorder exit code 4294930434, then "crash loop").
- **Fewer Chromium processes** (`skj-run` adds `--disable-site-isolation-trials --disable-features=IsolateOrigins,site-per-process
  --renderer-process-limit=4`; `SKJ_MEDAL_FLAGS=` turns it off). Medal.exe's code (210 MB) sits at file offsets that
  aren't page aligned, so Wine reads it into private memory in every process (~550 MB per renderer). With site
  isolation each ad frame on the home page got its own renderer: 12 renderers, 15 GB, OOM within seconds of start.
  Now ~3.7 GB while recording.
- **`DXVK_ENABLE_NVAPI=1`** (set by `skj-run`): otherwise DXVK reports the RTX as AMD (1002:73DF) and Medal looks for AMF.

## Open
- Hardware encoding: Medal's capture library (`scope.dll`) has its own NVENC encoder that opens the session with the
  D3D11 device (`NvEncOpenEncodeSessionEx` then unload). The nvidia-libs wrapper only has CUDA/GL behind it, so Medal
  lists 0 hardware encoders and uses the CPU (x264). Also present: AMF, Intel VPL, Media Foundation (`MFVideoEncoder`).
- Hotkeys: Medal's own hooks see keys only in Wine windows, so `bin/skj-hotkeys --medal` (started by `skj-run`) registers
  them with the desktop and types them into the prefix. They only do something while recording runs: switch on
  Screen Recording in Medal (games under Proton aren't detected as games, so Medal records the screen).
- Recording: screen capture goes through `IDXGIOutput1::DuplicateOutput` → SKJ's dxgi.dll + skj-screencast, as for Moments.
- Process loopback records all sound, not one program.

## Debug tips
- Recorder log: `users/steamuser/Documents/Medal/MedalLog<date>.txt`; Medal's: `AppData/Roaming/Medal/.logs/`.
- Decompiled recorder: `ilspycmd` (needs `DOTNET_ROLL_FORWARD=Major` with a newer runtime).
- `WINEDEBUG=+websocket` logs every message text both ways.
