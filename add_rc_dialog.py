#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""向 MusicPlayer2.rc 添加播放统计对话框资源"""

import re

rc_path = r"Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc"

# 读取文件（UTF-16 LE）
with open(rc_path, "r", encoding="utf-16-le") as f:
    content = f.read()

# 检查是否已存在
if "IDD_PLAY_STATISTICS_DIALOG" in content:
    print("IDD_PLAY_STATISTICS_DIALOG already exists in .rc file, skipping.")
else:
    # 在 IDD_LISTEN_TIME_STATISTICS_DLG 对话框定义之后插入新对话框
    # 找到 IDD_LISTEN_TIME_STATISTICS_DLG 的 END
    pattern = r"(IDD_LISTEN_TIME_STATISTICS_DLG\s+DIALOGEX.*?END\s*\n)"
    match = re.search(pattern, content, re.DOTALL)
    if match:
        new_dialog = """
IDD_PLAY_STATISTICS_DIALOG DIALOGEX 0, 0, 500, 320
STYLE DS_SETFONT | DS_MODALFRAME | WS_MAXIMIZEBOX | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "Play Statistics"
FONT 9, "微软雅黑", 400, 0, 0x0
BEGIN
    CONTROL         "",IDC_STAT_TAB,"SysTabControl32",0x0,7,7,486,290
    CONTROL         "",IDC_STAT_OVERVIEW_LIST,"SysListView32",LVS_REPORT | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,12,32,476,260
    CONTROL         "",IDC_STAT_DETAIL_LIST,"SysListView32",LVS_REPORT | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,12,32,476,260
    PUSHBUTTON      "Export CSV",IDC_EXPORT_CSV_BTN,7,300,60,14
    PUSHBUTTON      "Export JSON",IDC_EXPORT_JSON_BTN,75,300,60,14
    DEFPUSHBUTTON   "Close",IDCANCEL,440,300,50,14
END
"""
        content = content[:match.end()] + new_dialog + content[match.end():]
        print("Inserted IDD_PLAY_STATISTICS_DIALOG dialog resource.")
    else:
        print("ERROR: Could not find IDD_LISTEN_TIME_STATISTICS_DLG in .rc file")

# 同时在 AFX_DIALOG_LAYOUT 部分添加布局信息
layout_marker = "IDD_LISTEN_TIME_STATISTICS_DLG AFX_DIALOG_LAYOUT"
if layout_marker in content and "IDD_PLAY_STATISTICS_DIALOG AFX_DIALOG_LAYOUT" not in content:
    layout_entry = "\nIDD_PLAY_STATISTICS_DIALOG AFX_DIALOG_LAYOUT\nBEGIN\n    0\nEND\n"
    # 找到 IDD_LISTEN_TIME_STATISTICS_DLG AFX_DIALOG_LAYOUT 块
    layout_pattern = r"(IDD_LISTEN_TIME_STATISTICS_DLG AFX_DIALOG_LAYOUT\s*\nBEGIN\s*\n\s*0\s*\nEND\s*\n)"
    lmatch = re.search(layout_pattern, content)
    if lmatch:
        content = content[:lmatch.end()] + layout_entry + content[lmatch.end():]
        print("Inserted AFX_DIALOG_LAYOUT for IDD_PLAY_STATISTICS_DIALOG.")

# 写回文件
with open(rc_path, "w", encoding="utf-16-le") as f:
    f.write(content)

print("Done!")
