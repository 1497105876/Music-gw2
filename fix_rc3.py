import sys
sys.stdout.reconfigure(encoding='utf-8')

rc_path = r'Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc'

with open(rc_path, 'r', encoding='utf-16-le') as f:
    content = f.read()

# 当前状态：
#   CONTROL "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER|WS_TABSTOP,103,169,140,12
#   COMBOBOX IDC_SPECIAL_KEY_COMBO,245,168,15,100,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP
#   PUSHBUTTON "Set",IDC_SET_BUTTON,263,168,47,14

# 目标布局：让热键控件和下拉框看起来像一体的
# 对话框总宽 317，右边到 310
# 热键控件 103,169,120,12  （缩短到120）
# 下拉框   225,168,65,100   （紧贴热键控件，宽65能显示"Ctrl + Space"）
# Set按钮  292,168,18,14    （紧贴下拉框，窄一点只显示图标或"S"）
# 实际上Set按钮太窄不好按，调整：
# 热键控件 103,169,110,12
# 下拉框   215,168,65,100
# Set按钮  282,168,28,14

# 不，回到最初的思路：用户说"和原有输入地方重合"
# 那就把下拉框和热键控件放在完全相同的位置，下拉框覆盖在热键控件上面
# 但这样热键控件就没法用了

# 最实际的方案：去掉单独下拉框，把热键控件本身换成 ComboBox
# .rc 里把 "msctls_hotkey32" 换成 COMBOBOX
# 但这样就不是 Hot Key Control 了

# 算了，用户要的就是简单：原来热键控件那么大，现在让它既能输入又能下拉
# 最简单：去掉 .rc 里的 COMBOBOX，恢复热键控件原样
# 然后在 OnInitDialog 里对热键控件做子类化，添加下拉功能
# 但这太复杂

# 最终方案：保留两个控件，但让下拉框覆盖在热键控件上方，看起来是一个控件
# 下拉框用 CBS_DROPDOWN 风格（能输入+能下拉）
# 热键控件在底下，不可见但仍然处理按键
# 不好...

# 好吧最简单的：恢复热键控件原宽度153，去掉下拉框
# 改为在热键控件上右键弹出菜单"Ctrl + Space"
# 不，用户明确说要下拉

# 最终决定：热键控件缩小，下拉框紧贴右边，看起来一体
# 热键控件 103,169,105,12
# 下拉框   210,168,50,100  （紧贴，宽度50显示"Ctrl+Space"够了）
# Set按钮  262,168,48,14   （恢复接近原始大小）

old_hk = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,140,12'
new_hk = 'CONTROL         "",IDC_HOTKEY1,"msctls_hotkey32",WS_BORDER | WS_TABSTOP,103,169,105,12'
if old_hk in content:
    content = content.replace(old_hk, new_hk)
    print("OK: hotkey resized to 105")
else:
    print("ERROR: hotkey not found")

old_combo = 'COMBOBOX        IDC_SPECIAL_KEY_COMBO,245,168,15,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
new_combo = 'COMBOBOX        IDC_SPECIAL_KEY_COMBO,210,168,50,100,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
if old_combo in content:
    content = content.replace(old_combo, new_combo)
    print("OK: combo at 210, width=50")
else:
    print("ERROR: combo not found")

old_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,263,168,47,14'
new_btn = 'PUSHBUTTON      "Set",IDC_SET_BUTTON,262,168,48,14'
if old_btn in content:
    content = content.replace(old_btn, new_btn)
    print("OK: set button at 262,48")
else:
    print("ERROR: set button not found")

with open(rc_path, 'w', encoding='utf-16-le') as f:
    f.write(content)

print("Done")
