// AiSettingDlg.cpp：选项设置里的「AI 设置」页签

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "AiSettingDlg.h"
#include "AiModelEditDlg.h"
#include "AiClient.h"
#include <memory>

IMPLEMENT_DYNAMIC(CAiSettingDlg, CTabDlg)

namespace
{
    // 模型列表的列
    const int kColName = 0;
    const int kColProvider = 1;
    const int kColModel = 2;
    const int kColStatus = 3;

    std::wstring NowText()
    {
        CTime now = CTime::GetCurrentTime();
        wchar_t buf[32];
        swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d",
            now.GetYear(), now.GetMonth(), now.GetDay(), now.GetHour(), now.GetMinute());
        return buf;
    }

    // 历史版本里显示的那一行摘要
    std::wstring OneLineOf(const std::wstring& text)
    {
        std::wstring s;
        for (wchar_t c : text)
        {
            if (c == L'\n' || c == L'\r') { s += L' '; continue; }
            s += c;
        }
        if (s.size() > 40)
        {
            s = s.substr(0, 40);
            s += L"…";
        }
        return s;
    }
}

CAiSettingDlg::CAiSettingDlg(CWnd* pParent /*= nullptr*/)
    : CTabDlg(IDD_AI_SETTING_DIALOG, pParent)
{
}

CAiSettingDlg::~CAiSettingDlg()
{
}

bool CAiSettingDlg::InitializeControls()
{
    return true;
}

void CAiSettingDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_AI_CUR_MODEL_COMBO, m_cur_model_combo);
    DDX_Control(pDX, IDC_AI_MODEL_LIST, m_model_list);
    DDX_Control(pDX, IDC_AI_PROMPT_HIST_LIST, m_hist_list);
    DDX_Control(pDX, IDC_AI_PROXY_COMBO, m_proxy_combo);
    DDX_Control(pDX, IDC_AI_LANG_COMBO, m_lang_combo);
    DDX_Control(pDX, IDC_AI_PROMPT_EDIT, m_prompt_edit);
}

BEGIN_MESSAGE_MAP(CAiSettingDlg, CTabDlg)
    ON_BN_CLICKED(IDC_AI_ENABLE_CHECK, &CAiSettingDlg::OnBnClickedEnable)
    ON_BN_CLICKED(IDC_AI_BTN_ADD, &CAiSettingDlg::OnBnClickedAdd)
    ON_BN_CLICKED(IDC_AI_BTN_EDIT, &CAiSettingDlg::OnBnClickedEdit)
    ON_BN_CLICKED(IDC_AI_BTN_DELETE, &CAiSettingDlg::OnBnClickedDelete)
    ON_BN_CLICKED(IDC_AI_BTN_TEST, &CAiSettingDlg::OnBnClickedTest)
    ON_BN_CLICKED(IDC_AI_BTN_RESET_PROMPT, &CAiSettingDlg::OnBnClickedResetPrompt)
    ON_BN_CLICKED(IDC_AI_BTN_BROWSE, &CAiSettingDlg::OnBnClickedBrowse)
    ON_BN_CLICKED(IDC_AI_SAVE_CHAT_CHECK, &CAiSettingDlg::OnBnClickedSaveChat)
    ON_CBN_SELCHANGE(IDC_AI_CUR_MODEL_COMBO, &CAiSettingDlg::OnCbnSelchangeCurModel)
    ON_NOTIFY(NM_DBLCLK, IDC_AI_MODEL_LIST, &CAiSettingDlg::OnModelListDblClk)
    ON_NOTIFY(NM_CLICK, IDC_AI_PROMPT_HIST_LIST, &CAiSettingDlg::OnHistItemClick)
    ON_MESSAGE(WM_AI_TEST_DONE, &CAiSettingDlg::OnTestDone)
END_MESSAGE_MAP()

