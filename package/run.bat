@echo off
setlocal
cd /d "%~dp0"

set "QEMU=C:\Program Files\qemu\qemu-system-i386.exe"

if not exist "%QEMU%" (
    echo QEMU was not found at "%QEMU%".
    echo Install QEMU for Windows, then run this file again.
    pause
    exit /b 1
)

"%QEMU%" -machine pc -cpu qemu32 -m 128M -drive file=os.img,format=raw,if=ide -snapshot -no-reboot -display gtk,zoom-to-fit=on -serial file:boot.log
