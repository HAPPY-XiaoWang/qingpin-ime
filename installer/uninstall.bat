@echo off
chcp 65001 >nul
echo ============================================
echo   轻拼输入法 (QingPin IME) 卸载脚本
echo ============================================
echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 请右键此脚本, 选择"以管理员身份运行"。
    pause
    exit /b 1
)

set "DEST=%ProgramFiles%\QingPinIME"

if exist "%DEST%\QingPinIME.dll" (
    regsvr32 /s /u "%DEST%\QingPinIME.dll"
    echo [成功] 输入法已从系统注销。
) else (
    echo [提示] 未找到已安装的输入法。
)
if exist "%DEST%\QingPinIME_x86.dll" (
    regsvr32 /s /u "%SystemRoot%\SysWOW64\regsvr32.exe" "%DEST%\QingPinIME_x86.dll" 2>nul
)

:: 清理用户词库 (如需保留请注释掉下一行)
if exist "%APPDATA%\QingPinIME" rmdir /s /q "%APPDATA%\QingPinIME"

if exist "%DEST%" rmdir /s /q "%DEST%"
echo [完成] 程序文件已删除。
echo 建议注销或重启一次, 使输入法列表彻底刷新。
echo.
pause
