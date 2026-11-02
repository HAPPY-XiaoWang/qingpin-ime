#include "service.h"

#include <new>

#include <shellapi.h>
#include <shlobj.h>

#include "registry.h"

namespace qp {
namespace {

// ---- 路径转换 (UTF-16 -> UTF-8) -------------------------------------------
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr,
                        nullptr);
    return s;
}

bool FallbackCaretRect(RECT* out) {
    GUITHREADINFO gi = {sizeof(gi)};
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &gi)) return false;
    RECT r = gi.rcCaret;
    HWND hwnd = gi.hwndCaret ? gi.hwndCaret : gi.hwndActive;
    if (!hwnd || (r.right == 0 && r.bottom == 0)) return false;
    POINT tl = {r.left, r.top};
    POINT br = {r.right ? r.right : r.left + 2, r.bottom ? r.bottom : r.top + 20};
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    out->left = tl.x;
    out->top = tl.y;
    out->right = br.x;
    out->bottom = br.y;
    return true;
}

// ---- 组合文本显示属性 -------------------------------------------------------
class CDisplayAttributeInfoImpl final : public ITfDisplayAttributeInfo {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&c_);
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = InterlockedDecrement(&c_);
        if (!c) delete this;
        return c;
    }

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_QingPinAttrInput;
        return S_OK;
    }
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (!pbstrDesc) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(L"轻拼输入法 输入中");
        return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pAttr) override {
        if (!pAttr) return E_INVALIDARG;
        ZeroMemory(pAttr, sizeof(*pAttr));
        pAttr->lsStyle = TF_LS_DOT;   // 输入中: 点状下划线
        pAttr->fBold = FALSE;
        // 颜色取默认 (TF_CT_NONE = 0), 由应用程序主题决定
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    LONG c_ = 1;
};

}  // namespace

// ===========================================================================
// 构造 / 析构 / IUnknown
// ===========================================================================

CTextService::CTextService() {
    InterlockedIncrement(&g_cRefDll);
}