BOOL CAiSettingDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    // 进来先拷一份，改的都是副本
    m_data = AiConfig::Get();
    m_initial_prompt = m_data.prompt.system;

    CheckDlgButton(IDC_AI_ENABLE_CHECK, m_data.enabled ? BST_CHECKED : BST_UNCHECKED);

    // ── 模型管理 ──
    InitModelList();
    FillModelList();
    FillCurrentModelCombo();

    // ── 请求 ──
    CheckDlgButton(IDC_AI_STREAM_CHECK, m_data.request.stream ? BST_CHECKED : BST_UNCHECKED);
    wchar_t buf[32];
    swprintf_s(buf, L"%d", m_data.request.retry);
    SetDlgItemTextW(IDC_AI_RETRY_EDIT, buf);

    InitProxyCombo();
    m_proxy_combo.SetCurSel(static_cast<int>(m_data.request.proxy_mode));
    SetDlgItemTextW(IDC_AI_PROXY_URL, m_data.request.proxy_url.c_str());

    // ── 提示词 ──
    m_prompt_edit.SetWindowTextW(m_data.prompt.system.c_str());
    m_lang_combo.AddString(L"跟随界面语言");
    m_lang_combo.AddString(L"始终中文");
    m_lang_combo.AddString(L"始终英文");
    m_lang_combo.SetCurSel(static_cast<int>(m_data.prompt.language));
    InitPromptHistoryList();
    FillPromptHistory();

    // ── 隐私 ──
    CheckDlgButton(IDC_AI_ALLOW_META_CHECK, m_data.privacy.allow_song_meta ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_AI_SAVE_CHAT_CHECK, m_data.privacy.save_chat_history ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(IDC_AI_CHAT_PATH_EDIT, m_data.ChatHistoryDir().c_str());

    m_cur_model_combo.SetMouseWheelEnable(false);
    m_proxy_combo.SetMouseWheelEnable(false);
    m_lang_combo.SetMouseWheelEnable(false);

    UpdateEnabledState();
    return TRUE;
}

// ───────────────────────── 填界面 ─────────────────────────

