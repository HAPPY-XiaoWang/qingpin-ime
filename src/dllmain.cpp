// 轻拼输入法 —— DLL 入口与 COM 导出
#include <new>

#include "globals.h"
#include "registry.h"
#include "service.h"

namespace {

// ---- 类工厂 ---------------------------------------------------------------
class CQingPinClassFactory final : public IClassFactory {
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        InterlockedIncrement(&g_cRefDll);
        return 2;  // 类工厂为静态单例, 恒定引用计数
    }
    STDMETHODIMP_(ULONG) Release() override {
        InterlockedDecrement(&g_cRefDll);
        return 1;
    }

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid,
                                void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto* pService = new (std::nothrow) qp::CTextService();
        if (!pService) return E_OUTOFMEMORY;
        HRESULT hr = pService->QueryInterface(riid, ppv);
        pService->Release();  // QI 成功时内部已 AddRef
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock)
            InterlockedIncrement(&g_cLockDll);
        else
            InterlockedDecrement(&g_cLockDll);
        return S_OK;
    }
};

CQingPinClassFactory g_classFactory;

}  // namespace

// ---- 标准导出 (见 QingPinIME.def) -----------------------------------------

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (rclsid != CLSID_QingPinTextService) return CLASS_E_CLASSNOTAVAILABLE;
    return g_classFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return (g_cRefDll == 0 && g_cLockDll == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    return RegisterProfiles();
}

STDAPI DllUnregisterServer() {
    return UnregisterProfiles();
}

// ---- DLL 生命周期 ----------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hinst;
            DisableThreadLibraryCalls(hinst);
            break;
        case DLL_PROCESS_DETACH:
            g_hInst = nullptr;
            break;
        default:
            break;
    }
    return TRUE;
}
