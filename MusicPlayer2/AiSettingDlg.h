#pragma once
#include "TabDlg.h"
#include "MyComboBox.h"
#include "AiConfig.h"

// 「选项设置」的第 7 个页签：AI 设置。
//
// 全局配置只有一份（AiConfig::Get()），这一页进来时拷一份到自己手里改，
// 点「确定 / 应用」时才写回去 —— 这样「取消」才是真的取消。

class CAiSettingDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CAiSettingDlg)

public:
    CAiSettingDlg(CWnd* pParent = nullptr);
    virtual ~CAiSettingDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_AI_SETTING_DIALOG };
#endif

protected:
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual void GetDataFromUi() override;

    // ── 界面填数 ──
    void FillCurrentModelCombo();
    void InitModelList();
    void FillModelList();
    void InitPromptHistoryList();
    void FillPromptHistory();
    void InitProxyCombo();
    void FillRequestPart();
    void FillPrivacyPart();
    void UpdateEnabledState();          // 开关联动：关掉总开关就全灰；没勾保存就灰掉路径
    int  SelectedModelIndex() const;    // 列表里选中的那套，-1 表示没选
    void ResetConnStatus();             // 换模型 / 改模型后把「连接状态」打回未测试，并作废在途结果
    void FinishTesting();               // 把「测试连接」按钮从「测试中…」恢复回来

    // ── 提示词历史 ──
    void PushPromptHistory(const std::wstring& text);

    AiSettings m_data;                  // 本页的工作副本
    std::wstring m_initial_prompt;      // 进来时的系统提示词，用来判断要不要进历史
    std::wstring m_default_chat_dir;    // 「保存位置」没填时回显的默认目录，用来判断用户到底改没改
    int m_gen{ 0 };                     // 代号：作废迟到的网络结果
    bool m_testing{ false };

    CMyComboBox m_cur_model_combo;
    CListCtrl   m_model_list;
    CListCtrl   m_hist_list;
    CMyComboBox m_proxy_combo;
    CMyComboBox m_lang_combo;
    CEdit       m_prompt_edit;

    DECLARE_MESSAGE_MAP()

public:
    virtual BOOL OnInitDialog() override;
    afx_msg void OnBnClickedAdd();
    afx_msg void OnBnClickedEdit();
    afx_msg void OnBnClickedDelete();
    afx_msg void OnBnClickedTest();
    afx_msg void OnBnClickedResetPrompt();
    afx_msg void OnBnClickedBrowse();
    afx_msg void OnBnClickedEnable();
    afx_msg void OnBnClickedSaveChat();
    afx_msg void OnCbnSelchangeCurModel();
    afx_msg void OnModelListDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnHistItemClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnTestDone(WPARAM wParam, LPARAM lParam);
};
