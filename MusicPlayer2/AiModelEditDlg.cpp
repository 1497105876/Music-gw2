// AiModelEditDlg.cpp：新增 / 编辑一套模型配置

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "AiModelEditDlg.h"
#include "AiClient.h"
#include <memory>

IMPLEMENT_DYNAMIC(CAiModelEditDlg, CBaseDialog)

CAiModelEditDlg::CAiModelEditDlg(const AiModelConfig& model, const AiRequestConfig& req, CWnd* pParent /*= nullptr*/)
    : CBaseDialog(IDD_AI_MODEL_EDIT_DIALOG, pParent)
    , m_model(model)
    , m_req(req)
{
}

CAiModelEditDlg::~CAiModelEditDlg()
{
}

CString CAiModelEditDlg::GetDialogName() const
{
    // 说明/录入类的小对话框不参与「记住大小」，返回空
    return CString();
}

bool CAiModelEditDlg::InitializeControls()
{
    SetWindowTextW(m_model.id.empty() ? L"添加模型" : L"编辑模型");
    return true;
}

void CAiModelEditDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_AI_EDIT_PROVIDER, m_provider_combo);
    DDX_Control(pDX, IDC_AI_EDIT_MODEL, m_model_combo);
}

BEGIN_MESSAGE_MAP(CAiModelEditDlg, CBaseDialog)
    ON_CBN_SELCHANGE(IDC_AI_EDIT_PROVIDER, &CAiModelEditDlg::OnCbnSelchangeProvider)
    ON_BN_CLICKED(IDC_AI_BTN_FETCH, &CAiModelEditDlg::OnBnClickedFetch)
    ON_BN_CLICKED(IDC_AI_BTN_TEST_EDIT, &CAiModelEditDlg::OnBnClickedTest)
    ON_MESSAGE(WM_AI_MODELS_DONE, &CAiModelEditDlg::OnModelsDone)
    ON_MESSAGE(WM_AI_TEST_DONE, &CAiModelEditDlg::OnTestDone)
END_MESSAGE_MAP()

BOOL CAiModelEditDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    FillProviderCombo();

    SetDlgItemTextW(IDC_AI_EDIT_NAME, m_model.name.c_str());
    SetDlgItemTextW(IDC_AI_EDIT_URL, m_model.base_url.c_str());
    SetDlgItemTextW(IDC_AI_EDIT_KEY, m_model.api_key.c_str());

    // 模型名用可手填的下拉（CBS_DROPDOWN），拉到的列表塞进候选里，也能直接敲
    if (!m_model.model.empty())
        m_model_combo.AddString(m_model.model.c_str());
    m_model_combo.SetCurSel(0);
    // 编辑框里的文字要能整段选中，默认给足宽度
    m_model_combo.SetDroppedWidth(theApp.DPI(220));
    m_model_combo.SetMouseWheelEnable(false);
    m_provider_combo.SetMouseWheelEnable(false);

    wchar_t buf[32];
    swprintf_s(buf, L"%.2g", m_model.temperature);
    SetDlgItemTextW(IDC_AI_EDIT_TEMP, buf);
    swprintf_s(buf, L"%.2g", m_model.top_p);
    SetDlgItemTextW(IDC_AI_EDIT_TOPP, buf);
    swprintf_s(buf, L"%d", m_model.max_tokens);
    SetDlgItemTextW(IDC_AI_EDIT_MAXTOK, buf);
    swprintf_s(buf, L"%d", m_model.timeout_sec);
    SetDlgItemTextW(IDC_AI_EDIT_TIMEOUT, buf);

    SetHint(L"");
    return TRUE;
}

void CAiModelEditDlg::FillProviderCombo()
{
    int count = 0;
    const AiProviderPreset* presets = AiGetPresets(count);
    for (int i = 0; i < count; ++i)
    {
        m_provider_combo.AddString(presets[i].name);
        if (m_model.provider == presets[i].key)
            m_provider_combo.SetCurSel(i);
    }
    if (m_provider_combo.GetCurSel() < 0)
        m_provider_combo.SetCurSel(count - 1);      // 兜底：自定义
}

void CAiModelEditDlg::OnCbnSelchangeProvider()
{
    int index = m_provider_combo.GetCurSel();
    if (index < 0)
        return;
    int count = 0;
    const AiProviderPreset* presets = AiGetPresets(count);
    if (index >= count)
        return;

    m_model.provider = presets[index].key;
    // 自定义没有默认值，别把用户填的清掉
    if (presets[index].base_url[0] != L'\0')
        SetDlgItemTextW(IDC_AI_EDIT_URL, presets[index].base_url);
    if (presets[index].model[0] != L'\0')
    {
        m_model_combo.SetWindowTextW(presets[index].model);
        SetHint(L"");
    }
}

void CAiModelEditDlg::SetHint(const std::wstring& text)
{
    SetDlgItemTextW(IDC_AI_EDIT_HINT, text.c_str());
}

