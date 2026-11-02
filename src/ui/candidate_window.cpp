#include "ui/candidate_window.h"

#include <windowsx.h>

#include "globals.h"

namespace qp {
namespace {

// ---- 简约扁平浅色主题 (类微软拼音 Win11 风格) ------------------------------
const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kBorder = RGB(0xE2, 0xE2, 0xE6);
const COLORREF kPyText = RGB(0x6E, 0x6E, 0x73);
const COLORREF kText = RGB(0x1F, 0x1F, 0x24);
const COLORREF kNumText = RGB(0x9A, 0x9A, 0xA0);
const COLORREF kAccent = RGB(0x00, 0x67, 0xC0);  // Win11 系统强调蓝
const COLORREF kHover = RGB(0xF2, 0xF3, 0xF5);

const int kPadX = 10;        // 窗口左右内边距
const int kPyRowH = 26;      // 拼音行高
const int kCandH = 34;       // 候选行高
const int kItemGap = 2;      // 候选项间距
const int kItemPadX = 9;     // 候选项水平内边距
const int kMaxWidth = 620;   // 窗口最大宽度
const int kPerPage = 5;      // 每页候选数 (数字键 1~5 选择)

const wchar_t* kCandClass = L"QingPinCandWnd";
const wchar_t* kHintClass = L"QingPinHintWnd";

bool g_class_registered = false;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

// Win11 圆角 (Win10 上为普通直角, 不影响使用)
void EnableRoundCorner(HWND hwnd) {
    typedef HRESULT(WINAPI * FnDwmSet)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) return;
    auto fn = (FnDwmSet)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!fn) return;
    DWORD pref = DWMWCP_ROUND;
    fn(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

HFONT MakeFont(int height, bool bold) {
    return CreateFontW(-height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

int WorkAreaFor(const RECT& anchor, RECT* wa) {
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (mon && GetMonitorInfoW(mon, &mi)) {
        *wa = mi.rcWork;
        return 1;
    }
    SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0);
    return 1;
}

}  // namespace

bool CandidateWindow::EnsureCreated() {
    if (hwnd_) return true;

    if (!g_class_registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = &CandidateWindow::WndProcStatic;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kCandClass;
        if (!RegisterClassExW(&wc)) return false;

        WNDCLASSEXW wh = {sizeof(wh)};
        wh.style = CS_DROPSHADOW;
        wh.lpfnWndProc = &CandidateWindow::WndProcStatic;
        wh.hInstance = g_hInst;
        wh.lpszClassName = kHintClass;
        if (!RegisterClassExW(&wh)) return false;
        g_class_registered = true;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kCandClass, L"",
        WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_hInst, this);
    hint_hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kHintClass, L"",
        WS_POPUP, 0, 0, 64, 48, nullptr, nullptr, g_hInst, this);
    if (!hwnd_ || !hint_hwnd_) return false;

    EnableRoundCorner(hwnd_);
    EnableRoundCorner(hint_hwnd_);

    font_word_ = MakeFont(16, false);
    font_py_ = MakeFont(13, false);
    font_num_ = MakeFont(12, false);
    return true;
}

void CandidateWindow::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (hint_hwnd_) {
        DestroyWindow(hint_hwnd_);
        hint_hwnd_ = nullptr;
    }
    if (font_word_) { DeleteObject(font_word_); font_word_ = nullptr; }
    if (font_py_) { DeleteObject(font_py_); font_py_ = nullptr; }
    if (font_num_) { DeleteObject(font_num_); font_num_ = nullptr; }
}

bool CandidateWindow::Visible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void CandidateWindow::ShowCandidates(const std::vector<Candidate>& cands, int page,
                                     int per_page, const RECT& anchor,
                                     const std::wstring& py_display) {
    if (!EnsureCreated()) return;
    cands_ = cands;
    py_ = py_display;
    page_ = page;
    per_page_ = per_page > 0 ? per_page : kPerPage;
    anchor_ = anchor;
    hover_ = -1;
    page_count_ = (int)((cands_.size() + per_page_ - 1) / per_page_);
    if (page_count_ < 1) page_count_ = 1;
    Layout();
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::Reposition(const RECT& anchor) {
    anchor_ = anchor;
    if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
    Layout();
}

void CandidateWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    if (hint_hwnd_) ShowWindow(hint_hwnd_, SW_HIDE);
}

