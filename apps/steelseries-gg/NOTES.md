# SteelSeries GG under SKJ Wine — findings

Tested: GG 120.0.0, wine-staging 11.19 + SKJ patches 0001–0004, 2026-10-04. **Confirmed on real hardware: Aerox 3 Wireless detected, DPI changes work (Fedora 44).**
`setup.sh` + `run.sh` verified end to end on a fresh prefix (about 2 min to set up).

![GG Engine page under SKJ Wine](screenshots/03-engine.png)

## Status

| Piece | What it is | Status |
|---|---|---|
| `SteelSeriesGGEZ.exe` | .NET 8 ASP.NET Core server on `https://127.0.0.1:6327`, starts everything else | **Runs**, TLS with the real installed cert |
| `SteelSeriesGG.exe --hosted` ("oldGG") | Go, GG core | **Runs**, websocket to GGEZ connected |
| `apps/engine/SteelSeriesEngine.exe` | Go, device layer (`SSEdevice.dll` → `hid.dll` + `setupapi`) | **Runs**, serves `/devices`, `/quickset/items`, `/sock` |
| `apps/engine/prism/SteelSeriesPrism.exe` | RGB / lighting | **Runs** |
| `SteelSeriesMoments.exe` | Go, clip recorder | **Runs** |
| `SteelSeriesGGClient.exe` | Electron UI | **Runs**: login → skip → main app → Engine page. The login screen's left hero video stays blank (artwork on other screens is fine) |
| Update service proxy | .NET Framework Windows service | Not used (crashes on wine-mono; GGEZ is started directly) |
| `sshid.sys` | KMDF input filter → `\\.\SSengine` | **Replaced by `drivers/skjsshid`** (WDM, loads in Wine). Engine connects and enables it. Handles GG's *software* features only (macros, remaps, accel/decel, angle snapping), which are not emulated yet; DPI/RGB/polling go over HID |
| `ssdevfactory.sys`, `msihid.sys`, … | other KMDF drivers (virtual devices, MSI boards, PS/2, SMBus) | Can't load, disabled by setup.sh; not needed for mice |

## Problems found and fixes

1. **Installer can't run PowerShell** → no localhost TLS cert. `setup.sh` makes it with openssl, `tools/certinstall` imports it.
2. **`dbconf.yml` missing** in `GG\db` and `GG\apps\engine\db` → GG/Engine panic. Copied by `setup.sh`.
3. **Wine: `CryptAcquireCertificatePrivateKey` ignored `CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG`** → .NET AccessViolation. `patches/wine/0001`.
4. **Wine: `ApplicationData.Current` succeeded for desktop apps** → `Microsoft.Data.Sqlite` crash. `patches/wine/0002`.
5. **Wine: `PFXImportCertStore` stored the wrong key spec (PP_KEYSPEC bitmask) and dropped `CRYPT_MACHINE_KEYSET`** → NTE_BAD_KEYSET / NTE_NO_KEY when opening the cert's key. `patches/wine/0003`.
6. **Wine: Schannel only looked for key containers in HKCU** → TLS server with a LocalMachine cert dropped every connection. `patches/wine/0004`.
7. **GGEZ didn't know about the Engine.** GGEZ starts sub-apps from its own `ggez.db`; the installer's table migration that should copy `engine`/`sonar`/`threeDAT` from `database.db` doesn't run under Wine. `setup.sh` copies the rows.
8. **`shared/guid.json` missing** → written by `setup.sh`.
9. **Wine: SSL chain policy ignored `iPAddress` alt names** → GGEZ's websocket proxy to the GG core (`wss://127.0.0.1:6329/eventing` and `/sock`, .NET `ClientWebSocket`, callback accepts only chain errors) failed with a name mismatch and never retried. No core event reached the sub-apps: Engine never got `deviceConnected`, never created its PrismSync executor (`device executor for device 203 not found`, ~30/s in the debug log) and Prism frames were dropped. `patches/wine/0005`.
10. **Wine: no `IAudioMeterInformation`** → Prism's audio visualizer (`shared/audio-sample-lib.dll`: loopback `IAudioClient` + endpoint peak meter) never started sampling. `patches/wine/0006` implements the meter (own loopback/capture stream, opened on first use). With it Prism holds two PulseAudio capture streams ("SteelSeries Prism") on the output's monitor.
11. **Games can't find GG.** The Engine's GameSense port is random and only written to `coreProps.json` inside GG's prefix; GG's own CS2/Dota 2 setup needs to see the game as the foreground Windows process and Steam in the registry. `bin/skj-gg-gamesense` (started by `skj-gg`, log `~/.cache/skj-wine/gamesense.log`) copies `coreProps.json` into every Steam/Proton and Heroic prefix and writes `gamestate_integration_steelseries.cfg` for native CS2/Dota 2, with the cfg text taken from the user's Engine binary. Checked: `POST /game_metadata`, `/bind_game_event`, `/game_event` from Linux return 200 and reach the Engine; a fake CS2 payload to `/csgo_game_event` is parsed ("CSGO state update event data", "Start of a new round").

## Known leftovers

