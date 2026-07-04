// CHotKeySettingDlg.cpp: 实现文件
//

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "CHotKeySettingDlg.h"
#include "WinVersionHelper.h"


// CHotKeySettingDlg 对话框

IMPLEMENT_DYNAMIC(CHotKeySettingDlg, CTabDlg)

CHotKeySettingDlg::CHotKeySettingDlg(CWnd* pParent /*=nullptr*/)
    : CTabDlg(IDD_HOT_KEY_SETTINGS_DIALOG, pParent)
{

}

CHotKeySettingDlg::~CHotKeySettingDlg()
{
}

void CHotKeySettingDlg::ShowKeyList()
{
    int index = 0;
    for (int i = HK_PLAY_PAUSE; i < HK_MAX; i++)
    {
        m_key_list.SetItemText(index, 1, m_hotkey_group[static_cast<eHotKeyId>(i)].GetHotkeyName().c_str());
        index++;
    }
}

void CHotKeySettingDlg::EnableControl()
{
    m_key_list.EnableWindow(m_data.hot_key_enable);
}

void CHotKeySettingDlg::ListClicked()
{
    EnableControl();
}

bool CHotKeySettingDlg::InitializeControls()
{
    wstring temp;
    temp = theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_HOOK_SHORTCUT_KEY_ENABLE");
    SetDlgItemTextW(IDC_HOT_KEY_ENABLE_CHECK, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_HOOK_MULTI_MEDIA_KEY_ENABLE");
    SetDlgItemTextW(IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_SHORTCUT_KEY_SEL");
    SetDlgItemTextW(IDC_TXT_OPT_HOT_KEY_SHORTCUT_KEY_SEL_STATIC, temp.c_str());

    return false;
}

void CHotKeySettingDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_HOT_KEY_LIST, m_key_list);
    DDX_Control(pDX, IDC_HOT_KEY_ENABLE_CHECK, m_hot_key_enable_check);
    DDX_Control(pDX, IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK, m_enable_global_multimedia_key_check);
}


BEGIN_MESSAGE_MAP(CHotKeySettingDlg, CTabDlg)
    ON_NOTIFY(NM_CLICK, IDC_HOT_KEY_LIST, &CHotKeySettingDlg::OnNMClickHotKeyList)
    ON_NOTIFY(NM_RCLICK, IDC_HOT_KEY_LIST, &CHotKeySettingDlg::OnNMRClickHotKeyList)
    ON_BN_CLICKED(IDC_HOT_KEY_ENABLE_CHECK, &CHotKeySettingDlg::OnBnClickedHotKeyEnableCheck)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK, &CHotKeySettingDlg::OnBnClickedEnableGlobalMultimediaKeyCheck)
END_MESSAGE_MAP()


// CHotKeySettingDlg 消息处理程序