void CAiSettingDlg::InitModelList()
{
    CRect rect;
    m_model_list.GetWindowRect(rect);
    const int total = rect.Width();
    int w[4];
    w[kColProvider] = theApp.DPI(70);
    w[kColStatus] = theApp.DPI(70);
    w[kColModel] = theApp.DPI(96);
    w[kColName] = total - w[kColProvider] - w[kColModel] - w[kColStatus] - theApp.DPI(8);
    if (w[kColName] < theApp.DPI(60)) w[kColName] = theApp.DPI(60);

    m_model_list.InsertColumn(kColName, L"备注名", LVCFMT_LEFT, w[kColName]);
    m_model_list.InsertColumn(kColProvider, L"服务商", LVCFMT_LEFT, w[kColProvider]);
    m_model_list.InsertColumn(kColModel, L"模型", LVCFMT_LEFT, w[kColModel]);
    m_model_list.InsertColumn(kColStatus, L"状态", LVCFMT_LEFT, w[kColStatus]);
    m_model_list.SetExtendedStyle(m_model_list.GetExtendedStyle()
        | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
}

void CAiSettingDlg::FillModelList()
{
    m_model_list.SetRedraw(FALSE);
    m_model_list.DeleteAllItems();
    for (size_t i = 0; i < m_data.models.size(); ++i)
    {
        const AiModelConfig& m = m_data.models[i];
        std::wstring display = m.DisplayName();
        if (m.id == m_data.current_model_id)
            display += L"（当前）";
        int row = m_model_list.InsertItem(static_cast<int>(i), display.c_str());
        if (row < 0) continue;
        m_model_list.SetItemText(row, kColProvider, m.ProviderName().c_str());
        m_model_list.SetItemText(row, kColModel, m.model.c_str());
        m_model_list.SetItemText(row, kColStatus, L"");
    }
    m_model_list.SetRedraw(TRUE);
    m_model_list.Invalidate();
}

void CAiSettingDlg::FillCurrentModelCombo()
{
    m_cur_model_combo.ResetContent();
    int cur_index = -1;
    for (size_t i = 0; i < m_data.models.size(); ++i)
    {
        const AiModelConfig& m = m_data.models[i];
        std::wstring label = m.DisplayName() + L" · " + m.model;
        m_cur_model_combo.AddString(label.c_str());
        if (m.id == m_data.current_model_id)
            cur_index = static_cast<int>(i);
    }
    if (m_data.models.empty())
    {
        m_cur_model_combo.AddString(L"（还没添加模型）");
        m_cur_model_combo.SetCurSel(0);
    }
    else
    {
        m_cur_model_combo.SetCurSel(cur_index >= 0 ? cur_index : 0);
    }
}

void CAiSettingDlg::InitProxyCombo()
{
    m_proxy_combo.ResetContent();
    m_proxy_combo.AddString(L"不使用代理");
    m_proxy_combo.AddString(L"跟随系统设置");
    m_proxy_combo.AddString(L"手动填写");
}

void CAiSettingDlg::InitPromptHistoryList()
{
    CRect rect;
    m_hist_list.GetWindowRect(rect);
    int w_time = theApp.DPI(96);
    int w_text = rect.Width() - w_time - theApp.DPI(8);
    if (w_text < theApp.DPI(80)) w_text = theApp.DPI(80);
    m_hist_list.InsertColumn(0, L"时间", LVCFMT_LEFT, w_time);
    m_hist_list.InsertColumn(1, L"内容", LVCFMT_LEFT, w_text);
    m_hist_list.SetExtendedStyle(m_hist_list.GetExtendedStyle()
        | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
}

void CAiSettingDlg::FillPromptHistory()
{
    m_hist_list.DeleteAllItems();
    for (size_t i = 0; i < m_data.prompt.history.size(); ++i)
    {
        int row = m_hist_list.InsertItem(static_cast<int>(i), m_data.prompt.history[i].time.c_str());
        if (row < 0) continue;
        m_hist_list.SetItemText(row, 1, OneLineOf(m_data.prompt.history[i].text).c_str());
    }
}

void CAiSettingDlg::UpdateEnabledState()
{
    const bool on = (IsDlgButtonChecked(IDC_AI_ENABLE_CHECK) != 0);
    const bool save_chat = (IsDlgButtonChecked(IDC_AI_SAVE_CHAT_CHECK) != 0);

    const int ids[] = {
        IDC_AI_CUR_MODEL_COMBO, IDC_AI_BTN_TEST, IDC_AI_MODEL_LIST,
        IDC_AI_BTN_ADD, IDC_AI_BTN_EDIT, IDC_AI_BTN_DELETE,
        IDC_AI_STREAM_CHECK, IDC_AI_RETRY_EDIT, IDC_AI_PROXY_COMBO, IDC_AI_PROXY_URL,
        IDC_AI_PROMPT_EDIT, IDC_AI_BTN_RESET_PROMPT, IDC_AI_PROMPT_HIST_LIST, IDC_AI_LANG_COMBO,
        IDC_AI_ALLOW_META_CHECK, IDC_AI_SAVE_CHAT_CHECK
    };
    for (int id : ids)
    {
        if (CWnd* pWnd = GetDlgItem(id))
            pWnd->EnableWindow(on);
    }

    // 保存位置只在「对话记录存到本地」勾上时才可用
    if (CWnd* pWnd = GetDlgItem(IDC_AI_CHAT_PATH_EDIT))
        pWnd->EnableWindow(on && save_chat);
    if (CWnd* pWnd = GetDlgItem(IDC_AI_BTN_BROWSE))
        pWnd->EnableWindow(on && save_chat);
}

int CAiSettingDlg::SelectedModelIndex() const
{
    POSITION pos = m_model_list.GetFirstSelectedItemPosition();
    if (pos == NULL)
        return -1;
    return m_model_list.GetNextSelectedItem(pos);
}

// ───────────────────────── 交互 ─────────────────────────

void CAiSettingDlg::OnBnClickedEnable()
{
    UpdateEnabledState();
}

void CAiSettingDlg::OnBnClickedSaveChat()
{
    UpdateEnabledState();
}

void CAiSettingDlg::OnCbnSelchangeCurModel()
{
    int index = m_cur_model_combo.GetCurSel();
    if (index < 0 || index >= static_cast<int>(m_data.models.size()))
        return;
    m_data.current_model_id = m_data.models[index].id;
    FillModelList();
    SetDlgItemTextW(IDC_AI_CONN_STATUS, L"未测试");
}

void CAiSettingDlg::OnBnClickedAdd()
{
    AiModelConfig m;
    m.id = m_data.NewModelId();
    m.provider = L"zhipu";      // 默认给个国内直连的
    const AiProviderPreset* preset = AiFindPreset(m.provider);
    m.base_url = preset->base_url;
    m.model = preset->model;

    CAiModelEditDlg dlg(m, m_data.request, this);
    if (dlg.DoModal() == IDOK)
    {
        m_data.models.push_back(dlg.GetModel());
        if (m_data.current_model_id.empty())
            m_data.current_model_id = m_data.models.back().id;
        FillModelList();
        FillCurrentModelCombo();
    }
}

void CAiSettingDlg::OnBnClickedEdit()
{
    int index = SelectedModelIndex();
    if (index < 0 || index >= static_cast<int>(m_data.models.size()))
    {
        MessageBox(L"先在上面选中一套模型。", L"AI 设置", MB_ICONINFORMATION);
        return;
    }
    CAiModelEditDlg dlg(m_data.models[index], m_data.request, this);
    if (dlg.DoModal() == IDOK)
    {
        m_data.models[index] = dlg.GetModel();
        FillModelList();
        FillCurrentModelCombo();
        SetDlgItemTextW(IDC_AI_CONN_STATUS, L"未测试");
    }
}

void CAiSettingDlg::OnBnClickedDelete()
{
    int index = SelectedModelIndex();
    if (index < 0 || index >= static_cast<int>(m_data.models.size()))
    {
        MessageBox(L"先在上面选中一套模型。", L"AI 设置", MB_ICONINFORMATION);
        return;
    }
    const std::wstring name = m_data.models[index].DisplayName();
    if (MessageBox((L"确定删掉「" + name + L"」吗？").c_str(), L"AI 设置", MB_ICONQUESTION | MB_YESNO) != IDYES)
        return;

    const std::wstring removed_id = m_data.models[index].id;
    m_data.models.erase(m_data.models.begin() + index);
    if (m_data.current_model_id == removed_id)
    {
        m_data.current_model_id = m_data.models.empty() ? L"" : m_data.models.front().id;
        m_gen++;        // 让正在跑的测试作废
        SetDlgItemTextW(IDC_AI_CONN_STATUS, L"未测试");
    }
    FillModelList();
    FillCurrentModelCombo();
}

void CAiSettingDlg::OnModelListDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pResult != nullptr) *pResult = 0;
    // 双击直接把它设成「当前使用」，省一步
    int index = SelectedModelIndex();
    if (index < 0 || index >= static_cast<int>(m_data.models.size()))
        return;
    m_data.current_model_id = m_data.models[index].id;
    FillModelList();
    FillCurrentModelCombo();
    SetDlgItemTextW(IDC_AI_CONN_STATUS, L"未测试");
}

