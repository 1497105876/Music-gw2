import re

path = r"Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc"
with open(path, "r", encoding="utf-16") as f:
    content = f.read()

# 找到 IDD_HOT_KEY_SETTINGS_DIALOG 块
old_block = """BEGIN
    CONTROL         "Enable global hot keys",IDC_HOT_KEY_ENABLE_CHECK,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,7,7,145,8
    CONTROL         "Enable global multimedia keys",IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,161,7,149,8
    CONTROL         "",IDC_HOT_KEY_LIST,"SysListView32",LVS_REPORT | LVS_SINGLESEL | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,7,20,303,142
    RTEXT           "Key:",IDC_TXT_OPT_HOT_KEY_SHORTCUT_KEY_SEL_STATIC,7,171,92,8
    CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,153,12
    PUSHBUTTON      "Set",IDC_SET_BUTTON,260,168,50,14
END"""

new_block = """BEGIN
    CONTROL         "Enable global hot keys",IDC_HOT_KEY_ENABLE_CHECK,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,7,7,145,8
    CONTROL         "Enable global multimedia keys",IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,161,7,149,8
    CONTROL         "",IDC_HOT_KEY_LIST,"SysListView32",LVS_REPORT | LVS_SINGLESEL | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,7,20,303,162
END"""

if old_block in content:
    content = content.replace(old_block, new_block)
    print("Replaced dialog content")
else:
    print("ERROR: old block not found!")
    # 尝试找一下附近的文本
    idx = content.find("IDD_HOT_KEY_SETTINGS_DIALOG DIALOGEX")
    if idx >= 0:
        print("Context around IDD_HOT_KEY_SETTINGS_DIALOG:")
        print(repr(content[idx:idx+800]))
    exit(1)

# 修改 AFX_DIALOG_LAYOUT 块
old_layout = """IDD_HOT_KEY_SETTINGS_DIALOG AFX_DIALOG_LAYOUT
BEGIN
    0,
    0, 0, 50, 0,
    50, 0, 50, 0,
    0, 0, 100, 100,
    0, 100, 76, 0,
    76, 100, 0, 0,
    76, 100, 24, 0
END"""

new_layout = """IDD_HOT_KEY_SETTINGS_DIALOG AFX_DIALOG_LAYOUT
BEGIN
    0,
    0, 0, 50, 0,
    50, 0, 50, 0,
    0, 0, 100, 100
END"""

if old_layout in content:
    content = content.replace(old_layout, new_layout)
    print("Replaced layout")
else:
    print("ERROR: old layout not found!")
    idx = content.find("IDD_HOT_KEY_SETTINGS_DIALOG AFX_DIALOG_LAYOUT")
    if idx >= 0:
        print("Context:")
        print(repr(content[idx:idx+400]))

with open(path, "w", encoding="utf-16") as f:
    f.write(content)

print("Done")
