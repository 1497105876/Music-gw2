#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "user32.lib")

struct TestKey {
    const char* name;
    WORD vk;
};

int main() {
    TestKey test_keys[] = {
        {"VK_SPACE (0x20)", VK_SPACE},
        {"VK_RETURN (0x0D)", VK_RETURN},
        {"VK_TAB (0x09)", VK_TAB},
        {"VK_ESCAPE (0x1B)", VK_ESCAPE},
        {"VK_BACK (0x08)", VK_BACK},
        {"VK_DELETE (0x2E)", VK_DELETE},
        {"VK_INSERT (0x2D)", VK_INSERT},
        {"VK_HOME (0x24)", VK_HOME},
        {"VK_END (0x23)", VK_END},
        {"VK_PRIOR PgUp (0x21)", VK_PRIOR},
        {"VK_NEXT PgDn (0x22)", VK_NEXT},
        {"VK_LEFT (0x25)", VK_LEFT},
        {"VK_UP (0x26)", VK_UP},
        {"VK_RIGHT (0x27)", VK_RIGHT},
        {"VK_DOWN (0x28)", VK_DOWN},
        {"VK_CAPITAL (0x14)", VK_CAPITAL},
        {"VK_NUMLOCK (0x90)", VK_NUMLOCK},
        {"VK_SCROLL (0x91)", VK_SCROLL},
        {"VK_SNAPSHOT (0x2C)", VK_SNAPSHOT},
        {"VK_PAUSE (0x13)", VK_PAUSE},
        {"VK_MENU Alt (0x12)", VK_MENU},
        {"VK_F1 (0x70)", VK_F1},
        {"VK_A (0x41)", 0x41},
        {"VK_1 (0x31)", 0x31},
        {"VK_OEM_3 (0xC0)", VK_OEM_3},
    };
    int count = sizeof(test_keys) / sizeof(test_keys[0]);

    printf("=== Windows Hot Key Control Test ===\n\n");

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestHotKey";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "TestHotKey", "t", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandleA(NULL), NULL);
    HWND hHK = CreateWindowExA(0, HOTKEY_CLASSA, "", WS_CHILD, 0, 0, 100, 20, hwnd, NULL, GetModuleHandleA(NULL), NULL);

    if (!hHK) {
        printf("Failed! Error: %lu\n", GetLastError());
        return 1;
    }

    WORD mods = HOTKEYF_CONTROL;

    printf("%-30s  VK     GetKey   GetMod   Match?\n", "Key");
    printf("%-30s  ----   ------   ------   ------\n", "---");

    int fail_count = 0;
    for (int i = 0; i < count; i++) {
        SendMessageA(hHK, HKM_SETHOTKEY, MAKEWORD(test_keys[i].vk, mods), 0);
        LRESULT result = SendMessageA(hHK, HKM_GETHOTKEY, 0, 0);
        WORD got_key = LOBYTE(LOWORD(result));
        WORD got_mods = HIBYTE(LOWORD(result));
        BOOL match = (got_key == test_keys[i].vk && got_mods == mods);
        printf("%-30s  0x%02X   0x%02X     0x%02X      %s\n",
            test_keys[i].name, test_keys[i].vk, got_key, got_mods,
            match ? "OK" : "*** FAIL ***");
        if (!match) fail_count++;
    }

    printf("\nTotal: %d keys, %d failed\n", count, fail_count);

    printf("\n=== RegisterHotKey test ===\n");
    UnregisterHotKey(hwnd, 0xC000);
    BOOL r1 = RegisterHotKey(hwnd, 0xC000, MOD_CONTROL, VK_SPACE);
    printf("RegisterHotKey(MOD_CONTROL, VK_SPACE) = %s\n", r1 ? "SUCCESS" : "FAILED");
    if (r1) UnregisterHotKey(hwnd, 0xC000);

    UnregisterHotKey(hwnd, 0xC001);
    BOOL r2 = RegisterHotKey(hwnd, 0xC001, MOD_CONTROL, VK_RETURN);
    printf("RegisterHotKey(MOD_CONTROL, VK_RETURN) = %s\n", r2 ? "SUCCESS" : "FAILED");
    if (r2) UnregisterHotKey(hwnd, 0xC001);

    DestroyWindow(hHK);
    DestroyWindow(hwnd);
    return 0;
}
