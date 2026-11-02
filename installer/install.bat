@echo off
chcp 65001 >nul
echo ============================================
echo   轻拼输入法 (QingPin IME) 安装脚本
echo ============================================
echo.

:: 检查管理员权限
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 请右键此脚本, 选择"以管理员身份运行"。
    pause
    exit /b 1
)

set "SRC=%~dp0.."
set "DEST=%ProgramFiles%\QingPinIME"

:: 检查已编译产物
if not exist "%SRC%\bin\QingPinIME.dll" (
    echo [错误] 未找到 %SRC%\bin\QingPinIME.dll
    echo 请先用 CMake/Visual Studio 编译工程, 再运行本脚本。
    pause
    exit /b 1
)

:: 复制文件到安装目录
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%SRC%\bin\QingPinIME.dll" "%DEST%\" >nul
if exist "%SRC%\bin\qingpin-dicttool.exe" copy /y "%SRC%\bin\qingpin-dicttool.exe" "%DEST%\" >nul
xcopy /y /e /i "%SRC%\dict" "%DEST%\dict\" >nul
copy /y "%SRC%\assets\qingpin.ico" "%DEST%\" >nul

:: 注册 TSF 文本服务 (64 位 DLL -> 64 位程序, 32 位 DLL -> 32 位程序)
set REG_FAIL=0
regsvr32 /s "%DEST%\QingPinIME.dll"
if %errorlevel% neq 0 set REG_FAIL=1
if exist "%DEST%\QingPinIME_x86.dll" (
    regsvr32 /s "%SystemRoot%\SysWOW64\regsvr32.exe" "%DEST%\QingPinIME_x86.dll" 2>nul
    if %errorlevel% neq 0 set REG_FAIL=1
)
if %REG_FAIL% equ 0 (
    echo [成功] 输入法已注册到系统。
    echo.
    echo 下一步: 打开 设置 ^> 时间和语言 ^> 语言和区域
    echo         中文(简体) ^> 语言选项 ^> 添加键盘 ^> 选择"轻拼输入法"
    echo.
    echo 使用提示: 轻敲 Shift 键切换中/英文, 数字键或空格选词。
) else (
    echo [错误] 注册失败, 错误码 %errorlevel%。
    echo 常见原因: 杀毒软件拦截、缺少管理员权限。
)
echo.
pause