void CAiSettingDlg::OnBnClickedTest()
{
    if (m_testing)
        return;

    const AiModelConfig* model = m_data.CurrentModel();
    if (model == nullptr)
    {
        SetDlgItemTextW(IDC_AI_CONN_STATUS, L"还没有可用的模型");
        return;
    }
    std::wstring why;
    AiCallParams params = AiCallParams::FromModel(*model, m_data.request);
    if (!params.Valid(why))
    {
        SetDlgItemTextW(IDC_AI_CONN_STATUS, (L"配置不完整：" + why).c_str());
        return;
    }

    m_testing = true;
    GetDlgItem(IDC_AI_BTN_TEST)->EnableWindow(FALSE);
    SetDlgItemTextW(IDC_AI_BTN_TEST, L"测试中…");
    SetDlgItemTextW(IDC_AI_CONN_STATUS, L"正在发一次最小请求…");
    m_gen++;
    AiStartTestJob(GetSafeHwnd(), m_gen, params);
}

afx_msg LRESULT CAiSettingDlg::OnTestDone(WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<AiCallResult> result(reinterpret_cast<AiCallResult*>(lParam));
    if (static_cast<int>(wParam) != m_gen)
        return 0;

    m_testing = false;
    GetDlgItem(IDC_AI_BTN_TEST)->EnableWindow(TRUE);
    SetDlgItemTextW(IDC_AI_BTN_TEST, L"测试连接");

    if (result->ok)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"刚刚测试：正常 · %d 毫秒", result->elapsed_ms);
        SetDlgItemTextW(IDC_AI_CONN_STATUS, buf);
    }
    else
    {
        std::wstring text = L"测试失败：" + result->error;
        SetDlgItemTextW(IDC_AI_CONN_STATUS, text.c_str());
    }
    return 0;
}

