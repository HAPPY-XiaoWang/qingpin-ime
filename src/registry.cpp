#include "registry.h"

#include "globals.h"

namespace {

const wchar_t* kTipKey =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\{4FE60D32-0B7F-452B-BE8A-3166EB45E0AC}";

// 分类 GUID (见 msctf.h)
const wchar_t* kCatKeyboard = L"{34745C63-B2F0-4789-8B54-5E61B0AEBEEA}";   // TIP_KEYBOARD
const wchar_t* kCatInputMode = L"{CA30BFE5-3D5B-4FD7-812B-2F6D75AAE7EA}";  // TIPCAP_INPUTMODECOMPARTMENT

std::wstring GuidStr(REFGUID guid) {
    wchar_t buf[64] = {};
    StringFromGUID2(guid, buf, 64);
    return buf;
}

LONG SetStr(HKEY root, const std::wstring& sub, const wchar_t* name,
            const std::wstring& value) {
    HKEY h = nullptr;
    LONG rc = RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &h, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                        (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return rc;
}

LONG SetDword(HKEY root, const std::wstring& sub, const wchar_t* name, DWORD value) {
    HKEY h = nullptr;
    LONG rc = RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &h, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
    RegCloseKey(h);
    return rc;
}

}  // namespace

HRESULT RegisterProfiles() {
    std::wstring profile = GuidStr(GUID_QingPinProfile);
    std::wstring tip = std::wstring(kTipKey);  // {CLSID} 形式

    wchar_t dll[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll, MAX_PATH);

    // --- TIP 根键 ---
    HRESULT hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, tip, L"Default", L"*"));
    if (FAILED(hr)) return hr;

    // --- 语言 Profile: 简体中文 (0x0804) ---
    std::wstring prof = tip + L"\\LanguageProfile\\0x0804\\" + profile;
    hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, prof, L"Description", L"轻拼输入法"));
    if (FAILED(hr)) return hr;
    hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, prof, L"Display Name", L"轻拼输入法"));
    if (FAILED(hr)) return hr;
    hr = HRESULT_FROM_WIN32(SetDword(HKEY_LOCAL_MACHINE, prof, L"Enable", 1));
    if (FAILED(hr)) return hr;
    hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, prof, L"IconFile", dll));
    if (FAILED(hr)) return hr;
    hr = HRESULT_FROM_WIN32(SetDword(HKEY_LOCAL_MACHINE, prof, L"IconIndex", 0));
    if (FAILED(hr)) return hr;

    // --- 类别: 键盘 TIP (必需) + 输入模式切空室 (支持 Shift 中英切换指示) ---
    std::wstring cat1 = tip + L"\\Category\\" + kCatKeyboard + L"\\" + profile;
    hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, cat1, L"", L""));
    if (FAILED(hr)) return hr;
    std::wstring cat2 = tip + L"\\Category\\" + kCatInputMode + L"\\" + profile;
    hr = HRESULT_FROM_WIN32(SetStr(HKEY_LOCAL_MACHINE, cat2, L"", L""));
    if (FAILED(hr)) return hr;

    return S_OK;
}

HRESULT UnregisterProfiles() {
    // 整棵删除 TIP 键 (类别/Profile 都在其下)
    LONG rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kTipKey);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(rc);
    return S_OK;
}