CTextService::~CTextService() {
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (riid == IID_IUnknown) {
        *ppv = static_cast<IUnknown*>(static_cast<ITfTextInputProcessorEx*>(this));
    } else if (riid == IID_ITfTextInputProcessor) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (riid == IID_ITfTextInputProcessorEx) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (riid == IID_ITfFunction) {
        *ppv = static_cast<ITfFunction*>(this);
    } else if (riid == IID_ITfThreadMgrEventSink) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else if (riid == IID_ITfTextLayoutSink) {
        *ppv = static_cast<ITfTextLayoutSink*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfCompositionSink) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (riid == IID_ITfDisplayAttributeProvider) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else if (riid == IID_ITfCompartmentEventSink) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release() {
    ULONG c = InterlockedDecrement(&m_cRef);
    if (!c) delete this;
    return c;
}

STDMETHODIMP CTextService::GetDisplayName(BSTR* pbstrName) {
    if (!pbstrName) return E_INVALIDARG;
    *pbstrName = SysAllocString(L"轻拼输入法");
    return *pbstrName ? S_OK : E_OUTOFMEMORY;
}

// ===========================================================================
// 激活 / 反激活
// ===========================================================================

STDMETHODIMP CTextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tid) {
    return ActivateEx(pThreadMgr, tid, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tid,
                                      DWORD /*dwFlags*/) {
    if (!pThreadMgr) return E_INVALIDARG;
    if (m_bActivated) Deactivate();

    m_pThreadMgr = pThreadMgr;
    m_clientId = tid;

    // ---- 初始化引擎 (词库与 DLL 同目录; 用户词典在 %APPDATA%) ----
    {
        std::wstring dir;
        std::string dict_path, user_path;
        if (GetDllDir(&dir)) dict_path = WideToUtf8(dir + L"\\dict\\dict.txt");
        PWSTR appdata = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                           &appdata))) {
            std::wstring udir = std::wstring(appdata) + L"\\QingPinIME";
            CreateDirectoryW(udir.c_str(), nullptr);
            user_path = WideToUtf8(udir + L"\\userdict.txt");
            CoTaskMemFree(appdata);
        }
        m_bEngineOk = !dict_path.empty() && m_engine.Init(dict_path, user_path);
        if (!m_bEngineOk) {
            OutputDebugStringW(L"[QingPin] 词库加载失败, 将以无候选模式运行\n");
        }
    }

    // ---- 候选窗 ----
    m_bWndOk = m_wnd.EnsureCreated();
    m_wnd.SetOnSelect([this](int idx) {
        if (m_bActivated && m_pTextCtx.Get()) CommitCandidate(m_pTextCtx.Get(), idx);
    });
    m_wnd.SetOnPage([this](int dir) { ChangePage(dir); });

    // ---- 注册事件 sink ----
    // 1) 线程管理器事件 (焦点切换等)
    {
        QPComPtr<ITfSource> pSrc;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pSrc))))
            pSrc->AdviseSink(IID_ITfThreadMgrEventSink,
                             (ITfThreadMgrEventSink*)this, &m_dwThreadMgrCookie);
    }
    // 2) 键盘事件
    {
        QPComPtr<ITfKeystrokeMgr> pKeys;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pKeys))))
            m_bKeySinkAdvise =
                SUCCEEDED(pKeys->AdviseKeyEventSink(m_clientId,
                                                    (ITfKeyEventSink*)this, TRUE));
    }
    // 3) 键盘开关空室 (用户 Ctrl+Space 关闭输入法时收尾)
    {
        QPComPtr<ITfCompartmentMgr> pMgr;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pMgr)))) {
            if (SUCCEEDED(pMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                               &m_pOpenCloseCompartment))) {
                QPComPtr<ITfSource> pSrc;
                if (SUCCEEDED(m_pOpenCloseCompartment->QueryInterface(
                        IID_PPV_ARGS(&pSrc))))
                    pSrc->AdviseSink(IID_ITfCompartmentEventSink,
                                     (ITfCompartmentEventSink*)this,
                                     &m_dwCompartmentCookie);
            }
        }
    }
    // 4) 显示属性 GUID -> TfGuidAtom
    {
        QPComPtr<ITfCategoryMgr> pCat;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pCat))))
            pCat->RegisterGUID(GUID_QingPinAttrInput, &m_atomAttrInput);
    }
    // 5) 当前焦点文档
    {
        QPComPtr<ITfDocumentMgr> pDoc;
        if (SUCCEEDED(m_pThreadMgr->GetFocus(&pDoc)) && pDoc.Get()) {
            QPComPtr<ITfContext> pCtx;
            if (SUCCEEDED(pDoc->GetTop(&pCtx)) && pCtx.Get())
                AdviseContextSinks(pCtx.Get());
        }
    }

    m_bActivated = true;
    return S_OK;
}

STDMETHODIMP CTextService::Deactivate() {
    if (!m_bActivated) return S_OK;
    m_bActivated = false;

    // 取消进行中的组合
    if (m_pComposition.Get() && m_pTextCtx.Get()) {
        RequestEdit(m_pTextCtx.Get(), TF_ES_SYNC | TF_ES_READWRITE,
                    [&](TfEditCookie ec) -> HRESULT {
                        EndComposition(ec, true);
                        return S_OK;
                    });
    }
    ResetInputState();

    UnadviseContextSinks();

    if (m_pOpenCloseCompartment.Get()) {
        QPComPtr<ITfSource> pSrc;
        if (SUCCEEDED(m_pOpenCloseCompartment->QueryInterface(IID_PPV_ARGS(&pSrc))))
            pSrc->UnadviseSink(m_dwCompartmentCookie);
        m_pOpenCloseCompartment.Release();
    }
    m_dwCompartmentCookie = TF_INVALID_COOKIE;

    if (m_bKeySinkAdvise) {
        QPComPtr<ITfKeystrokeMgr> pKeys;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pKeys))))
            pKeys->UnadviseKeyEventSink(m_clientId);
        m_bKeySinkAdvise = false;
    }
    if (m_dwThreadMgrCookie != TF_INVALID_COOKIE) {
        QPComPtr<ITfSource> pSrc;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pSrc))))
            pSrc->UnadviseSink(m_dwThreadMgrCookie);
        m_dwThreadMgrCookie = TF_INVALID_COOKIE;
    }

    m_wnd.Hide();
    m_pThreadMgr.Release();
    m_clientId = 0;
    return S_OK;
}

// ===========================================================================
// 编辑会话封装
// ===========================================================================