void CAiSettingDlg::OnBnClickedResetPrompt()
{
    m_prompt_edit.SetWindowTextW(AiConfig::DefaultSystemPrompt().c_str());
}

void CAiSettingDlg::OnHistItemClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pResult != nullptr) *pResult = 0;
    POSITION pos = m_hist_list.GetFirstSelectedItemPosition();
    if (pos == NULL)
        return;
    int index = m_hist_list.GetNextSelectedItem(pos);
    if (index < 0 || index >= static_cast<int>(m_data.prompt.history.size()))
        return;
    m_prompt_edit.SetWindowTextW(m_data.prompt.history[index].text.c_str());
}

void CAiSettingDlg::OnBnClickedBrowse()
{
    CFolderPickerDialog dlg(nullptr, 0, this, 0);
    if (dlg.DoModal() == IDOK)
    {
        CString path = dlg.GetPathName();
        if (path.GetLength() > 0)
        {
            if (path.GetAt(path.GetLength() - 1) != L'\\')
                path += L"\\";
            SetDlgItemTextW(IDC_AI_CHAT_PATH_EDIT, path);
        }
    }
}

void CAiSettingDlg::PushPromptHistory(const std::wstring& text)
{
    if (text.empty())
        return;
    AiPromptHistoryItem item;
    item.time = NowText();
    item.text = text;
    m_data.prompt.history.insert(m_data.prompt.history.begin(), item);
    if (m_data.prompt.history.size() > 3)
        m_data.prompt.history.resize(3);
}

void CAiSettingDlg::GetDataFromUi()
{
    m_data.enabled = (IsDlgButtonChecked(IDC_AI_ENABLE_CHECK) != 0);

    // 当前使用的模型：下拉里选的就是列表里对应那一套
    int cur_sel = m_cur_model_combo.GetCurSel();
    if (cur_sel >= 0 && cur_sel < static_cast<int>(m_data.models.size()))
        m_data.current_model_id = m_data.models[cur_sel].id;

    m_data.request.stream = (IsDlgButtonChecked(IDC_AI_STREAM_CHECK) != 0);
    CString tmp;
    GetDlgItemTextW(IDC_AI_RETRY_EDIT, tmp);
    int retry = _wtoi(tmp.GetString());
    if (retry < 0) retry = 0;
    if (retry > 5) retry = 5;
    m_data.request.retry = retry;
    m_data.request.proxy_mode = static_cast<AiProxyMode>(m_proxy_combo.GetCurSel());
    GetDlgItemTextW(IDC_AI_PROXY_URL, tmp);
    m_data.request.proxy_url = tmp.GetString();

    // 系统提示词：改动了就把改之前那版塞进历史（只留最近 3 条）
    CString prompt_text;
    m_prompt_edit.GetWindowTextW(prompt_text);
    std::wstring new_prompt = prompt_text.GetString();
    if (new_prompt != m_initial_prompt)
        PushPromptHistory(m_initial_prompt);
    m_data.prompt.system = new_prompt;
    m_initial_prompt = new_prompt;
    m_data.prompt.language = static_cast<AiAnswerLanguage>(m_lang_combo.GetCurSel());
    FillPromptHistory();

    m_data.privacy.allow_song_meta = (IsDlgButtonChecked(IDC_AI_ALLOW_META_CHECK) != 0);
    m_data.privacy.save_chat_history = (IsDlgButtonChecked(IDC_AI_SAVE_CHAT_CHECK) != 0);
    GetDlgItemTextW(IDC_AI_CHAT_PATH_EDIT, tmp);
    m_data.privacy.chat_history_dir = tmp.GetString();

    // 只有点「确定 / 应用」才真正写回全局
    AiConfig::Get() = m_data;
}
