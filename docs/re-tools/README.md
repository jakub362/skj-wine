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
| `gosyms.py EXE > SYMS` | function table (addr size name) of a Go binary (Engine, Prism, SteelSeriesGG.exe, Moments) from its pclntab; stdlib only |
| `godis.py EXE SYMS NAME\|0xADDR` | objdump one Go function, with call targets named and strings shown |
| `gostr.py EXE h:ADDR \| s:ADDR:LEN` | read a Go string header / raw string at a virtual address |
| `callers.py ASM SYMS ADDR...` | functions that reference an address (`objdump -d -M intel --no-show-raw-insn EXE > ASM` first) |
| `cdp.py` | drive GG's Electron UI without touching the mouse: start `SteelSeriesGGClient.exe … --remote-debugging-port=9223 --remote-allow-origins=*`, then `cdp.py list`, `cdp.py app.asar/render/index.html eval\|shot\|click\|clickxy …` |
| `leds.py LOG` | per-second summary of the LED frames actually written to the Aerox 3 Wireless, from a `WINEDEBUG=+hid,+timestamp` log |
| `wslisten.py SECONDS` | print the events on GGEZ's (`:6327`) and the GG core's (`:6329`) `/eventing` websockets side by side |

Live tracing of the Go processes works with plain `gdb -p <pid>` on the Wine process (image base
`0x140000000`, `handle all nostop noprint pass`, breakpoints on addresses from `gosyms.py`).

.NET parts of GG (GGEZ, GG.*.dll, update-service proxy) decompile cleanly with
`ilspycmd` (`dotnet tool install -g ilspycmd`; on .NET 8 SDK set `DOTNET_ROLL_FORWARD=Major`):
`ilspycmd -p -o out/ "C:/…/GG.Services.dll"`.
GG's Electron UI: `npx @electron/asar extract resources/app.asar out/`.
