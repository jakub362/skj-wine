# Reverse-engineering helpers

Used to figure out SteelSeries GG's internals (sshid.sys IOCTLs, SSEdevice.dll calls).
Need: `pip install pefile capstone --break-system-packages`, `mingw64-binutils` (Fedora) /
`binutils-mingw-w64` (Debian) for `x86_64-w64-mingw32-objdump`.

| Script | What it does |
|---|---|
| `pedis.py FILE ADDR LEN` | disassemble LEN bytes at virtual ADDR (hex), resolving imports and string refs |
| `ioctls.py FILE [Func ...]` | list call sites of imports (default DeviceIoControl, CreateFileW) with the `edx` constant (= IOCTL code / access) just before |
| `xref.py FILE STRING` | find code that references a string (ASCII or UTF-16) |
| `wdf_annotate.py wdffuncenum.h ASM` | add KMDF function names to an objdump listing of a KMDF driver |
| `fn.py ASM ADDR [N]` | print one function from a listing |

.NET parts of GG (GGEZ, GG.*.dll, update-service proxy) decompile cleanly with
`ilspycmd` (`dotnet tool install -g ilspycmd`; on .NET 8 SDK set `DOTNET_ROLL_FORWARD=Major`):
`ilspycmd -p -o out/ "C:/…/GG.Services.dll"`.
GG's Electron UI: `npx @electron/asar extract resources/app.asar out/`.