namespace {
// 编辑会话对象: 同步执行捕获的闭包
class CEditSessionImpl final : public ITfEditSession {
public:
    explicit CEditSessionImpl(std::function<HRESULT(TfEditCookie)> fn)
        : fn_(std::move(fn)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&c_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG c = InterlockedDecrement(&c_);
        if (!c) delete this;
        return c;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        return fn_ ? fn_(ec) : E_FAIL;
    }

private:
    LONG c_ = 1;
    std::function<HRESULT(TfEditCookie)> fn_;
};
}  // namespace

HRESULT CTextService::RequestEdit(ITfContext* pContext, DWORD flags,
                                  std::function<HRESULT(TfEditCookie)> fn) {
    if (!pContext) return E_POINTER;
    HRESULT hr_session = E_FAIL;
    CEditSessionImpl* pSession = new CEditSessionImpl(std::move(fn));
    HRESULT hr = pContext->RequestEditSession(m_clientId, pSession, flags,
                                              &hr_session);
    pSession->Release();
    if (FAILED(hr)) return hr;
    return hr_session;
}

// ===========================================================================
// 组合 (composition) 操作
// ===========================================================================

HRESULT CTextService::EnsureComposition(TfEditCookie ec, ITfContext* pContext) {
    if (m_pComposition.Get()) return S_OK;

    QPComPtr<ITfInsertAtSelection> pInsert;
    HRESULT hr = pContext->QueryInterface(IID_PPV_ARGS(&pInsert));
    if (FAILED(hr)) return hr;

    QPComPtr<ITfRange> pRange;
    hr = pInsert->InsertTextAtSelection(ec, 0, nullptr, 0, &pRange);
    if (FAILED(hr)) return hr;

    QPComPtr<ITfContextComposition> pCtxComp;
    hr = pContext->QueryInterface(IID_PPV_ARGS(&pCtxComp));
    if (FAILED(hr)) return hr;

    hr = pCtxComp->StartComposition(ec, pRange.Get(),
                                    (ITfCompositionSink*)this, &m_pComposition);
    if (FAILED(hr)) return hr;

    // 组合文本设置显示属性 (点状下划线)
    if (m_atomAttrInput == 0 && m_pThreadMgr.Get()) {
        QPComPtr<ITfCategoryMgr> pCat;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_PPV_ARGS(&pCat))))
            pCat->RegisterGUID(GUID_QingPinAttrInput, &m_atomAttrInput);
    }
    if (m_atomAttrInput != 0) {
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = (LONG)m_atomAttrInput;
        pRange->SetValue(ec, GUID_PROP_ATTRIBUTE, &var);
        VariantClear(&var);
    }
    return S_OK;
}

HRESULT CTextService::SetCompositionText(TfEditCookie ec, const std::wstring& text) {
    if (!m_pComposition.Get()) return E_FAIL;
    QPComPtr<ITfRange> pRange;
    if (FAILED(m_pComposition->GetRange(&pRange))) return E_FAIL;
    return pRange->SetText(ec, 0, text.c_str(), (LONG)text.size());
}

HRESULT CTextService::UpdateCompositionText(TfEditCookie ec) {
    std::wstring disp = m_bEngineOk ? m_engine.Display(m_input) : m_input;
    return SetCompositionText(ec, disp);
}

void CTextService::EndComposition(TfEditCookie ec, bool cancel) {
    if (m_pComposition.Get()) {
        m_pComposition->EndComposition(ec, cancel ? TRUE : FALSE);
        m_pComposition.Release();
    }
}

// ===========================================================================
// 候选窗定位
// ===========================================================================

bool CTextService::GetCaretRect(ITfContext* pContext, TfEditCookie ec, RECT* out) {
    if (!m_pComposition.Get()) return false;
    QPComPtr<ITfContextView> pView;
    if (FAILED(pContext->GetActiveView(&pView))) return false;
    QPComPtr<ITfRange> pRange;
    if (FAILED(m_pComposition->GetRange(&pRange))) return false;
    RECT r = {};
    BOOL clipped = FALSE;
    if (FAILED(pView->GetTextExt(ec, pRange.Get(), &r, &clipped))) return false;
    HWND hwnd = nullptr;
    if (FAILED(pView->GetWnd(&hwnd)) || !hwnd) hwnd = GetFocus();
    if (hwnd) {
        POINT tl = {r.left, r.top};
        POINT br = {r.right, r.bottom};
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        out->left = tl.x;
        out->top = tl.y;
        out->right = br.x;
        out->bottom = br.y;
    } else {
        *out = r;
    }
    return true;
}

