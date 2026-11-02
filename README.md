# 轻拼输入法 (QingPin IME)

一个基于微软 **TSF (Text Services Framework)** 的 Windows 全拼输入法, 使用 C++17 / COM 编写, 支持简体中文输入、Shift 中英切换、用户词自学习与外部词库导入。

> 目标环境: Windows 10 / 11 x64, Visual Studio 2022

---

## 功能特性

| 模块 | 说明 |
| ---- | ---- |
| 全拼解析 | 414 个标准音节、带回溯的音节切分, 支持 `'` 显式分隔 (如 `xi'an` 西安) |
| 候选与翻页 | 词库匹配 + 按词频排序, 每页 5 个候选, `-`/`=` 或 PageUp/Down 或滚轮翻页 |
| 组词造词 | 词库无整词时, 用各音节高频字自动组合候选 (如 `zhangsan` → 张三) |
| 中英切换 | 轻敲 Shift 切换中/英文, 界面弹出「中/英」提示气泡 |
| 全角标点 | 中文模式下 `,。;:?[]` 等自动转全角, 引号自动配对 |
| 用户词自学习 | 每次选词自动调频并持久化到 `%APPDATA%\QingPinIME\userdict.txt` |
| 外部词库导入 | `qingpin-dicttool` 支持搜狗 `.scel` 细胞词库与文本词库导入、合并 |
| 原生体验 | 注册为系统级 TSF 文本服务, 可在任意支持 TSF 的应用中使用 (记事本、浏览器、Office 等) |

## 目录结构

```
QingPinIME/
├── CMakeLists.txt              # 构建脚本 (VS2022 / MSVC)
├── src/
│   ├── engine/                 # ★ 跨平台核心引擎 (可独立编译测试)
│   │   ├── segmenter.h/.cpp    #   拼音音节切分器
│   │   ├── dict.h/.cpp         #   词库加载/匹配/调频
│   │   ├── engine.h/.cpp       #   查询引擎 (候选排序/组词/自学习)
│   │   └── syllables.inc       #   音节表 (由 scripts/gen_dict.py 生成)
│   ├── globals.h/.cpp          # GUID 与模块状态
│   ├── dllmain.cpp             # COM 类工厂 + 4 个标准 DLL 导出
│   ├── registry.h/.cpp         # regsvr32 自注册 (CTF\TIP 注册表树)
│   ├── service.h/.cpp          # ★ TSF 文本服务主体 (事件/按键/组合)
│   ├── ui/candidate_window.*   # 候选窗与提示气泡 (Win32/GDI)
│   └── QingPinIME.def          # DLL 导出表
├── assets/
│   ├── QingPinIME.rc           # 版本信息 + 图标资源
│   └── qingpin.ico             # 程序图标
├── dict/dict.txt               # 内置基础词库 (5282 条)
├── tools/dicttool/main.cpp     # 词库导入/合并工具
├── tests/test_engine.cpp       # 核心引擎单元测试
├── installer/                  # 安装 / 卸载脚本
└── scripts/                    # 词库再生成脚本 (Python)
```

## 编译 (Visual Studio 2022)

### 方式一: CMake 命令行

```bat
cd QingPinIME
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物在 `build\Release\`:
- `QingPinIME.dll` —— 输入法本体
- `qingpin-dicttool.exe` —— 词库工具
- `qingpin-test.exe` —— 引擎单元测试

### 方式二: Visual Studio 打开 CMake 工程

「文件 → 打开 → CMake」选择 `CMakeLists.txt`, 选择 `x64-Release` 配置后「全部生成」。

### 运行单元测试 (建议先跑)

```bat
build\Release\qingpin-test.exe dict\dict.txt %TEMP%\qp_userdict.txt
```

预期输出 `全部通过` (24 项: 音节切分 / pattern 枚举 / 词库查询 / 组词 / 自学习持久化)。

## 安装与卸载

1. 以**管理员身份**运行 `installer\install.bat`
   - 脚本会把 DLL、词库复制到 `%ProgramFiles%\QingPinIME\` 并调用 `regsvr32` 注册;
2. 打开 **设置 → 时间和语言 → 语言和区域 → 中文(简体) → 语言选项 → 添加键盘**, 选择 **轻拼输入法**;
   - 若列表中没有, 注销并重新登录一次再试;
3. 卸载: 以管理员身份运行 `installer\uninstall.bat`。

> 注册表写入位置 (与微软官方输入法示例一致):
> `HKLM\SOFTWARE\Microsoft\CTF\TIP\{4FE60D32-...}` 下的
> `LanguageProfile\0x0804\{C5591836-...}` 与 `Category\{GUID_TFCAT_TIP_KEYBOARD}\...`

## 使用说明

| 按键 | 作用 |
| ---- | ---- |
| `a`–`z` | 开始/继续输入拼音 (组合文本带点状下划线) |
| `'` | 音节分隔, 如 `xi'an` |
| `1`–`5` 或 空格 | 选择候选 (空格 = 首选) |
| `-` / `=`、PageUp / PageDown、鼠标滚轮 | 翻页 |
| `Enter` | 原样上屏拼音字母 |
| `Esc` | 取消本次输入 |
| `Backspace` | 删除一个字母; 全删后取消组合 |
| 轻敲 `Shift` | 中/英文切换 (弹出提示气泡) |
| `Shift` + 字母 | 英文大写字母直通 |
| `,` `.` `;` `?` `/` `[` `]` `\` 等 | 中文标点 (组合中则先上屏首选词再加标点) |
| 鼠标点击 | 直接点选候选词 |

## 词库管理

### 词库文件格式

`dict\dict.txt` 为 UTF-8 文本, 每行:

```
词语<TAB>音节1'音节2'...<TAB>频次
你好      ni'hao  38026
```

- 频次 1–99999, 越大越靠前; `#` 开头的行为注释;
- 用户词自学习写入 `%APPDATA%\QingPinIME\userdict.txt`, 格式相同, 命中时排序权重×50 (选过一两次即可排到前面)。

