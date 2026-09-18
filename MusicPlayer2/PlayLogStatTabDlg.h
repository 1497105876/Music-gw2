#pragma once
#include "TabDlg.h"
#include "ListCtrlEx.h"
#include "StatCommon.h"
#include "StatAnalysis.h"
#include "PlayStatistics.h"

// ─────────────────────────────────────────────────────────────────────────────
// 「歌曲详细记录」页面的数据快照：由主对话框统一算好后下发，子页只读。
// 所有聚合都在聚合层完成，子页只做填充，避免同一个指标出现两份算法。
// ─────────────────────────────────────────────────────────────────────────────
struct PlayLogStatData
{
    const std::vector<PlayRecord>* records{ nullptr };  // 已按时间区间过滤（唯一过滤点）
    StatFilter     filter;
    StatSummary    summary;
    FinishBreakdown finish;
    std::vector<ArtistRankItem> artists;
    std::vector<AlbumRankItem>  albums;
    std::vector<SongRankItem>   songs;
    std::vector<PeriodBucket>   day_buckets;    // 日粒度桶，用于「听得最久的一天」
    int            hour_hist[24]{};             // 24 小时分布（有效记录数）
    int            first_ymd{ 0 };              // 数据最早日期
    int            last_ymd{ 0 };               // 数据最晚日期
    int            broken_lines{ 0 };           // 解析损坏被跳过的行数
    int            failed_files{ 0 };           // 读取失败的文件数
    bool           load_ok{ false };            // 数据是否成功读入
    bool           valid{ false };              // 快照是否已算好
};

// 子页基类：负责列表初始化与「脏则重算」的进入逻辑
class CPlayLogStatTabDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CPlayLogStatTabDlg)

public:
    CPlayLogStatTabDlg(UINT nIDTemplate, CWnd* pParent = nullptr);

    // 主对话框在数据变化时调用；只做标记，真正刷新发生在进入该页时
    void SetData(const PlayLogStatData* data);
    virtual void OnTabEntered() override;

protected:
    virtual void RefreshView() = 0;
    virtual void InitListColumns() = 0;

    // 通用：设置列表样式为整行选中 + 网格线
    void PrepareList(CListCtrlEx& list);
    // 空态提示：在列表里显示一行灰色说明
    void ShowEmptyRow(CListCtrlEx& list, const wchar_t* text);

    const PlayLogStatData* m_data{ nullptr };
    bool m_dirty{ true };
};

// 概览页：两列（项目 / 数值）分组列表
class CPlayLogStatOverviewTabDlg : public CPlayLogStatTabDlg
{
public:
    CPlayLogStatOverviewTabDlg(CWnd* pParent = nullptr) : CPlayLogStatTabDlg(IDD_PLAYLOG_STAT_OVERVIEW_DLG, pParent) {}

protected:
    virtual void RefreshView() override;
    virtual void InitListColumns() override;

private:
    void AddRow(int group, const wchar_t* item, const std::wstring& value);

    CListCtrlEx m_list;
    int m_row{ 0 };
    int m_group{ -1 };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
};

// 歌手排行页
class CPlayLogStatArtistTabDlg : public CPlayLogStatTabDlg
{
public:
    CPlayLogStatArtistTabDlg(CWnd* pParent = nullptr) : CPlayLogStatTabDlg(IDD_PLAYLOG_STAT_ARTIST_DLG, pParent) {}

protected:
    virtual void RefreshView() override;
    virtual void InitListColumns() override;

private:
    CListCtrlEx m_list;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
};

// 专辑排行页
class CPlayLogStatAlbumTabDlg : public CPlayLogStatTabDlg
{
public:
    CPlayLogStatAlbumTabDlg(CWnd* pParent = nullptr) : CPlayLogStatTabDlg(IDD_PLAYLOG_STAT_ALBUM_DLG, pParent) {}

protected:
    virtual void RefreshView() override;
    virtual void InitListColumns() override;

private:
    CListCtrlEx m_list;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
};

// 曲目排行页
class CPlayLogStatSongTabDlg : public CPlayLogStatTabDlg
{
public:
    CPlayLogStatSongTabDlg(CWnd* pParent = nullptr) : CPlayLogStatTabDlg(IDD_PLAYLOG_STAT_SONG_DLG, pParent) {}

protected:
    virtual void RefreshView() override;
    virtual void InitListColumns() override;

private:
    CListCtrlEx m_list;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
};

// 播放明细页：展示全部原始记录（不做 15 秒过滤），末列标注入是否被计入统计
class CPlayLogStatDetailTabDlg : public CPlayLogStatTabDlg
{
public:
    CPlayLogStatDetailTabDlg(CWnd* pParent = nullptr) : CPlayLogStatTabDlg(IDD_PLAYLOG_STAT_DETAIL_DLG, pParent) {}

protected:
    virtual void RefreshView() override;
    virtual void InitListColumns() override;

private:
    CListCtrlEx m_list;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
};
