# SKJ Wine - developer notes

What was learned while getting apps to run; read this before working on an app.
Per-app findings and open items: `apps/<app>/NOTES.md` (GG: `apps/steelseries-gg/NOTES.md`).
Reverse-engineering helpers: `docs/re-tools/`.

## How GG is put together
- Version tested: **GG 120.0.0** (installer `https://steelseries.com/gg/downloads/gg/latest/windows` → `SteelSeriesGG120.0.0Setup.exe`, NSIS, silent `/S`).
- **GGEZ** (`SteelSeriesGGEZ.exe`, .NET 8 ASP.NET Core) = the server, `https://127.0.0.1:6327` (websocket for sub-apps: `wss://127.0.0.1:6327/eventing`). Starts the sub-apps listed in `ProgramData/SteelSeries/GG/db/ggez.db` table `sub_apps` (`oldGG, moments, performanceCoach, engine, sonar, threeDAT`; columns `is_enabled, auto_start, executable_name`). Passes them env vars `GG_SERVER_ADDRESS, GG_CERT_TEXT, GG_API_AUTH_TOKEN, APP_INSTALL_PATH, APP_DATA_PATH, …` (see `GG.Services/…/ProcessManagerService.cs` when decompiled).
- **oldGG** (`SteelSeriesGG.exe --hosted`, Go) = GG core; GameSense API on a random port written to `ProgramData/SteelSeries/GG/coreProps.json` (`address`, `encryptedAddress`, `ggEncryptedAddress`) — also copied to `ProgramData/SteelSeries/SteelSeries Engine 3/coreProps.json`.
- **Engine** (`apps/engine/SteelSeriesEngine.exe`, Go) + `SSEdevice.dll` (C++) = device layer. Talks to devices via `hid.dll`/`setupapi` (works through Wine hidraw) and to `\\.\SSengine` (our `skjsshid.sys`). **Prism** (`apps/engine/prism/SteelSeriesPrism.exe`) = RGB sync. **Moments**, **Sonar** (`apps/sonar`), **threeDAT** (3D aim trainer launcher), **cvgamesense** (`cvgamesense/SteelSeriesCVGameSense.exe`, computer-vision game detection, PyInstaller-style `_internal/` folder) are separate.
- **UI**: `SteelSeriesGGClient.exe` (Electron); reads `windowProps.json` (missing = defaults; `disableHardwareAcceleration` would turn GPU off).
- TLS: GGEZ uses the cert whose SHA-1 thumbprint is in `HKLM\Software\SteelSeries\GGCertificateThumbprint`, from `LocalMachine\My`. setup.sh makes it with openssl + `tools/certinstall`.
- Windows autostart = `HKLM\…\Run\SteelSeriesGG` → `SteelSeriesGGEZ.exe -dataPath="C:\ProgramData\SteelSeries\GG" -dbEnv=production -auto=true`. Start-menu shortcut = same without `-auto=true`. GGEZ CLI: `-dev -enableDebugLog -adminApisEnabled -deepLink -installPath -dataPath`; unknown args are passed to oldGG.
- Installer steps that FAIL under Wine and are redone in `apps/steelseries-gg/setup.sh`: PowerShell cert script, `InstallUtil` service registration (update-service proxy, not needed), DB table migration (sub_apps rows), `dbconf.yml` copies, `shared/guid.json`, KMDF drivers (`sshid, ssdevfactory, msihid, ssbthid, ssps2, sssmbus` → disabled; `skjsshid` installed instead).
- **Events** (needed for GameSense/Moments too): every Go sub-app keeps a websocket to GGEZ `wss://127.0.0.1:6327/eventing` (UI: `/sock`). GGEZ itself only relays: its `GGProxyService` connects as a *client* to the GG core's `wss://127.0.0.1:6329/eventing` and `/sock` and forwards both ways. Device flow: Engine `POST :6327/engine/device/connected` → GGEZ HTTP proxy → oldGG `BroadcastToAllGroups("deviceConnected")` → GGEZ proxy → Engine `gg_events.handleDeviceConnected` → `prismsync_executor.ConnectDevice` ("New device (ID 203), instantiating executor"). Prism (hosted by the Engine: Engine's own `/eventing`, Prism HTTP on a random port in Engine's `/subApps`) sends frames to the Engine, which writes them to the device. If the GGEZ→oldGG proxy is down nothing errors, events just never arrive (`docs/re-tools/wslisten.py` shows both sides).
- GGEZ decompiles with ilspycmd 8.2 (`GG.Services.dll`: `GGProxy/`, `Socket.Client/WebSocketClient.cs`, `SubApps/`); GGEZ writes almost nothing to gg-errorlog.txt even with `-enableDebugLog`.
- Debug logging: `skj-gg` passes nothing extra; for more GG logs run GGEZ with `-enableDebugLog` (passed on to all sub-apps).

