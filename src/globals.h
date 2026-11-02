// 轻拼输入法 —— 全局定义: GUID / 模块状态 / 轻量 COM 智能指针
#ifndef QINGPIN_GLOBALS_H_
#define QINGPIN_GLOBALS_H_

#include <string>

#include <windows.h>
#include <msctf.h>

// ---- 本输入法的固定 GUID (regsvr32 注册与 TSF 加载均依赖这些值) -----------

// {4FE60D32-0B7F-452B-BE8A-3166EB45E0AC}  文本服务 CLSID
DEFINE_GUID(CLSID_QingPinTextService,
            0x4fe60d32, 0x0b7f, 0x452b, 0xbe, 0x8a, 0x31, 0x66, 0xeb, 0x45, 0xe0, 0xac);

// {C5591836-2317-4594-99CD-0CDEF0BD6E44}  简体中文(0x0804)下的语言 Profile
DEFINE_GUID(GUID_QingPinProfile,
            0xc5591836, 0x2317, 0x4594, 0x99, 0xcd, 0x0c, 0xde, 0xf0, 0xbd, 0x6e, 0x44);

// {7524A523-7A60-4805-B09E-3FEAB2E11910}  组合文本显示属性(下划线)
DEFINE_GUID(GUID_QingPinAttrInput,
            0x7524a523, 0x7a60, 0x4805, 0xb0, 0x9e, 0x3f, 0xea, 0xb2, 0xe1, 0x19, 0x10);

// ---- 模块级状态 (globals.cpp) ---------------------------------------------

extern HINSTANCE g_hInst;  // DLL 实例句柄
extern LONG g_cRefDll;     // 模块级对象引用计数
extern LONG g_cLockDll;    // IClassFactory::LockServer 计数

// ---- 极简 COM 智能指针 (避免依赖 ATL) --------------------------------------

template <class T>
class QPComPtr {
public:
    QPComPtr() = default;
    ~QPComPtr() { Release(); }
    QPComPtr(const QPComPtr&) = delete;
    QPComPtr& operator=(const QPComPtr&) = delete;

    T** operator&() {
        Release();
        return &ptr_;
    }
    T* operator->() const { return ptr_; }
    operator T*() const { return ptr_; }
    T* Get() const { return ptr_; }

    // 裸指针赋值 (AddRef)
    QPComPtr& operator=(T* p) {
        if (p) p->AddRef();
        Release();
        ptr_ = p;
        return *this;
    }

    void Release() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

// ---- 通用小工具 ------------------------------------------------------------

// 取 DLL 所在目录 (UTF-16), 末尾不带反斜杠
bool GetDllDir(std::wstring* out);

#endif  // QINGPIN_GLOBALS_H_