bool CAiModelEditDlg::CollectFromUi(std::wstring& why)
{
    CString tmp;
    GetDlgItemTextW(IDC_AI_EDIT_NAME, tmp);
    m_model.name = tmp.GetString();

    GetDlgItemTextW(IDC_AI_EDIT_URL, tmp);
    m_model.base_url = tmp.GetString();
    // 地址结尾的斜杠去掉，后面拼 /chat/completions 才不会双斜杠
    while (!m_model.base_url.empty() && m_model.base_url.back() == L'/')
        m_model.base_url.pop_back();

    GetDlgItemTextW(IDC_AI_EDIT_KEY, tmp);
    m_model.api_key = tmp.GetString();

    m_model_combo.GetWindowTextW(tmp);
    m_model.model = tmp.GetString();

    GetDlgItemTextW(IDC_AI_EDIT_TEMP, tmp);
    double temperature = _wtof(tmp.GetString());
    if (temperature < 0) temperature = 0;
    if (temperature > 2) temperature = 2;
    m_model.temperature = temperature;

    GetDlgItemTextW(IDC_AI_EDIT_TOPP, tmp);
    double top_p = _wtof(tmp.GetString());
    if (top_p <= 0 || top_p > 1) top_p = 1.0;
    m_model.top_p = top_p;

    GetDlgItemTextW(IDC_AI_EDIT_MAXTOK, tmp);
    int max_tokens = _wtoi(tmp.GetString());
    if (max_tokens < 16) max_tokens = 16;
    if (max_tokens > 32768) max_tokens = 32768;
    m_model.max_tokens = max_tokens;

    GetDlgItemTextW(IDC_AI_EDIT_TIMEOUT, tmp);
    int timeout_sec = _wtoi(tmp.GetString());
    if (timeout_sec < 3) timeout_sec = 3;
    if (timeout_sec > 300) timeout_sec = 300;
    m_model.timeout_sec = timeout_sec;

    if (m_model.base_url.empty())
    {
        why = L"API 地址不能为空";
        return false;
    }
    if (m_model.base_url.find(L"http://") != 0 && m_model.base_url.find(L"https://") != 0)
    {
        why = L"API 地址要以 http:// 或 https:// 开头";
        return false;
    }
    if (m_model.model.empty())
    {
        why = L"模型名不能为空";
        return false;
    }
    if (m_model.name.empty())
        m_model.name = m_model.ProviderName();
    return true;
}

void CAiModelEditDlg::OnOK()
{
    std::wstring why;
    if (!CollectFromUi(why))
    {
        MessageBox(why.c_str(), L"编辑模型", MB_ICONINFORMATION);
        return;
    }
    CBaseDialog::OnOK();
}

// ───────────────────── 拉模型列表 ─────────────────────

void CAiModelEditDlg::OnBnClickedFetch()
{
    if (m_busy)
        return;

    AiModelConfig tmp = m_model;
    std::wstring why;
    // 先用界面上的值拼一次参数（不改变 m_model 本身，取消时要还原）
    AiModelConfig backup = m_model;
    bool collected = CollectFromUi(why);
    AiCallParams params = AiCallParams::FromModel(collected ? m_model : tmp, m_req);
    m_model = backup;
    if (!collected)
    {
        SetHint(why);
        return;
    }

    m_busy = true;
    GetDlgItem(IDC_AI_BTN_FETCH)->EnableWindow(FALSE);
    SetDlgItemTextW(IDC_AI_BTN_FETCH, L"获取中…");
    SetHint(L"正在向服务商要模型列表…");
    m_gen++;
    AiStartFetchModelsJob(GetSafeHwnd(), m_gen, params);
}

afx_msg LRESULT CAiModelEditDlg::OnModelsDone(WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<AiModelListResult> result(reinterpret_cast<AiModelListResult*>(lParam));
    if (static_cast<int>(wParam) != m_gen)
        return 0;       // 迟到的结果，丢掉

    m_busy = false;
    GetDlgItem(IDC_AI_BTN_FETCH)->EnableWindow(TRUE);
    SetDlgItemTextW(IDC_AI_BTN_FETCH, L"获取可用模型");

    if (!result->ok)
    {
        // 拉不到不是致命错误 —— 手填照样能存
        SetHint(result->error.empty() ? L"没拉到模型列表，可以手填" : result->error);
        return 0;
    }

    CString current;
    m_model_combo.GetWindowTextW(current);
    m_model_combo.ResetContent();
    int match = -1;
    for (const auto& name : result->models)
    {
        m_model_combo.AddString(name.c_str());
        if (current == name.c_str())
            match = m_model_combo.GetCount() - 1;
    }
    if (match >= 0)
        m_model_combo.SetCurSel(match);
    else if (m_model_combo.GetCount() > 0)
        m_model_combo.SetCurSel(0);

    wchar_t buf[64];
    swprintf_s(buf, L"拉到 %d 个模型，点输入框下拉可选", static_cast<int>(result->models.size()));
    SetHint(buf);
    return 0;
}

// ───────────────────── 测试连接 ─────────────────────

void CAiModelEditDlg::OnBnClickedTest()
{
    if (m_busy)
        return;

    AiModelConfig backup = m_model;
    std::wstring why;
    bool collected = CollectFromUi(why);
    AiCallParams params = AiCallParams::FromModel(collected ? m_model : backup, m_req);
    m_model = backup;
    if (!collected)
    {
        SetHint(why);
        return;
    }

    m_busy = true;
    GetDlgItem(IDC_AI_BTN_TEST_EDIT)->EnableWindow(FALSE);
    SetDlgItemTextW(IDC_AI_BTN_TEST_EDIT, L"测试中…");
    SetHint(L"正在发一次最小请求…");
    m_gen++;
    AiStartTestJob(GetSafeHwnd(), m_gen, params);
}

afx_msg LRESULT CAiModelEditDlg::OnTestDone(WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<AiCallResult> result(reinterpret_cast<AiCallResult*>(lParam));
    if (static_cast<int>(wParam) != m_gen)
        return 0;

    m_busy = false;
    GetDlgItem(IDC_AI_BTN_TEST_EDIT)->EnableWindow(TRUE);
    SetDlgItemTextW(IDC_AI_BTN_TEST_EDIT, L"测试连接");

    if (result->ok)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"连接正常，用时 %d 毫秒", result->elapsed_ms);
        SetHint(buf);
    }
    else
    {
        SetHint(result->error);
    }
    return 0;
}