## skjsshid.sys / sshid.sys (summary; details in `drivers/skjsshid/README.md`)
- SteelSeries `sshid.sys` 2.11.5.0 = KMDF **USB lower filter** for the PIDs in `sshid.inf` (incl. Aerox 3 `1838/183A`), creates `\\.\SSengine`. It does **software features only**: macro injection, button suppression, accel/decel/angle-snapping, key-event reporting. DPI/RGB/polling go over plain HID.
- IOCTLs `0xC0DE0000…0x44` (METHOD_BUFFERED): `00/04` disable/enable injection; `08` queue input packet (0x814 B, header `u16 vid,u16 pid`, sent by a 1 ms timer; packet types 2 / 0x0D seen); `10` flush; `0C,14,18,1C,20,24,28,38,3C,40,44` per-device settings (some doubles); `2C` register event HANDLE; `30` read ≤10×8-byte events; `34` reset all. Per-device ones return NOT_FOUND in skjsshid (= "device not attached", SteelSeries' own SSE2Bypass state).
- Engine sends `0xC0DE0004` at start (confirmed). `ObReferenceObjectByHandle` on the caller's event fails in Wine (driver runs in winedevice.exe) → accepted anyway.
- Analysis files: dissassembly via `docs/re-tools/` (KMDF function names via `wdf_annotate.py`). Handler addresses in sshid.sys: dispatcher `0x14000d360`; 0008→`0x1400014b8` (queues via `0x140004438`), timer callback `0x140003cc0`, 0030→`0x140001bb4`, 002C→`0x140001c58`/`0x140005140`.

## Open work on GG, in order
1. **Moments** (clip recording): needs an AAC audio encoder for Media Foundation, then screen capture (DXGI desktop duplication). Only turn Moments on with the user at the PC and check for a full-screen Wine window (clicks stopped working once while it was on; cause not confirmed).
2. **Sonar**: needs Windows' PolicyConfig COM class (`{870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}`) and virtual audio devices (plan: PipeWire sinks).
3. **Aim Tools**: lobby works; the Unity drills (`3D Aim Trainer.exe`) reported working by the user, performance not measured.
4. **`skj-inputd`**: macros and live-preview button suppression (evdev grab + uinput, driven by skjsshid IOCTLs).
5. Other apps: Winamp (plain old app test), Corsair iCUE, Adobe (long term).

## Gotchas learned
- Wine only creates the last folder of `WINEPREFIX` → `mkdir -p` the parent first.
- `WINEDLLPATH` can't override built-in DLLs → SKJ Wine ships a private Wine copy with replaced DLLs.
- Built-in Wine DLLs copied into `system32` are ignored (builtin marker) → replace them in Wine's lib dir.
- Fedora's WineHQ RPM uses `lib64/wine/...`, Ubuntu's uses `lib/wine/...` — find the dir, don't hardcode.
- Disable `winemenubuilder` (its menu entries use the *system* Wine and break).
- wine-mono can't run GG's .NET Framework update-service proxy (Serilog needs `EventLogInvalidDataException`) — not needed, GGEZ is started directly.
- `InstallUtil` on wine-mono: "method not implemented" → use `sc create`.
- Polling-rate change reconnects the mouse; remaps only fully work after **Save**.
- Never redistribute SteelSeries binaries/logos in the repo (icon is extracted on the user's PC).
- "Works in the UI" is not proof for Prism: the Prism page and preview work even when no frame reaches the device. Check the Engine debug log.
- `wineserver -k` needs `WINEPREFIX` set (or use `skj-gg --stop`), otherwise it kills nothing and the old GG keeps running.
- New Wine code: `InitializeCriticalSection` leaves `DebugInfo` = -1 here; don't write `lock.DebugInfo->Spare[0]`.
- A sub-app that dies shows up as `got an exit error from <name>: exit status 0xc0000005` in the Engine log; `WINEDEBUG=+seh` gives the fault address.
- `pgrep -f PATTERN` also matches the shell command that contains PATTERN, so "is it running?" checks typed with the full name always say yes (and `pkill -f` kills the calling shell). Write the pattern so it can't match itself (`'SteelSeriesPris[m]'`) or look at `ps` output.
- GG's Engine verifies the signature of the sub-apps it starts (`VerifyExecutable`), in debug mode too; a modified sub-app exe is refused.
- Proton-GE (GE-Proton11-7 looked at 2026-10-06): its Wine is 11.0 (staging-based), runs outside Steam on Fedora, ships DXVK, vkd3d-proton, dxvk-nvapi and nvidia-libs. It has neither the AAC encoder MFT, PolicyConfig nor IAudioMeterInformation, so it would not fix Moments or Sonar by itself; switching to it means rebuilding every SKJ patch (DLLs + wineserver) against its source.
- A portable wine-staging build (Kron4ek Wine-Builds, `wine-11.19-staging-amd64-wow64`) works with SKJ's prebuilt DLLs and wineserver on top; `install.sh` uses it when `/opt/wine-staging` isn't there.
