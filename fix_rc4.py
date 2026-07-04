import sys
sys.stdout.reconfigure(encoding='utf-8')

rc_path = r'Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc'

with open(rc_path, 'r', encoding='utf-16-le') as f:
    content = f.read()

# 去掉单独下拉框，热键控件恢复原宽度153
# 把三个控件行替换成一行热键控件 + Set按钮
old = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,105,12\r\n    COMBOBOX        IDC_SPECIAL_KEY_COMBO,210,168,50,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP\r\n    PUSHBUTTON      "Set",IDC_SET_BUTTON,262,168,48,14'
new = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,153,12\r\n    PUSHBUTTON      "Set",IDC_SET_BUTTON,260,168,50,14'

if old in content:
    content = content.replace(old, new)
    print("OK: removed combo, restored hotkey+button to original")
else:
    # try without \r\n
    old2 = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,105,12\n    COMBOBOX        IDC_SPECIAL_KEY_COMBO,210,168,50,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP\n    PUSHBUTTON      "Set",IDC_SET_BUTTON,262,168,48,14'
    new2 = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,153,12\n    PUSHBUTTON      "Set",IDC_SET_BUTTON,260,168,50,14'
    if old2 in content:
        content = content.replace(old2, new2)
        print("OK: removed combo (LF)")
    else:
        print("ERROR: pattern not found")
        # debug
        import re
        for m in re.finditer(r'IDC_HOTKEY1.*?IDC_SET_BUTTON.*?\d+', content, re.DOTALL):
            print(repr(m.group()))

with open(rc_path, 'w', encoding='utf-16-le') as f:
    f.write(content)