// ===========================================================================
// 输入状态
// ===========================================================================

void CTextService::UpdateAfterInput(ITfContext* pContext) {
    if (m_input.empty()) {
        ResetInputState();
        return;
    }
    m_cands = m_bEngineOk ? m_engine.Query(m_input, 50) : std::vector<Candidate>{};
    m_page = 0;

    RECT rect = m_lastRect;
    bool got_rect = false;
    HRESULT hr = RequestEdit(
        pContext, TF_ES_SYNC | TF_ES_READWRITE, [&](TfEditCookie ec) -> HRESULT {
            HRESULT h = EnsureComposition(ec, pContext);
            if (FAILED(h)) return h;
            h = UpdateCompositionText(ec);
            if (SUCCEEDED(h)) got_rect = GetCaretRect(pContext, ec, &rect);
            return h;
        });
    if (FAILED(hr)) {
        // 编辑会话失败: 回滚本次按键, 保持状态一致
        if (!m_input.empty()) m_input.pop_back();
        return;
    }
    if (!got_rect) FallbackCaretRect(&rect);
    m_lastRect = rect;

    if (m_bWndOk) {
        if (!m_cands.empty()) {
            m_wnd.ShowCandidates(m_cands, m_page, m_perPage, rect,
                                 m_bEngineOk ? m_engine.Display(m_input) : m_input);
        } else {
            m_wnd.Hide();
        }
    }
}

void CTextService::CommitCandidate(ITfContext* pContext, int idx) {
    if (idx < 0 || idx >= (int)m_cands.size() || !IsComposing()) {
        ResetInputState();
        return;
    }
    std::wstring word = m_cands[idx].word;
    std::wstring learned_input = m_input;
    RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                [&](TfEditCookie ec) -> HRESULT {
                    HRESULT h = SetCompositionText(ec, word);
                    if (SUCCEEDED(h)) EndComposition(ec, false);
                    return h;
                });
    // 自学习 (记录选词, 动态调频)
    if (m_bEngineOk) m_engine.Learn(learned_input, word);
    ResetInputState();
}

void CTextService::CommitRawPinyin(ITfContext* pContext) {
    if (!IsComposing()) return;
    std::wstring raw = m_input;
    RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                [&](TfEditCookie ec) -> HRESULT {
                    HRESULT h = SetCompositionText(ec, raw);
                    if (SUCCEEDED(h)) EndComposition(ec, false);
                    return h;
                });
    ResetInputState();
}

void CTextService::CommitPunct(ITfContext* pContext, wchar_t punct) {
    // 引号轮替 (“ ” / ‘ ’)
    if (punct == L'“' || punct == L'”' || punct == L'‘' || punct == L'’')
        m_bOpenQuote = !m_bOpenQuote;

    if (IsComposing()) {
        // 组合中: 首选词(或原样拼音) + 标点 一起上屏
        std::wstring text;
        int idx = m_page * m_perPage;
        if (!m_cands.empty() && idx < (int)m_cands.size()) {
            text = m_cands[idx].word;
            if (m_bEngineOk) m_engine.Learn(m_input, m_cands[idx].word);
        } else {
            text = m_input;
        }
        text += punct;
        RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                    [&](TfEditCookie ec) -> HRESULT {
                        HRESULT h = SetCompositionText(ec, text);
                        if (SUCCEEDED(h)) EndComposition(ec, false);
                        return h;
                    });
        ResetInputState();
    } else {
        // 非组合: 直接上屏全角标点
        RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                    [&](TfEditCookie ec) -> HRESULT {
                        QPComPtr<ITfInsertAtSelection> pInsert;
                        HRESULT h = pContext->QueryInterface(IID_PPV_ARGS(&pInsert));
                        if (FAILED(h)) return h;
                        QPComPtr<ITfRange> pRange;
                        return pInsert->InsertTextAtSelection(ec, 0, &punct, 1,
                                                              &pRange);
                    });
    }
}

void CTextService::ChangePage(int dir) {
    if (m_cands.empty()) return;
    int max_page = (int)((m_cands.size() + m_perPage - 1) / m_perPage) - 1;
    int np = m_page + dir;
    if (np < 0) np = 0;
    if (np > max_page) np = max_page;
    if (np == m_page) return;
    m_page = np;
    if (m_bWndOk) {
        m_wnd.ShowCandidates(m_cands, m_page, m_perPage, m_lastRect,
                             m_bEngineOk ? m_engine.Display(m_input) : m_input);
    }
}

