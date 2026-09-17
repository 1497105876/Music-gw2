#pragma once
#include "BaseDialog.h"

// 统计口径说明对话框（纯本地静态内容，无任何网络请求）
class CStatHelpDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CStatHelpDlg)
public:
    CStatHelpDlg(CWnd* pParent = nullptr);
    virtual ~CStatHelpDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_STAT_HELP_DLG };
#endif

protected:
    CString m_content;      // 组装好的口径说明文本

    void BuildContent();    // 生成口径清单 + 当前 schema_version + 最近变更

    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual bool IsRememberDialogSizeEnable() const override { return false; }

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog() override;
};
