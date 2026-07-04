import sys
sys.stdout.reconfigure(encoding='utf-8')

rc_path = r'Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc'

with open(rc_path, 'r', encoding='utf-16-le') as f:
    content = f.read()

# 精确匹配 .rc 文件中的格式
old_hotkey = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,153,12'
new_hotkey = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,120,12'
if old_hotkey in content:
    content = content.replace(old_hotkey, new_hotkey)
    print("OK: hotkey control narrowed")
else:
    print("ERROR: hotkey control not found")
    sys.exit(1)

old_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,260,168,50,14'
new_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,290,168,20,14'
if old_btn in content:
    content = content.replace(old_btn, new_btn)
    print("OK: set button moved")
else:
    print("ERROR: set button not found")
    sys.exit(1)

# 插入下拉框
combo_line = '\n    COMBOBOX        IDC_SPECIAL_KEY_COMBO,226,168,60,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
if 'IDC_SPECIAL_KEY_COMBO' not in content:
    content = content.replace(new_hotkey, new_hotkey + combo_line)
    print("OK: combo box inserted")
else:
    print("INFO: combo already exists")

with open(rc_path, 'w', encoding='utf-16-le') as f:
    f.write(content)

print("Done")