void CandidateWindow::ShowModeHint(bool english, const RECT& anchor) {
    if (!EnsureCreated()) return;
    hint_char_ = english ? L'英' : L'中';
    int x = (anchor.left + anchor.right) / 2 - 32;
    int y = anchor.bottom + 8;
    RECT wa;
    WorkAreaFor(anchor, &wa);
    if (y + 48 > wa.bottom) y = anchor.top - 8 - 48;
    if (x + 64 > wa.right) x = wa.right - 64;
    if (x < wa.left) x = wa.left;
    SetWindowPos(hint_hwnd_, HWND_TOPMOST, x, y, 64, 48,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(hint_hwnd_, nullptr, TRUE);
    SetTimer(hint_hwnd_, 1, 700, nullptr);
}

void CandidateWindow::Layout() {
    if (!hwnd_) return;
    HDC dc = GetDC(hwnd_);
    // 拼音行宽度
    HGDIOBJ old = SelectObject(dc, font_py_);
    SIZE py_sz = {0, 0};
    if (!py_.empty()) GetTextExtentPoint32W(dc, py_.c_str(), (int)py_.size(), &py_sz);
    SelectObject(dc, font_word_);

    items_.clear();
    int x = kPadX;
    int begin = page_ * per_page_;
    int end = (int)cands_.size();
    if (end - begin > per_page_) end = begin + per_page_;

    for (int i = begin; i < end; ++i) {
        const std::wstring& word = cands_[i].word;
        SIZE sz = {0, 0};
        GetTextExtentPoint32W(dc, word.c_str(), (int)word.size(), &sz);
        int num_w = 0;
        SelectObject(dc, font_num_);
        SIZE nsz = {0, 0};
        wchar_t num[2] = {(wchar_t)(L'1' + (i - begin)), 0};
        GetTextExtentPoint32W(dc, num, 1, &nsz);
        num_w = nsz.cx;
        SelectObject(dc, font_word_);

        int w = num_w + 4 + sz.cx + kItemPadX * 2;
        if (x + w > kMaxWidth) break;
        ItemRect it;
        it.rc = {x, 0, x + w, kCandH};
        it.global_idx = i;
        it.selected = (i == begin);
        items_.push_back(it);
        x += w + kItemGap;
    }

    // 总宽 = max(拼音行, 候选行) + 边距
    int content_w = x - kItemGap + kPadX;
    int py_w = py_sz.cx + kPadX * 2 + 56;  // 右侧留页码
    int width = (content_w > py_w ? content_w : py_w) + 2;
    if (width < 120) width = 120;
    if (width > kMaxWidth + kPadX * 2) width = kMaxWidth + kPadX * 2;
    int height = 4 + kPyRowH + kCandH + 6;

    // 定位: 插入点下方, 超出工作区则放上方, 并夹取到工作区内
    RECT wa;
    WorkAreaFor(anchor_, &wa);
    int px = anchor_.left;
    int py = anchor_.bottom + 4;
    if (py + height > wa.bottom) py = anchor_.top - height - 4;
    if (px + width > wa.right) px = wa.right - width;
    if (px < wa.left) px = wa.left;
    if (py < wa.top) py = wa.top;

    SetWindowPos(hwnd_, nullptr, px, py, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ReleaseDC(hwnd_, dc);
}

LRESULT CALLBACK CandidateWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp,
                                                LPARAM lp) {
    // WM_NCCREATE 时从 CREATESTRUCT 取回 this
    if (msg == WM_NCCREATE) {
        auto cs = (CREATESTRUCTW*)lp;
        auto self = (CandidateWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto self = (CandidateWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hwnd == hint_hwnd_)
                PaintHint(hdc);
            else
                Paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            // 只有提示气泡窗口使用了定时器
            if (hwnd == hint_hwnd_) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;

        case WM_MOUSEMOVE: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = -1;
            for (int i = 0; i < (int)items_.size(); ++i) {
                if (PtInRect(&items_[i].rc, pt)) {
                    h = i;
                    break;
                }
            }
            if (h != hover_) {
                hover_ = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            hover_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (auto& it : items_) {
                if (PtInRect(&it.rc, pt)) {
                    if (on_select_) on_select_(it.global_idx);
                    return 0;
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (on_page_) on_page_(delta > 0 ? -1 : 1);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CandidateWindow::Paint(HDC hdc) {
    // 双缓冲
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    // 背景 + 边框
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ old_pen = SelectObject(mem, pen);
    HGDIOBJ old_br = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, w, h);
    SelectObject(mem, old_pen);
    SelectObject(mem, old_br);
    DeleteObject(pen);

    SetBkMode(mem, TRANSPARENT);

    // 拼音行
    SelectObject(mem, font_py_);
    SetTextColor(mem, kPyText);
    if (!py_.empty())
        TextOutW(mem, kPadX, 2, py_.c_str(), (int)py_.size());

    // 页码
    wchar_t page_str[16];
    swprintf(page_str, 16, L"%d/%d", page_ + 1, page_count_);
    SelectObject(mem, font_num_);
    SetTextColor(mem, kNumText);
    SIZE psz = {0, 0};
    GetTextExtentPoint32W(mem, page_str, (int)wcslen(page_str), &psz);
    TextOutW(mem, w - psz.cx - kPadX, 2 + (kPyRowH - psz.cy) / 2 - 2, page_str,
             (int)wcslen(page_str));

    // 拼音行与候选行分隔线
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xF0, 0xF0, 0xF2));
    HGDIOBJ old_pen2 = SelectObject(mem, sep);
    MoveToEx(mem, 1, 4 + kPyRowH, nullptr);
    LineTo(mem, w - 1, 4 + kPyRowH);
    SelectObject(mem, old_pen2);
    DeleteObject(sep);

    // 候选行
    int base_y = 4 + kPyRowH;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const ItemRect& it = items_[i];
        RECT r = it.rc;
        OffsetRect(&r, 0, base_y);
        int idx_in_page = it.global_idx - page_ * per_page_;
        wchar_t num[2] = {(wchar_t)(L'1' + idx_in_page), 0};

        bool selected = it.selected;
        bool hover = (i == hover_);

        if (selected || hover) {
            HBRUSH fill = CreateSolidBrush(selected ? kAccent : kHover);
            // 圆角底
            HBRUSH oldb = (HBRUSH)SelectObject(mem, fill);
            HPEN nop = CreatePen(PS_SOLID, 1, selected ? kAccent : kHover);
            HGDIOBJ oldp = SelectObject(mem, nop);
            RoundRect(mem, r.left, r.top, r.right, r.bottom, 10, 10);
            SelectObject(mem, oldb);
            SelectObject(mem, oldp);
            DeleteObject(fill);
            DeleteObject(nop);
        }

        // 序号
        SelectObject(mem, font_num_);
        SetTextColor(mem, selected ? RGB(0xD8, 0xE8, 0xFF) : kNumText);
        SIZE nsz = {0, 0};
        GetTextExtentPoint32W(mem, num, 1, &nsz);
        TextOutW(mem, r.left + kItemPadX, r.top + (r.bottom - r.top - nsz.cy) / 2,
                 num, 1);

        // 词
        SelectObject(mem, font_word_);
        const std::wstring& word = cands_[it.global_idx].word;
        SetTextColor(mem, selected ? RGB(0xFF, 0xFF, 0xFF) : kText);
        SIZE wsz = {0, 0};
        GetTextExtentPoint32W(mem, word.c_str(), (int)word.size(), &wsz);
        TextOutW(mem, r.left + kItemPadX + nsz.cx + 4,
                 r.top + (r.bottom - r.top - wsz.cy) / 2, word.c_str(),
                 (int)word.size());
    }

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void CandidateWindow::PaintHint(HDC hdc) {
    RECT rc;
    GetClientRect(hint_hwnd_, &rc);
    int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kAccent);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0xFF, 0xFF, 0xFF));
    HFONT f = MakeFont(22, true);
    HGDIOBJ old = SelectObject(mem, f);
    wchar_t s[2] = {hint_char_, 0};
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(mem, s, 1, &sz);
    TextOutW(mem, (w - sz.cx) / 2, (h - sz.cy) / 2, s, 1);
    SelectObject(mem, old);
    DeleteObject(f);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

}  // namespace qp
