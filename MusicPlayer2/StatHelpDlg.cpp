#include "stdafx.h"
#include "MusicPlayer2.h"
#include "StatHelpDlg.h"
#include "StatMeta.h"

IMPLEMENT_DYNAMIC(CStatHelpDlg, CBaseDialog)

CStatHelpDlg::CStatHelpDlg(CWnd* pParent)
    : CBaseDialog(IDD_STAT_HELP_DLG, pParent)
{
}

CStatHelpDlg::~CStatHelpDlg()
{
}

CString CStatHelpDlg::GetDialogName() const
{
    return L"StatHelpDlg";
}

// 组装口径说明（纯静态文本 + 读本地变更记录）
void CStatHelpDlg::BuildContent()
{
    std::wstring text;
    text += L"播放统计口径说明\r\n";
    text += L"════════════════════════\r\n\r\n";

    text += L"【统计口径】\r\n";
    text += L"1. 单次播放不足 15 秒的记录不计入任何统计（视为试听/误触）。\r\n";
    text += L"2. 24 小时时段分布自 v2 起与其余指标口径统一，同样过滤不足 15 秒的记录。\r\n";
    text += L"3. 播放时间按本地时区记录，格式为 YYYY-MM-DDTHH:MM:SS。\r\n";
    text += L"4. 播放结果（finish_reason）四态定义：\r\n";
    text += L"   · 播完：自然播放到曲目结尾；\r\n";
    text += L"   · 跳过：手动切歌（下一首/上一首）；\r\n";
    text += L"   · 停止：主动停止播放；\r\n";
    text += L"   · 出错：播放过程中发生错误。\r\n\r\n";

    text += L"【数据存储位置】\r\n";
    text += L"· 播放记录：config_dir/statistics/playlog_YYYY-MM.jsonl（按月分文件，追加写入）\r\n";
    text += L"· 口径变更记录：config_dir/statistics/schema_changelog.txt\r\n\r\n";

    text += L"【隐私承诺】\r\n";
    text += L"· 统计功能完全本地运行：不上传、不联网、无遥测。\r\n\r\n";

    text += L"【版本】\r\n";
    wchar_t ver[32];
    swprintf_s(ver, L"· 当前口径版本：v%d\r\n", CStatMeta::GetSchemaVersion());
    text += ver;

    std::vector<std::wstring> changelog = CStatMeta::GetChangelog();
    if (!changelog.empty())
    {
        text += L"· 最近一条口径变更：" + changelog.back() + L"\r\n";
    }
    else
    {
        text += L"· 最近一条口径变更：（暂无记录）\r\n";
    }

    m_content = text.c_str();
}

bool CStatHelpDlg::InitializeControls()
{
    SetWindowTextW(L"统计口径说明");
    SetDlgItemTextW(IDOK, L"关闭");

    BuildContent();
    SetDlgItemTextW(IDC_STAT_HELP_TEXT, m_content);

    return true;
}

BEGIN_MESSAGE_MAP(CStatHelpDlg, CBaseDialog)
END_MESSAGE_MAP()

BOOL CStatHelpDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();
    return TRUE;
}
