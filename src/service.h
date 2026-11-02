// 轻拼输入法 —— TSF 文本服务主体
// 汇聚所有 TSF 事件接口: 键盘 / 组合 / 焦点 / 布局 / 空室 / 显示属性
#ifndef QINGPIN_SERVICE_H_
#define QINGPIN_SERVICE_H_

#include <functional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "globals.h"
#include "ui/candidate_window.h"

namespace qp {

class CTextService
    : public ITfTextInputProcessorEx,
      public ITfFunction,           // GetDisplayName (注: ITfTextInputProcessor 并不继承它)
      public ITfThreadMgrEventSink,
      public ITfTextEditSink,
      public ITfTextLayoutSink,
      public ITfKeyEventSink,
      public ITfCompositionSink,
      public ITfDisplayAttributeProvider,
      public ITfCompartmentEventSink {
public:
    CTextService();
    ~CTextService();

    // ---- IUnknown ----
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ---- ITfFunction (ITfTextInputProcessor 基接口) ----
    STDMETHODIMP GetDisplayName(BSTR* pbstrName) override;

    // ---- ITfTextInputProcessor / Ex ----
    STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tid,
                            DWORD dwFlags) override;

    // ---- ITfThreadMgrEventSink ----
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) override;
    STDMETHODIMP OnPushContext(ITfContext* pContext) override;
    STDMETHODIMP OnPopContext(ITfContext* pContext) override;

    // ---- ITfThreadMgrEventSink (续)
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocusPrev,
                            ITfDocumentMgr* pDocMgrFocusNext) override;

    // ---- ITfTextEditSink ----
    STDMETHODIMP OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly,
                           ITfEditRecord* pEditRecord) override;

    // ---- ITfTextLayoutSink ----
    STDMETHODIMP OnLayoutChange(ITfContext* pContext, TfLayoutCode lcode,
                                ITfContextView* pView) override;

    // ---- ITfKeyEventSink ----
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam,
                               BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam,
                           BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam,
                             BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam,
                         BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pContext, REFGUID rguid,
                                BOOL* pfEaten) override;

    // ---- ITfCompositionSink ----
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                         ITfComposition* pComposition) override;

    // ---- ITfDisplayAttributeProvider ----
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid,
                                         ITfDisplayAttributeInfo** ppInfo) override;

    // ---- ITfCompartmentEventSink ----
    STDMETHODIMP OnChange(REFGUID rguid) override;

private:
    // ---- 键盘决策状态机 ----
    enum class Action {
        kPass,      // 不吃键
        kAddChar,   // 字母/分隔符入串 (pending_char_)
        kBackspace, // 删除一个字母
        kCancel,    // Esc 取消组合
        kCommitRaw, // Enter 原样上屏拼音
        kSelect,    // 数字/空格选词 (pending_select_)
        kPagePrev,  // - / PageUp
        kPageNext,  // = / PageDown
        kPunct,     // 中文标点 (pending_punct_)
    };

    struct Pending {
        wchar_t ch = 0;
        int select = -1;
        wchar_t punct = 0;
    };

    Action Decide(WPARAM vk, Pending* out);
    void Execute(ITfContext* pContext, Action a, const Pending& p);
    void TrackShiftKey(WPARAM vk, bool down);

    // ---- 组合(composition)操作 (须在编辑会话内调用) ----
    HRESULT EnsureComposition(TfEditCookie ec, ITfContext* pContext);
    HRESULT UpdateCompositionText(TfEditCookie ec);
    HRESULT SetCompositionText(TfEditCookie ec, const std::wstring& text);
    void EndComposition(TfEditCookie ec, bool cancel);

    // 编辑会话封装: 同步执行 fn
    HRESULT RequestEdit(ITfContext* pContext, DWORD flags,
                        std::function<HRESULT(TfEditCookie)> fn);

    // ---- 输入状态 ----
    void UpdateAfterInput(ITfContext* pContext);
    void CommitCandidate(ITfContext* pContext, int idx);
    void CommitRawPinyin(ITfContext* pContext);
    void CommitPunct(ITfContext* pContext, wchar_t punct);
    void ResetInputState(bool hide_window = true);
    void ChangePage(int dir);
    bool IsComposing() const { return m_pComposition.Get() != nullptr; }

    // ---- 候选窗定位 ----
    bool GetCaretRect(ITfContext* pContext, TfEditCookie ec, RECT* out);

    // ---- 事件 sink 注册 ----
    void AdviseContextSinks(ITfContext* pContext);
    void UnadviseContextSinks();

private:
    LONG m_cRef = 1;

    // TSF
    QPComPtr<ITfThreadMgr> m_pThreadMgr;
    TfClientId m_clientId = 0;
    bool m_bActivated = false;

    DWORD m_dwThreadMgrCookie = TF_INVALID_COOKIE;
    DWORD m_dwKeySinkCookie = 0;
    bool m_bKeySinkAdvise = false;
    DWORD m_dwCompartmentCookie = TF_INVALID_COOKIE;
    QPComPtr<ITfCompartment> m_pOpenCloseCompartment;

    QPComPtr<ITfContext> m_pTextCtx;          // 当前焦点上下文 (sink 已注册)
    DWORD m_dwTextEditCookie = TF_INVALID_COOKIE;
    DWORD m_dwTextLayoutCookie = TF_INVALID_COOKIE;

    QPComPtr<ITfComposition> m_pComposition;

    // TfGuidAtom: 组合文本显示属性
    TfGuidAtom m_atomAttrInput = 0;

    // ---- 引擎与输入状态 ----
    PinyinEngine m_engine;
    bool m_bEngineOk = false;
    std::wstring m_input;                 // 字母 + '
    std::vector<Candidate> m_cands;
    int m_page = 0;
    bool m_bEnglish = false;              // Shift 切换的英文直通模式
    bool m_bOpenQuote = false;            // 引号配对状态 (“/” 轮替)
    int m_perPage = 5;

    // Shift 轻敲检测
    bool m_fShiftDown = false;
    bool m_fShiftOtherKey = false;
    DWORD m_dwShiftDownTick = 0;

    // UI
    CandidateWindow m_wnd;
    bool m_bWndOk = false;
    RECT m_lastRect = {};
};

}  // namespace qp

#endif  // QINGPIN_SERVICE_H_
