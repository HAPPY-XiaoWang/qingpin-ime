// 轻拼输入法 —— 候选窗与模式提示气泡 (Win32/GDI, 简约扁平浅色风格)
#ifndef QINGPIN_CANDIDATE_WINDOW_H_
#define QINGPIN_CANDIDATE_WINDOW_H_

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "engine/engine.h"

namespace qp {

class CandidateWindow {
public:
    CandidateWindow() = default;
    ~CandidateWindow() { Destroy(); }

    // 创建窗口(每个线程一次); 懒创建, 失败返回 false 但输入法仍可用
    bool EnsureCreated();

    void Destroy();

    // 显示候选列表; anchor 为插入点屏幕坐标矩形
    void ShowCandidates(const std::vector<Candidate>& cands, int page,
                        int per_page, const RECT& anchor,
                        const std::wstring& py_display);

    // 依据新锚点移动窗口 (不改变内容)
    void Reposition(const RECT& anchor);

    void Hide();

    // 中/英模式提示气泡 (自动消失)
    void ShowModeHint(bool english, const RECT& anchor);

    // 回调: 用户点选第 idx 个候选 (全局序号) / 翻页
    void SetOnSelect(std::function<void(int)> cb) { on_select_ = std::move(cb); }
    void SetOnPage(std::function<void(int)> cb) { on_page_ = std::move(cb); }

    bool Visible() const;

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void Layout();
    void Paint(HDC hdc);
    void PaintHint(HDC hdc);

    HWND hwnd_ = nullptr;
    HWND hint_hwnd_ = nullptr;

    // 当前显示数据
    std::vector<Candidate> cands_;
    std::wstring py_;
    int page_ = 0;
    int per_page_ = 5;
    int page_count_ = 1;
    RECT anchor_ = {};
    int hover_ = -1;

    struct ItemRect {
        RECT rc;
        int global_idx;
        bool selected;
    };
    std::vector<ItemRect> items_;

    // 提示气泡
    wchar_t hint_char_ = 0;

    // 字体
    HFONT font_word_ = nullptr;   // 候选词 15px
    HFONT font_py_ = nullptr;     // 拼音 13px
    HFONT font_num_ = nullptr;    // 序号/页码 12px

    std::function<void(int)> on_select_;
    std::function<void(int)> on_page_;
};

}  // namespace qp

#endif  // QINGPIN_CANDIDATE_WINDOW_H_