void CTextService::ResetInputState(bool hide_window) {
    m_input.clear();
    m_cands.clear();
    m_page = 0;
    m_fShiftDown = false;
    m_fShiftOtherKey = false;
    if (hide_window && m_bWndOk) m_wnd.Hide();
}

// ===========================================================================
// 键盘决策状态机
// ===========================================================================

namespace {

// 中文标点映射 (全拼中文模式); 返回 0 表示透传
wchar_t MapPunct(WPARAM vk, bool shift) {
    switch (vk) {
        case VK_OEM_COMMA:  // ,
            return shift ? L'《' : L'，';
        case VK_OEM_PERIOD:  // .
            return shift ? L'》' : L'。';
        case VK_OEM_1:  // ;
            return shift ? L'：' : L'；';
        case VK_OEM_2:  // /
            return shift ? L'？' : L'、';
        case VK_OEM_3:  // `
            return shift ? L'～' : L'·';
        case VK_OEM_4:  // [
            return shift ? L'『' : L'【';
        case VK_OEM_6:  // ]
            return shift ? L'』' : L'】';
        case VK_OEM_5:  // 反斜杠 → 顿号
            return L'、';
        case '1':
            return shift ? L'！' : 0;
        case '4':
            return shift ? L'￥' : 0;
        case '6':
            return shift ? L'…' : 0;
        case '9':
            return shift ? L'（' : 0;
        case '0':
            return shift ? L'）' : 0;
        case VK_OEM_7:  // ' / "
            return shift ? (L'\0') : (L'\0');  // 在 Decide 中单独处理(轮替)
        default:
            return 0;
    }
}

}  // namespace

CTextService::Action CTextService::Decide(WPARAM vk, Pending* out) {
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl || alt) return Action::kPass;  // 组合热键全部透传

    if (m_bEnglish) {
        // 英文直通模式: 一律透传 (数字/标点/字母)
        return Action::kPass;
    }

    // ---- 中文模式 ----
    if (vk >= 'A' && vk <= 'Z') {
        if (shift) return Action::kPass;  // Shift+字母 → 大写英文直通
        out->ch = (wchar_t)(L'a' + (vk - 'A'));
        return Action::kAddChar;
    }
    if (vk == VK_OEM_7) {  // ' 键
        if (IsComposing() && !shift) {
            out->ch = L'\'';
            return Action::kAddChar;  // 音节分隔符
        }
        // 非组合或 Shift: 引号轮替
        out->punct = shift ? (m_bOpenQuote ? L'”' : L'“')
                           : (m_bOpenQuote ? L'’' : L'‘');
        return Action::kPunct;
    }

    if (IsComposing()) {
        if (vk >= '1' && vk <= '9') {
            int idx = m_page * m_perPage + (int)(vk - '1');
            if (idx < (int)m_cands.size()) {
                out->select = idx;
                return Action::kSelect;
            }
            return Action::kPass;
        }
        if (vk == VK_SPACE) {
            out->select = m_page * m_perPage;
            return m_cands.empty() ? Action::kCommitRaw : Action::kSelect;
        }
        if (vk == VK_RETURN) return Action::kCommitRaw;
        if (vk == VK_BACK) return Action::kBackspace;
        if (vk == VK_ESCAPE) return Action::kCancel;
        if (vk == VK_OEM_MINUS || vk == VK_PRIOR) return Action::kPagePrev;
        if (vk == VK_OEM_PLUS || vk == VK_NEXT) return Action::kPageNext;
    }

    wchar_t punct = MapPunct(vk, shift);
    if (punct) {
        out->punct = punct;
        return Action::kPunct;
    }
    return Action::kPass;
}