### 导入搜狗细胞词库 (.scel)

```bat
qingpin-dicttool import-scel 城市大全.scel city.txt
qingpin-dicttool merge dict\dict.txt city.txt dict\dict.txt
```

### 导入文本词库

支持三种行格式 (UTF-8 或 UTF-16LE), 缺拼音时按内置词库自动注音:

```
自定义词           ← 仅词, 频次取 --freq 默认值
大师\t8888         ← 词 + 频次
加油\tjia'you\t777 ← 词 + 拼音 + 频次
```

```bat
qingpin-dicttool import-txt mywords.txt out.txt --dict dict\dict.txt --freq 500
qingpin-dicttool merge dict\dict.txt out.txt dict\dict.txt
```

> `.scel` 解析遵循社区公开的二进制格式描述 (音节表 @0x1540 / 词表 @0x2628), 已用合成样例完成往返测试; 个别非标准格式的词库若解析失败, 可先用第三方工具转成 txt 再导入。

### 重新生成内置词库

```bash
pip3 install pypinyin
python3 scripts/gen_dict.py     # 重写 dict/dict.txt 与 src/engine/syllables.inc
```

字表/词表在 `scripts/common_chars.txt`、`scripts/common_words.txt` 中维护 (按频序排列), 拼音由 pypinyin 生成并按音节表校验。

## 工作原理 (速览)

```
键盘事件 → ITfKeyEventSink (OnTestKeyDown/OnKeyDown 决策, OnKeyUp 捕获 Shift 抬起)
   │            └ Decide(): 中英模式 / 组合状态 / 按键类型 → 动作
   ├─ 字母 → RequestEditSession(同步读写)
   │            ├ EnsureComposition()   插入组合范围并 StartComposition
   │            ├ SetText(显示拼音)     + SetValue(GUID_PROP_ATTRIBUTE) 下划线
   │            └ GetTextExt()          插入点屏幕坐标 → 候选窗定位
   ├─ 选词 → SetText(词语) + EndComposition(FALSE) + Learn() 自学习
   ├─ 取消 → EndComposition(TRUE)
   └─ 翻页/提示 → CandidateWindow (WS_EX_NOACTIVATE 顶层窗, 不抢焦点)
```

关键设计:
- **音节切分**: 414 音节集合 + 记忆化回溯, 最长音节优先; 匹配时枚举"完整切分 + 前缀切分 + 整串 partial"至多 4 种 pattern, 兼顾 `xian`/`xi'an` 与打字一半的场景;
- **词库匹配**: 线性扫描 (5 万级词典单次查询 <0.1ms), 条目自带音节边界, 严格按音节序列匹配而非字符串前缀 (避免 `ni` 误命中 `nian`);
- **自学习**: `BumpOrAdd` 词频 +400/次并立即原子写盘 (临时文件 + rename);
- **Shift 轻敲**: `OnKeyDown(VK_SHIFT)` 记时间戳, 期间无其他键且 `OnKeyUp(VK_SHIFT)` 距按下 <600ms 判定为轻敲 → 切换中英。

## 常见问题

**Q: 安装后输入法列表里找不到?**
确认 `regsvr32` 返回成功; 打开 `regedit` 检查
`HKLM\SOFTWARE\Microsoft\CTF\TIP\{4FE60D32-0B7F-452B-BE8A-3166EB45E0AC}` 是否存在;
注销重登或重启 `ctfmon.exe` (任务管理器结束 ctfmon 后 Win+R 运行 `ctfmon`)。

**Q: 某些程序里打不出字?**
以管理员权限运行的程序需要输入法也以提升权限注册 (UAC 桌面隔离), 这是所有第三方输入法的共同限制。个别使用 RAW Input 或自绘键盘的游戏不经过 TSF, 无法支持。

**Q: 候选窗位置不对?**
候选窗跟随 TSF 报告的插入点矩形; 个别自绘界面 (如某些 IDE) 的 `GetTextExt` 返回值异常, 已内置 `GetGUIThreadInfo` 兜底, 仍异常时可先切到英文模式规避。

**Q: 词库能换成超大词库吗?**
可以, 保持 TSV 格式替换 `dict\dict.txt` 即可; 超过 10 万条建议自行把 `Dict::Collect` 的线性扫描替换为 trie 索引。

**Q: 杀毒软件报风险?**
未签名 DLL 注册为全局输入法时, SmartScreen/杀软可能提示。签名或加入白名单即可。

## 已知限制 (MVP)

- 单字/词频数据为人工整理的基础词库 (约 5300 条), 生产使用建议导入专业词库;
- 暂无模糊音、双拼、整句智能纠错、云候选;
- Shift 轻敲依赖应用上报 `OnKeyUp`, 少数不经过 TSF 键盘管线的程序中无效;
- 用户词典多进程并发写时以最后写入者为准。

## 许可说明

- 本项目代码: MIT License
- 词库拼音数据: 由 [pypinyin](https://github.com/mozillazg/pypinyin) (MIT) 生成
- 图标: 本项目生成, 随代码分发
