#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""向 .rc 文件中的 IDD_PLAY_STATISTICS_DIALOG 添加图表控件"""

import re

rc_path = r"Z:\github\MusicPlayer2-gw\MusicPlayer2\MusicPlayer2.rc"

with open(rc_path, "r", encoding="utf-16-le") as f:
    content = f.read()

# 在 IDD_PLAY_STATISTICS_DIALOG 对话框中添加图表控件
old_block = """    CONTROL         "",IDC_STAT_DETAIL_LIST,"SysListView32",LVS_REPORT | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,12,32,476,260
    PUSHBUTTON      "Export CSV",IDC_EXPORT_CSV_BTN,7,300,60,14"""

new_block = """    CONTROL         "",IDC_STAT_DETAIL_LIST,"SysListView32",LVS_REPORT | LVS_ALIGNLEFT | WS_BORDER | WS_TABSTOP,12,32,476,260
    CONTROL         "",IDC_STAT_CHART_STATIC,"Static",SS_BLACKFRAME | SS_NOTIFY,12,32,476,260
    PUSHBUTTON      "Export CSV",IDC_EXPORT_CSV_BTN,7,300,60,14"""

if old_block in content:
    content = content.replace(old_block, new_block, 1)
    with open(rc_path, "w", encoding="utf-16-le") as f:
        f.write(content)
    print("Added chart control to IDD_PLAY_STATISTICS_DIALOG.")
else:
    print("ERROR: Could not find the expected block in .rc file")
    # Try to find it with more flexible matching
    if "IDC_STAT_CHART_STATIC" in content:
        print("Chart control already exists.")
    else:
        # Find the dialog block and add the control
        pattern = r'(IDD_PLAY_STATISTICS_DIALOG DIALOGEX.*?BEGIN\s*\n)(.*?)(\nEND)'
        match = re.search(pattern, content, re.DOTALL)
        if match:
            print("Found dialog block, contents:")
            print(repr(match.group(2)[:500]))