void CTextService::Execute(ITfContext* pContext, Action a, const Pending& p) {
    switch (a) {
        case Action::kAddChar: {
            std::wstring old = m_input;
            m_input += p.ch;
            size_t before = m_input.size();
            UpdateAfterInput(pContext);
            if (m_input.size() != before + 1) m_input = old;  // 失败回滚
            break;
        }
        case Action::kSelect:
            CommitCandidate(pContext, p.select);
            break;
        case Action::kCommitRaw:
            CommitRawPinyin(pContext);
            break;
        case Action::kBackspace: {
            if (!IsComposing() || m_input.empty()) break;
            m_input.pop_back();
            if (m_input.empty()) {
                RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                            [&](TfEditCookie ec) -> HRESULT {
                                EndComposition(ec, true);
                                return S_OK;
                            });
                ResetInputState();
            } else {
                UpdateAfterInput(pContext);
            }
            break;
        }
        case Action::kCancel:
            RequestEdit(pContext, TF_ES_SYNC | TF_ES_READWRITE,
                        [&](TfEditCookie ec) -> HRESULT {
                            EndComposition(ec, true);
                            return S_OK;
                        });
            ResetInputState();
            break;
        case Action::kPagePrev:
            ChangePage(-1);
            break;
        case Action::kPageNext:
            ChangePage(1);
            break;
        case Action::kPunct:
            CommitPunct(pContext, p.punct);
            break;
        default:
            break;
    }
}

// Shift 轻敲检测: 按下记录时间, 抬起时若期间未按过其他键且 <600ms → 切换中英
void CTextService::TrackShiftKey(WPARAM vk, bool down) {
    if (vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT) {
        if (m_fShiftDown) m_fShiftOtherKey = true;
        return;
    }
    if (down) {
        if (!m_fShiftDown) {
            m_fShiftDown = true;
            m_fShiftOtherKey = false;
            m_dwShiftDownTick = GetTickCount();
        }
        return;
    }
    // 抬起
    if (m_fShiftDown) {
        if (!m_fShiftOtherKey && GetTickCount() - m_dwShiftDownTick < 600) {
            m_bEnglish = !m_bEnglish;
            if (m_bEnglish && IsComposing() && m_pTextCtx.Get()) {
                RequestEdit(m_pTextCtx.Get(), TF_ES_SYNC | TF_ES_READWRITE,
                            [&](TfEditCookie ec) -> HRESULT {
                                EndComposition(ec, true);
                                return S_OK;
                            });
            }
            ResetInputState();
            if (m_bWndOk) m_wnd.ShowModeHint(m_bEnglish, m_lastRect);
        }
        m_fShiftDown = false;
    }
}

// ===========================================================================
// ITfKeyEventSink
// ===========================================================================

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* /*pDocMgr*/) {
    return S_OK;  // 焦点管理由 ITfThreadMgrEventSink::OnSetFocus(2参) 负责
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* pContext, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL* pfEaten) {
    *pfEaten = FALSE;
    if (!m_bActivated || !pContext) return S_OK;
    TrackShiftKey(wParam, true);
    Pending p;
    Action a = Decide(wParam, &p);
    *pfEaten = (a != Action::kPass);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pContext, WPARAM wParam,
                                     LPARAM /*lParam*/, BOOL* pfEaten) {
    *pfEaten = FALSE;
    if (!m_bActivated || !pContext) return S_OK;
    TrackShiftKey(wParam, true);
    Pending p;
    Action a = Decide(wParam, &p);
    if (a == Action::kPass) return S_OK;
    *pfEaten = TRUE;
    Execute(pContext, a, p);
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL* pfEaten) {
    *pfEaten = FALSE;  // Test 阶段不做状态变更
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*pContext*/, WPARAM wParam,
                                   LPARAM /*lParam*/, BOOL* pfEaten) {
    *pfEaten = FALSE;
    if (!m_bActivated) return S_OK;
    TrackShiftKey(wParam, false);
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pContext*/,
                                          REFGUID /*rguid*/, BOOL* pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

// ===========================================================================
// ITfThreadMgrEventSink
// ===========================================================================

STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr* /*pDocMgr*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* /*pDocMgrFocusPrev*/,
                                      ITfDocumentMgr* pDocMgrFocusNext) {
    // 焦点切换: 隐藏候选窗, 迁移上下文事件 sink
    if (m_bWndOk) m_wnd.Hide();
    UnadviseContextSinks();
    if (pDocMgrFocusNext && m_bActivated) {
        QPComPtr<ITfContext> pCtx;
        if (SUCCEEDED(pDocMgrFocusNext->GetTop(&pCtx)) && pCtx.Get())
            AdviseContextSinks(pCtx.Get());
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnPushContext(ITfContext* /*pContext*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnPopContext(ITfContext* /*pContext*/) {
    return S_OK;
}

void CTextService::AdviseContextSinks(ITfContext* pContext) {
    UnadviseContextSinks();
    m_pTextCtx = pContext;
    QPComPtr<ITfSource> pSrc;
    if (SUCCEEDED(pContext->QueryInterface(IID_PPV_ARGS(&pSrc)))) {
        pSrc->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink*)this,
                         &m_dwTextEditCookie);
        pSrc->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink*)this,
                         &m_dwTextLayoutCookie);
    }
}

