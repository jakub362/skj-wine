# SteelSeries GG under SKJ Wine — findings

Tested: GG 120.0.0, wine-staging 11.19 + SKJ patches, 2026-10-04.

## How GG is built

| Piece | What it is | Status under SKJ Wine |
|---|---|---|
| `SteelSeriesGGEZ.exe` | .NET 8 ASP.NET Core server on `https://127.0.0.1:6327`, starts the sub-apps | **Runs** (needs patches 0001 + 0002) |
| `SteelSeriesGG.exe` ("oldGG") | Go, the GG core, registers with GGEZ over HTTPS IPC | **Runs**, registers + reports ready |
| `SteelSeriesMoments.exe` | Go, clip recorder | **Runs**, talks to GGEZ |
| `apps/engine/SteelSeriesEngine.exe` | Go, device layer (mice/keyboards). `SSEdevice.dll` uses plain `hid.dll` + `setupapi` | Starts standalone. Not yet launched by oldGG — next step |
| `SteelSeriesGGClient.exe` | Chromium/CEF UI | Not tested yet |
| `updateService/SteelSeriesGGUpdateServiceProxy.exe` | .NET Framework Windows service, launches the Go update service | Crashes on wine-mono (Serilog needs `EventLogInvalidDataException`). Not needed: we start GGEZ directly |
| `sshid.sys`, `ssdevfactory.sys`, … | KMDF kernel drivers (`WDFLDR.SYS`) | Can't load — Wine has no KMDF. `sshid.sys` is a HID *filter* listing the Aerox 3 PIDs (1838, 183A, 1852…). Need to check what breaks without it |

## Problems found and fixes

1. **Installer can't run PowerShell** → `GenerateSelfSignedCertificate.ps1` fails, no TLS cert, empty `GGCertificateThumbprint`.
   Fix: `setup.sh` makes the same cert with openssl and imports it with `tools/certinstall`.
2. **`dbconf.yml` missing** in `GG\db` and `GG\apps\engine\db` → GG and Engine panic at start.
   Fix: copied from `ProgramData` by `setup.sh`.
3. **Wine bug: `CryptAcquireCertificatePrivateKey` ignores `CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG`** → .NET treats a CAPI handle as an NCrypt handle → AccessViolation in `NCryptGetProperty`.
   Fix: `patches/wine/0001`.
4. **Wine bug: `ApplicationData.Current` succeeds for unpackaged apps** (Windows throws "no package identity") → `Microsoft.Data.Sqlite` crashes in its static constructor.
   Fix: `patches/wine/0002`.
5. **Cert private key not usable through CAPI** (`NTE_NO_KEY` 0x8009000d) → GGEZ logs an error and falls back to a temporary cert. Works, but should be fixed (rsaenh key spec after PFX import).

## Next steps

- [ ] Get oldGG to launch `SteelSeriesEngine.exe` (check `database.db` sub-apps / what oldGG waits for).
- [ ] Start `SteelSeriesGGClient.exe` (UI) and see if CEF renders.
- [ ] Test with a real Aerox 3 on Fedora: hidraw passthrough (`DisableHidraw=0` + udev rule for 1038:*), see if Engine finds it without `sshid.sys`.
- [ ] Fix item 5.
- [ ] Turn the patch set into a proper SKJ Wine build (Fedora RPM / tarball).
