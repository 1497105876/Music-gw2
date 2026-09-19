#pragma once
#include "BaseDialog.h"
#include "AiConfig.h"
#include "MyComboBox.h"

// 「添加模型 / 编辑模型」小对话框。
//
// 设计原型里这一块是页内展开的编辑区，做成独立模态框是因为：
// 页内展开要在运行时挪十几个控件，滚动和动态布局都得跟着重算，容易出岔子；
// 模态框是等价的形态，还顺带把「确定 / 取消」的语义说清楚了。

class CAiModelEditDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CAiModelEditDlg)

public:
    // model 为空 id 时表示「新增」
    CAiModelEditDlg(const AiModelConfig& model, const AiRequestConfig& req, CWnd* pParent = nullptr);
    virtual ~CAiModelEditDlg();

    const AiModelConfig& GetModel() const { return m_model; }

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_AI_MODEL_EDIT_DIALOG };
#endif

protected:
    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    void FillProviderCombo();
    void SyncProviderPreset();          // 选了服务商就把地址和模型名填好
    bool CollectFromUi(std::wstring& why);  // 把界面上的值收进 m_model
    void SetHint(const std::wstring& text);

    AiModelConfig m_model;
    AiRequestConfig m_req;              // 只为拿代理设置去拉模型列表
    int m_gen{ 0 };                     // 代号：换一次就作废之前发出去的请求
    bool m_busy{ false };               // 有请求在跑

    CMyComboBox m_provider_combo;
    CMyComboBox m_model_combo;

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog() override;
    virtual void OnOK() override;
    afx_msg void OnCbnSelchangeProvider();
    afx_msg void OnBnClickedFetch();
    afx_msg void OnBnClickedTest();
    afx_msg LRESULT OnModelsDone(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTestDone(WPARAM wParam, LPARAM lParam);
};
