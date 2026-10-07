/* winlist.exe: what windows are open in this prefix, and what do they say?
 * Prints every visible top-level window (class, title, size) with the text of its controls -
 * enough to read an error box without looking at the screen.
 *   winlist.exe            list
 *   winlist.exe close      also ask every listed dialog (#32770) to close */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int close_dialogs;

static BOOL CALLBACK child(HWND hwnd, LPARAM lparam)
{
    WCHAR text[1024], class[64];
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetClassNameW(hwnd, class, 64);
    text[0] = 0;
    SendMessageTimeoutW(hwnd, WM_GETTEXT, 1024, (LPARAM)text, SMTO_ABORTIFHUNG, 500, NULL);
    if (text[0]) printf("    [%ls] %ls\n", class, text);
    return TRUE;
}

static BOOL CALLBACK top(HWND hwnd, LPARAM lparam)
{
    WCHAR title[512], class[64];
    DWORD pid;
    RECT r;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetClassNameW(hwnd, class, 64);
    GetWindowTextW(hwnd, title, 512);
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);
    if (r.right - r.left < 8 || !wcscmp(class, L"__wine_x11_ime")) return TRUE;
    printf("%p pid %04lx [%ls] \"%ls\" %ldx%ld\n", hwnd, pid, class, title, r.right - r.left, r.bottom - r.top);
    EnumChildWindows(hwnd, child, 0);
    if (close_dialogs && !wcscmp(class, L"#32770")) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

int main(int argc, char **argv)
{
    close_dialogs = argc > 1 && !strcmp(argv[1], "close");
    EnumWindows(top, 0);
    return 0;
}
