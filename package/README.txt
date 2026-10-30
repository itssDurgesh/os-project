OS Project - Laptop Test (Milestone 0)

This checks that our OS runs on your laptop. It runs inside QEMU,
so it cannot change Windows or your files.

1. Install QEMU for Windows from https://www.qemu.org/download/#windows
   Use the 64-bit installer and the default install folder.
   Use version 10.2.1 if it's available, or the closest version.

2. Unzip this ZIP to C:\OSProject
   Don't use a OneDrive folder or a path with spaces.

3. Double-click run.bat
   A window should open and show "OS Project: hello".

4. Close the window. A new file called boot.log appears in the folder.

5. Send these to the Integration member:
   - boot.log
   - Your CPU type (Settings > System > About > Processor)
   - Your QEMU version (run this in PowerShell):
     & "C:\Program Files\qemu\qemu-system-i386.exe" --version

If something goes wrong:
- If Windows blocks run.bat or QEMU, send a screenshot of the message.
  Don't turn off Smart App Control, because it can't simply be switched
  back on.
- If the mouse gets stuck inside the window, press Ctrl+Alt+G to release it.
