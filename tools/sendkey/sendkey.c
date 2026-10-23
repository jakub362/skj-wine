/* sendkey.exe: press a key combination inside a Wine prefix, as if typed.
 *   sendkey.exe F8
 *   sendkey.exe ALT+F7
 * Used by bin/skj-hotkeys: the desktop tells us a global shortcut was pressed, this hands the same
 * keys to the Windows programs of the prefix (their low-level keyboard hooks see it). */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static WORD key_code(const char *name)
{
    static const struct { const char *name; WORD vk; } keys[] =
    {
        {"CTRL", VK_CONTROL}, {"CONTROL", VK_CONTROL}, {"SHIFT", VK_SHIFT}, {"ALT", VK_MENU}, {"WIN", VK_LWIN},
        {"SPACE", VK_SPACE}, {"ENTER", VK_RETURN}, {"TAB", VK_TAB}, {"ESC", VK_ESCAPE}, {"HOME", VK_HOME},
        {"END", VK_END}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"PRINTSCREEN", VK_SNAPSHOT}, {"PAUSE", VK_PAUSE},
    };
    unsigned int i;

    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9' && atoi(name + 1) <= 24)
        return VK_F1 + atoi(name + 1) - 1;
    if (!name[1] && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return name[0];
    if (!name[1] && name[0] >= 'a' && name[0] <= 'z') return name[0] - 'a' + 'A';
    for (i = 0; i < ARRAYSIZE(keys); i++) if (!_stricmp(name, keys[i].name)) return keys[i].vk;
    return 0;
}

static void press(WORD vk, BOOL up)
{
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(input));
}

int main(int argc, char **argv)
{
    WORD vks[8];
    int n = 0, i;
    char combo[128], *part, *next;

    if (argc != 2) { fprintf(stderr, "usage: sendkey.exe ALT+F7\n"); return 1; }
    lstrcpynA(combo, argv[1], sizeof(combo));
    for (part = combo; part && n < 8; part = next)
    {
        if ((next = strchr(part, '+'))) *next++ = 0;
        while (*part == ' ') part++;
        if (!(vks[n++] = key_code(part))) { fprintf(stderr, "unknown key %s\n", part); return 1; }
    }
    for (i = 0; i < n; i++) { press(vks[i], FALSE); Sleep(15); }
    Sleep(40);
    for (i = n - 1; i >= 0; i--) { press(vks[i], TRUE); Sleep(15); }
    return 0;
}
