/* sstest.exe - talks to \\.\SSengine the way SSEdevice.dll does (SKJ Wine test tool) */
#include <windows.h>
#include <stdio.h>

static void ioctl(HANDLE h, DWORD code, void *in, DWORD in_len, void *out, DWORD out_len)
{
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, code, in, in_len, out, out_len, &ret, NULL);
    printf("  %08lx in=%-5lu out=%-3lu -> %s (err %lu, returned %lu)\n", code, in_len, out_len,
           ok ? "OK" : "FAIL", ok ? 0 : GetLastError(), ret);
}

int main(void)
{
    BYTE packet[0x814] = {0}, small[0x40] = {0}, events[0x50];
    HANDLE ev, h = CreateFileW(L"\\\\.\\SSengine", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open \\\\.\\SSengine FAILED, err %lu\n", GetLastError()); return 1; }
    printf("open \\\\.\\SSengine OK\n");
    *(WORD *)packet = 0x1038; *(WORD *)(packet + 2) = 0x1838;   /* Aerox 3 Wireless */
    *(WORD *)small = 0x1038;  *(WORD *)(small + 4) = 0x1838;
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ioctl(h, 0xC0DE0004, NULL, 0, NULL, 0);                 /* enable */
    ioctl(h, 0xC0DE002C, &ev, sizeof(ev), NULL, 0);         /* register event */
    ioctl(h, 0xC0DE0008, packet, sizeof(packet), NULL, 0);  /* queue packet */
    ioctl(h, 0xC0DE0010, small, 4, NULL, 0);                /* flush */
    ioctl(h, 0xC0DE0014, small, sizeof(small), NULL, 0);    /* per-device setter */
    ioctl(h, 0xC0DE0030, NULL, 0, events, sizeof(events));  /* read events */
    ioctl(h, 0xC0DE0034, NULL, 0, NULL, 0);                 /* reset all */
    ioctl(h, 0xC0DE0000, NULL, 0, NULL, 0);                 /* disable */
    CloseHandle(h);
    return 0;
}