void CTextService::UnadviseContextSinks() {
    if (m_pTextCtx.Get()) {
        QPComPtr<ITfSource> pSrc;
        if (SUCCEEDED(m_pTextCtx->QueryInterface(IID_PPV_ARGS(&pSrc)))) {
            if (m_dwTextEditCookie != TF_INVALID_COOKIE)
                pSrc->UnadviseSink(m_dwTextEditCookie);
            if (m_dwTextLayoutCookie != TF_INVALID_COOKIE)
                pSrc->UnadviseSink(m_dwTextLayoutCookie);
        }
    }
    m_pTextCtx.Release();
    m_dwTextEditCookie = TF_INVALID_COOKIE;
    m_dwTextLayoutCookie = TF_INVALID_COOKIE;
}

// ===========================================================================
// ITfTextEditSink / ITfTextLayoutSink
// ===========================================================================

STDMETHODIMP CTextService::OnEndEdit(ITfContext* pContext, TfEditCookie /*ec*/,
                                     ITfEditRecord* /*pEditRecord*/) {
    // 校验组合对象仍有效 (被外部破坏时收尾)
    if (m_bActivated && m_pComposition.Get() && pContext == m_pTextCtx.Get()) {
        QPComPtr<ITfRange> pRange;
        if (FAILED(m_pComposition->GetRange(&pRange))) {
            m_pComposition.Release();
            ResetInputState();
        }
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnLayoutChange(TfEditCookie ec, ITfContext* pContext,
                                          TfLayoutCookie /*lcookie*/) {
    // 插入点移动: 跟随候选窗
    if (m_bActivated && IsComposing() && pContext == m_pTextCtx.Get()) {
        RECT r = m_lastRect;
        if (GetCaretRect(pContext, ec, &r) || FallbackCaretRect(&r)) {
            m_lastRect = r;
            if (m_bWndOk && m_wnd.Visible()) m_wnd.Reposition(r);
        }
    }
    return S_OK;
}

// ===========================================================================
// ITfCompositionSink
// ===========================================================================

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/,
                                                   ITfComposition* pComposition) {
    // 系统/应用终止了组合 (如窗口失焦): 收尾并隐藏
    if (pComposition && pComposition == m_pComposition.Get()) {
        m_pComposition.Release();
    }
    ResetInputState();
    return S_OK;
}

// ===========================================================================
// ITfDisplayAttributeProvider
// ===========================================================================

STDMETHODIMP CTextService::GetDisplayAttributeCount(INT* pnCount) {
    if (!pnCount) return E_INVALIDARG;
    *pnCount = 1;
    return S_OK;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(INT /*iIndex*/,
                                                   ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = new (std::nothrow) CDisplayAttributeInfoImpl();
    return *ppInfo ? S_OK : E_OUTOFMEMORY;
}

// ===========================================================================
// ITfCompartmentEventSink
// ===========================================================================

STDMETHODIMP CTextService::OnChange(REFGUID rguid) {
    if (rguid != GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) return S_OK;
    // 用户以 Ctrl+Space 等方式关闭输入法时, 收尾当前组合
    if (m_pOpenCloseCompartment.Get()) {
        VARIANT var;
        VariantInit(&var);
        if (SUCCEEDED(m_pOpenCloseCompartment->GetValue(&var))) {
            bool open = V_I4(&var) != 0;
            VariantClear(&var);
            if (!open) {
                if (IsComposing() && m_pTextCtx.Get()) {
                    RequestEdit(m_pTextCtx.Get(), TF_ES_SYNC | TF_ES_READWRITE,
                                [&](TfEditCookie ec) -> HRESULT {
                                    EndComposition(ec, true);
                                    return S_OK;
                                });
                }
                ResetInputState();
            }
        }
    }
    return S_OK;
}

}  // namespace qp