- Some `tls: unknown certificate` lines from Engine/Moments while the UI starts. The requests then succeed (UI loads devices). Probably the UI connecting before it has received the sub-app's cert. Low priority.
- Login screen hero video doesn't play (likely a codec Wine/Electron lacks here).
- `chaintest.exe` (tools/) confirmed Wine's chain engine reports only `CERT_TRUST_IS_UNTRUSTED_ROOT` for GG's self-signed certs, which is what GGEZ expects.

## Next steps

- [x] **Real Aerox 3 Wireless (2.4 GHz) on Fedora 44: WORKS.** GG detects it (Gear page, 2.4G connected) and **DPI changes apply to the mouse**. Only the udev rule was needed (Wine passes the vendor HID interface through by default). Screenshot: `screenshots/04-aerox3-real.png`
- [x] GG's "Windows Pointer Options" banner: Wine defaults to MouseSpeed=1; setup.sh now sets it to 0
- [x] Battery: shown as bars (fine)
- [x] Illumination/RGB, polling rate (mouse reconnects on change, same as Windows), sleep timer: **work**
- [x] Remap button → keyboard key: **works** (stored on the mouse)
- [x] Remap button → other mouse button (B4→B1): **works once SAVED** (written to the mouse). Only GG's *live preview* sends both B1 and B4 — preview injects B1 and relies on sshid.sys to swallow B4 (not emulated; low priority)
- [ ] Macros: not onboard, need skjsshid injection (same helper)
- [x] Slow UI: Wine's D3D11→OpenGL path. Added `tools/install-dxvk.sh` (DXVK 3.1.1); GG itself doesn't disable HW acceleration (only if windowProps.json says so)
- [x] `sshid.sys` reverse engineered and replaced by `drivers/skjsshid` (see its README)
- [ ] `skj-inputd`: macro playback (+ live-preview suppression, accel/decel) via evdev+uinput, driven by skjsshid
- [x] Packaged: Fedora RPM (`build-rpm.sh`), `skj-gg` launcher, menu entry, autostart
- [x] **Prism** (2026-10-05): onboarding ("Welcome to Prism" → Get Started), canvas with the 3 Aerox zones, effects (Single color, Breathe, ColorShift) and presets apply without errors after patch 0005. Enabling Prism replaces the mouse's own illumination config (same as Windows). *Audio Visualizer* is accepted (uses Wine's PulseAudio loopback, not checked by ear); *Reflect* (screen colours) makes Prism send an error report — needs screen capture, same problem as Moments. Multi-device sync and per-game lighting not testable with one device / before GameSense
- [ ] Real CS2 run still open (CS2 needed an update on 2026-10-05).
- [x] **GameSense** (2026-10-05): API reachable from Linux and from Proton prefixes, CS2 config installed (problem 11). Not yet seen with a real game running: Jakub starts GG, then CS2, and watches the mouse (Apps → Counter-Strike 2 → Configure sets what is shown). Native Linux clients that look for coreProps.json themselves (GameSense Minecraft mod) have no agreed path on Linux — not handled
- [ ] **Audio visualizer does NOT work on the mouse yet** (measured 2026-10-05 evening): with the effect selected every LED frame is `ff ff ff` (white), sound or not; Jakub sees the same. Wine's side is fine: `tools/metertest` shows loopback data and the peak meter following real audio (peaks 0.3–0.4 with a video playing). The fault is on Prism's side: it calls `audio-sample-lib.dll!sample_audio` with one sampling width of **0**, so the library returns nothing (`AudioSampleService.AddActiveDeviceWidth` is never called; `addBaseZoneMapping`, `ProcessRegions`, `renderColors` do run). Next: find where `audiovisualizer.(*service).updateZones` (Prism `0x140406a40`) takes the width from (map built before the `AddActiveDeviceWidth` interface call at `0x140406f51`) and why it stays empty for the Aerox — may also be how GG behaves for a mouse on Windows; unknown. What patch 0006 fixed: without it the library never started (now it captures) — The older Apps → Audio Visualizer (`AudioSync.exe`, Qt) starts, reports "Engine App Active" and stops cleanly
- [ ] The Engine logs `Recovered panic during device_executor.writeBuffers: … 0xfead7007` each time Prism writes while the mouse sleeps (2.4G idle timeout); it recovers by itself when the mouse wakes
- [ ] **Moments, Aim Tools, Sonar need a SteelSeries account** (GG 120): with login skipped the Aim Tools page only shows "Use your SteelSeries account to access Moments, Sonar, game giveaways and more", and Moments refuses to record ("Current user is not logged in, stopping moments", `GET :6327/user` → `loggedIn:false`). Login is in-app (`POST :6327/login`), no browser needed. Found so far without login: Moments enables (`POST <moments>/moments/enable`), `gsdk.dll` 2.5.27.0 loads, monitor list + thumbnail work; capture itself untested
- [x] `ssgg://` links (GG's URL protocol): the desktop entry registers `x-scheme-handler/ssgg`, `skj-gg ssgg://…` passes the link to the running UI or to GGEZ `-deepLink`
- [ ] How to see what the mouse LEDs really get: `WINEDEBUG=+hid,+timestamp`, output reports starting `61 03 00 RR GG BB 01 RR GG BB 02 RR GG BB` are the 3 zone colours (`docs/re-tools/leds.py LOG`). Nothing is written while the mouse sleeps
- [ ] 3D aim trainer, Moments capture, Sonar — after login, see HANDOFF.md
