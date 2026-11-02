// 轻拼输入法 —— 注册/反注册 (regsvr32 调用)
// 写入的注册表键与微软 ChsIME 示例、Rime Weasel 等开源输入法一致:
//   HKLM\SOFTWARE\Microsoft\CTF\TIP\{CLSID}
//     ├─ LanguageProfile\0x0804\{ProfileGUID}   (Enable=1 使其出现在语言栏)
//     ├─ Category\{GUID_TFCAT_TIP_KEYBOARD}\{ProfileGUID}
//     └─ Category\{GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT}\{ProfileGUID}
#ifndef QINGPIN_REGISTRY_H_
#define QINGPIN_REGISTRY_H_

#include <windows.h>

HRESULT RegisterProfiles();
HRESULT UnregisterProfiles();

#endif  // QINGPIN_REGISTRY_H_