BOOL CHotKeySettingDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    m_hot_key_enable_check.SetCheck(m_data.hot_key_enable);
    m_enable_global_multimedia_key_check.SetCheck(m_data.global_multimedia_key_enable);
    if (CWinVersionHelper::IsWindows81OrLater())
        m_enable_global_multimedia_key_check.ShowWindow(SW_HIDE);

    m_toolTip.Create(this);
    m_toolTip.SetMaxTipWidth(theApp.DPI(300));
    m_toolTip.AddTool(GetDlgItem(IDC_ENABLE_GLOBAL_MULTIMEDIA_KEY_CHECK), theApp.m_str_table.LoadText(L"TIP_OPT_HOT_KEY_HOOK_MULTI_MEDIA_KEY").c_str());
    m_toolTip.SetWindowPos(&CWnd::wndTopMost, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

    CRect rect;
    m_key_list.GetWindowRect(rect);
    int width0 = theApp.DPI(180);
    int width1 = rect.Width() - width0 - theApp.DPI(20) - 1;
    m_key_list.SetExtendedStyle(m_key_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    m_key_list.InsertColumn(0, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_FUNCTION").c_str(), LVCFMT_LEFT, width0);
    m_key_list.InsertColumn(1, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_SHORTCUT_KEY").c_str(), LVCFMT_LEFT, width1);

    m_key_list.InsertItem(0, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_PLAY_PAUSE").c_str());
    m_key_list.InsertItem(1, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_STOP").c_str());
    m_key_list.InsertItem(2, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_FAST_FORWARD").c_str());
    m_key_list.InsertItem(3, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_REWIND").c_str());
    m_key_list.InsertItem(4, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_PREVIOUS").c_str());
    m_key_list.InsertItem(5, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_NEXT").c_str());
    m_key_list.InsertItem(6, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_VOLUME_UP").c_str());
    m_key_list.InsertItem(7, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_VOLUME_DOWN").c_str());
    m_key_list.InsertItem(8, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_EXIT").c_str());
    m_key_list.InsertItem(9, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_PLAYER_SHOW_HIDE").c_str());
    m_key_list.InsertItem(10, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_DESKTOP_LYRIC_SHOW_HIDE").c_str());
    m_key_list.InsertItem(11, theApp.m_str_table.LoadText(L"TXT_OPT_HOT_KEY_ADD_TO_MY_FAVOURITE").c_str());

    ShowKeyList();

    EnableControl();

    return TRUE;
}


void CHotKeySettingDlg::OnNMClickHotKeyList(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    m_item_selected = pNMItemActivate->iItem;

    // 如果点击的是第二列（快捷键列），进入捕获模式
    if (m_item_selected >= 0 && pNMItemActivate->iSubItem == 1 && m_data.hot_key_enable)
    {
        StartCapture();
    }
    else
    {
        EndCapture(false);
        ListClicked();
    }
    *pResult = 0;
}


void CHotKeySettingDlg::OnNMRClickHotKeyList(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
    m_item_selected = pNMItemActivate->iItem;

    // 右键点击：清除该行的快捷键
    if (m_item_selected >= 0 && pNMItemActivate->iSubItem == 1 && m_data.hot_key_enable)
    {
        eHotKeyId key_id = static_cast<eHotKeyId>(m_item_selected + HK_PLAY_PAUSE);
        m_hotkey_group[key_id].Clear();
        ShowKeyList();
    }

    EndCapture(false);
    ListClicked();
    *pResult = 0;
}


void CHotKeySettingDlg::StartCapture()
{
    if (m_item_selected < 0)
        return;

    m_capturing = true;
    eHotKeyId key_id = static_cast<eHotKeyId>(m_item_selected + HK_PLAY_PAUSE);
    m_capture_backup = m_hotkey_group[key_id];

    // 选中该行并显示提示
    m_key_list.SetItemState(m_item_selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_key_list.SetItemText(m_item_selected, 1, L"按下快捷键... (Esc取消)");

    // 把焦点设到对话框自身，确保 PreTranslateMessage 能收到键盘消息
    SetFocus();
}


void CHotKeySettingDlg::EndCapture(bool apply)
{
    if (!m_capturing)
        return;

    m_capturing = false;

    if (!apply && m_item_selected >= 0)
    {
        // 取消，恢复原值
        eHotKeyId key_id = static_cast<eHotKeyId>(m_item_selected + HK_PLAY_PAUSE);
        m_hotkey_group[key_id] = m_capture_backup;
    }

    ShowKeyList();
}


void CHotKeySettingDlg::OnBnClickedHotKeyEnableCheck()
{
    m_data.hot_key_enable = (m_hot_key_enable_check.GetCheck() != 0);
    EndCapture(false);
    EnableControl();
}


void CHotKeySettingDlg::OnBnClickedEnableGlobalMultimediaKeyCheck()
{
    m_data.global_multimedia_key_enable = (m_enable_global_multimedia_key_check.GetCheck() != 0);
}


BOOL CHotKeySettingDlg::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_MOUSEMOVE)
        m_toolTip.RelayEvent(pMsg);

    // 捕获模式：拦截所有键盘消息
    if (m_capturing && m_item_selected >= 0)
    {
        // 处理 WM_KEYDOWN 和 WM_SYSKEYDOWN（Alt 组合键走 SYS 路径）
        if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
        {
            int vk = (int)pMsg->wParam;

            // Esc：取消
            if (vk == VK_ESCAPE)
            {
                EndCapture(false);
                return TRUE;
            }

            // 回车：确认
            if (vk == VK_RETURN)
            {
                EndCapture(true);
                return TRUE;
            }

            // Tab：取消捕获，让焦点正常切换
            if (vk == VK_TAB)
            {
                EndCapture(false);
                return CTabDlg::PreTranslateMessage(pMsg);
            }

            // 单独修饰键不算，等用户按实际键
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_LMENU || vk == VK_RMENU ||
                vk == VK_LWIN || vk == VK_RWIN)
            {
                return TRUE;  // 吃掉但不处理，等下一个按键
            }

            // 读取修饰键状态
            WORD modifiers = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000)
                modifiers |= HOTKEYF_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000)
                modifiers |= HOTKEYF_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000)
                modifiers |= HOTKEYF_ALT;

            // 没有修饰键的单独按键不给设（避免设成单个字母之类的）
            if (modifiers == 0)
            {
                // F1-F12 允许不带修饰键
                if (vk >= VK_F1 && vk <= VK_F24)
                {
                    // OK，继续往下设入
                }
                else
                {
                    m_key_list.SetItemText(m_item_selected, 1, L"需要组合键 (Ctrl/Shift/Alt+键)");
                    return TRUE;
                }
            }

            // 写入热键
            CHotKey hot_key;
            hot_key.key = (short)vk;
            hot_key.ctrl = ((modifiers & HOTKEYF_CONTROL) != 0);
            hot_key.shift = ((modifiers & HOTKEYF_SHIFT) != 0);
            hot_key.alt = ((modifiers & HOTKEYF_ALT) != 0);

            eHotKeyId key_id = static_cast<eHotKeyId>(m_item_selected + HK_PLAY_PAUSE);
            m_hotkey_group[key_id] = hot_key;

            EndCapture(true);
            return TRUE;
        }

        // 捕获模式下吃掉其余键盘消息
        if (pMsg->message == WM_CHAR || pMsg->message == WM_SYSCHAR ||
            pMsg->message == WM_KEYUP || pMsg->message == WM_SYSKEYUP ||
            pMsg->message == WM_DEADCHAR || pMsg->message == WM_SYSDEADCHAR)
        {
            return TRUE;
        }
    }

    return CTabDlg::PreTranslateMessage(pMsg);
}
