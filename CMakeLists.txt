cmake_minimum_required(VERSION 3.20)
project(QingPinIME VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(WARNING "QingPinIME 是 Windows 平台项目: 请在 Windows + Visual Studio 2022 下构建 (详见 README.md)")
endif()

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

# 全部目标统一 UTF-8 源码编码 (否则英文系统上中文字面量会被按 ANSI 码页误读)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- 跨平台核心引擎 (词库/切分/查询, 也被测试与词库工具使用) ----------------
add_library(qpcore STATIC
    src/engine/segmenter.cpp
    src/engine/dict.cpp
    src/engine/engine.cpp)
target_include_directories(qpcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---- TSF 输入法主 DLL -------------------------------------------------------
add_library(QingPinIME SHARED
    src/globals.cpp
    src/dllmain.cpp
    src/registry.cpp
    src/service.cpp
    src/ui/candidate_window.cpp)

if(MSVC)
    # .def 文件定义 4 个 COM 导出
    set_target_properties(QingPinIME PROPERTIES
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/QingPinIME.def")
    target_compile_options(QingPinIME PRIVATE /W4 /permissive- /utf-8)
    # .rc 由 VS 生成器自动处理; 其他生成器需启用 RC 语言
    enable_language(RC)
    target_sources(QingPinIME PRIVATE assets/QingPinIME.rc)
endif()

target_link_libraries(QingPinIME PRIVATE
    qpcore
    user32 gdi32 advapi32 ole32 oleaut32 shell32 uuid)

set_target_properties(QingPinIME PROPERTIES OUTPUT_NAME "QingPinIME" PREFIX "")

# ---- 词库工具 (搜狗 .scel / 文本导入、合并) ---------------------------------
add_executable(qingpin-dicttool tools/dicttool/main.cpp)
target_link_libraries(qingpin-dicttool PRIVATE qpcore)

# ---- 核心引擎单元测试 -------------------------------------------------------
add_executable(qingpin-test tests/test_engine.cpp)
target_link_libraries(qingpin-test PRIVATE qpcore)

# ---- 安装: bin/ 下放置 dll + dict/ ------------------------------------------
install(TARGETS QingPinIME qingpin-dicttool RUNTIME DESTINATION bin)
install(DIRECTORY dict DESTINATION bin)
install(FILES assets/qingpin.ico DESTINATION bin)
