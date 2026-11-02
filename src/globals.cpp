// 轻拼输入法 —— 模块级状态定义
// 注意: 本文件必须第一个包含 <initguid.h>, 使上方 DEFINE_GUID 生成实例。
#include <initguid.h>

#include "globals.h"

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;
LONG g_cLockDll = 0;

bool GetDllDir(std::wstring* out) {
    if (!out) return false;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return false;
    std::wstring p = path;
    size_t pos = p.find_last_of(L'\\');
    if (pos == std::wstring::npos) return false;
    *out = p.substr(0, pos);
    return true;
}
