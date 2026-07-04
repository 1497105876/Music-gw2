import sys
sys.stdout.reconfigure(encoding='utf-8')

rc_path = r'Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc'

with open(rc_path, 'r', encoding='utf-16-le') as f:
    content = f.read()

# 当前布局：
#   CONTROL "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER|WS_TABSTOP,103,169,120,12
#   COMBOBOX IDC_SPECIAL_KEY_COMBO,226,168,60,100,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP
#   PUSHBUTTON "Set",IDC_SET_BUTTON,290,168,20,14

# 新布局：热键控件恢复原宽度153，下拉框紧贴右边缘（像一个下拉箭头），
# 下拉框宽度很窄只显示箭头，Set按钮恢复原位
# 热键控件 103,169,153,12（恢复原样）
# 下拉框 256,168,14,100（紧贴热键控件右边，只显示一个箭头）
# Set按钮 260,168,50,14（恢复原位，下拉框在它上面/重叠无所谓，z-order下拉框在上）

# 实际上更好：热键控件稍缩短，下拉框紧贴它右边，Set按钮在最后
# 热键控件 103,169,140,12
# 下拉框 245,168,40,100 （紧贴，显示部分文字）
# Set按钮 290,168,20,14

# 不，最简洁：下拉框就是热键控件的一部分——用一个窄下拉框覆盖热键控件右边
# 但这太 hacky 了

# 最直接的方案：去掉下拉框，热键控件恢复原样，改用右键菜单
# 但用户说"下拉选择"——那就在热键控件正下方放一个极窄的下拉箭头按钮

# 算了，用户的意思很明确：就在热键输入框那个位置，既能手动输入也能下拉
# 最干净的实现：热键控件恢复原宽度，旁边放一个很窄的下拉框（只显示箭头）
# 热键控件 103,169,140,12
# 下拉框 245,168,15,100（只显示箭头，紧贴热键控件）
# Set按钮 263,168,47,14

old = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,120,12'
new = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,140,12'
if old in content:
    content = content.replace(old, new)
    print("OK: hotkey control resized to 140")
else:
    print("ERROR: hotkey control not found")

old_combo = 'COMBOBOX        IDC_SPECIAL_KEY_COMBO,226,168,60,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
new_combo = 'COMBOBOX        IDC_SPECIAL_KEY_COMBO,245,168,15,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
if old_combo in content:
    content = content.replace(old_combo, new_combo)
    print("OK: combo moved to 245, width=15")
else:
    print("ERROR: combo not found")

old_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,290,168,20,14'
new_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,263,168,47,14'
if old_btn in content:
    content = content.replace(old_btn, new_btn)
    print("OK: set button restored to 263,47")
else:
    print("ERROR: set button not found")

with open(rc_path, 'w', encoding='utf-16-le') as f:
    f.write(content)

print("Done")
