# OS Project Setup Guide

This guide explains how to set up and run the OS on a Windows 11 laptop.

The OS runs inside an emulator, so it can't change Windows or your files. Never
install it directly on a laptop. It's made for old BIOS PCs with IDE disks, so it
won't start on a modern laptop, and writing it to a real disk could erase Windows.

## Choose a method

| Method | Use it if | Needs | Status |
| --- | --- | --- | --- |
| [1. Run the ready-made OS in Ubuntu](#method-1-run-the-ready-made-os-in-ubuntu) | You just want to use the OS | `os-demo.zip` | Tested |
| [2. Build it from the source code](#method-2-build-it-from-the-source-code) | You want to change the code | Access to the GitHub repo | Tested |
| [3. QEMU for Windows](#method-3-qemu-for-windows) | You don't want to install Ubuntu | `os-demo.zip` | Not tested yet |
| [4. VirtualBox](#method-4-virtualbox) | You already use VirtualBox | `os-demo.zip` | Not tested yet |

Start with Method 1 if you aren't sure. Get `os-demo.zip` from the project owner.

## Which terminal am I in?

Most mistakes come from typing a command in the wrong terminal. Look at the start of
the line:

| The line starts with | You're in | What runs here |
| --- | --- | --- |
| `PS C:\...` | Windows PowerShell | `wsl --install`, `wsl --shutdown`, `wsl ~` |
| `yourname@yourlaptop` | Ubuntu | `sudo`, `make`, `cd`, `code .`, `qemu-system-i386` |
| `/ #` or `/home/student $` | Our OS | `ls`, `edit`, `run`, `ps`, and so on |

Type `wsl ~` in PowerShell to enter Ubuntu, and `exit` to go back.

## Install Ubuntu (Methods 1 and 2)

1. Click **Start**, type `PowerShell`, right-click it and choose **Run as
   administrator**.
2. Run this command. Restart the laptop if it asks you to.

   ```text
   wsl --install -d Ubuntu
   ```

3. Open **Ubuntu** from the Start menu, then create a username and password.
   Nothing shows on screen while you type the password, which is normal.

## Method 1: Run the ready-made OS in Ubuntu

Put `os-demo.zip` in your Windows **Downloads** folder, then run these in Ubuntu.
Replace `YOUR_WINDOWS_NAME` with the name of your folder in `C:\Users`.

```bash
sudo apt update
sudo apt install -y qemu-system-x86 qemu-system-gui unzip
mkdir -p ~/os
cp /mnt/c/Users/YOUR_WINDOWS_NAME/Downloads/os-demo.zip ~/os/
cd ~/os
unzip -o os-demo.zip
```

Start the OS. A window opens with a login prompt:

```bash
qemu-system-i386 -machine pc -cpu qemu32 -m 128M -drive file=os.img,format=raw,if=ide -snapshot -no-reboot
```

Changes are thrown away when you close the window. To keep your files between runs,
leave out `-snapshot`:

```bash
qemu-system-i386 -machine pc -cpu qemu32 -m 128M -drive file=os.img,format=raw,if=ide -no-reboot
```

Before closing the window, type `exit` and wait a moment. Next time, start from step
`cd ~/os`.

## Method 2: Build it from the source code

The GitHub repo is private, so ask the owner to invite you first.

**1. Get the code.** After you accept the invite, the easiest way is to open the
[repo page](https://github.com/itssDurgesh/os-project), click **Code**, then
**Download ZIP**, and save it in your Windows **Downloads** folder.

**2. Unzip it in Ubuntu,** replacing `YOUR_WINDOWS_NAME`:

```bash
sudo apt update
sudo apt install -y unzip
cd ~
unzip -o /mnt/c/Users/YOUR_WINDOWS_NAME/Downloads/os-project-main.zip
mv os-project-main os-project
cd ~/os-project
```

To use Git instead, run `git clone https://github.com/itssDurgesh/os-project.git`.
GitHub doesn't accept your password here, so it needs a personal access token.

**3. Install the tools** listed in `requirements.txt`:

```bash
sed 's/#.*//' requirements.txt | xargs sudo apt install -y
```

**4. Build and start the OS:**

```bash
make run
```

Keep the code in your Ubuntu home folder and not in `/mnt/c`, because it builds
faster there and a space in a folder name can break the Makefile.

| Command | What it does |
| --- | --- |
| `make` | Builds the kernel, the programs and the disk image |
| `make run` | Builds and starts the OS |
| `make test` | Runs every automatic test (about 6 minutes) |
| `make dist` | Makes `build/os-demo.zip` to share |
| `make clean` | Deletes everything that was built |

To add your own program, put a `.cpp` file in `user/bin` and run `make run`. It
becomes a command inside the OS.

### Open the code in VS Code

1. In PowerShell, install the WSL extension once:

   ```text
   code --install-extension ms-vscode-remote.remote-wsl
   ```

2. In Ubuntu, open the project:

   ```bash
   cd ~/os-project && code .
   ```

The first time, VS Code downloads a helper into Ubuntu, so it needs internet. When
it's ready, the bottom-left corner shows **WSL: Ubuntu**.

## Method 3: QEMU for Windows

This method hasn't been tested yet.

1. Install the 64-bit version from <https://www.qemu.org/download/#windows> and keep
   the default install folder.
2. Unzip `os-demo.zip` to `C:\OSProject`. Avoid OneDrive folders and paths with spaces.
3. Double-click `run.bat`.

Press **Ctrl+Alt+G** to release your mouse from the window.

## Method 4: VirtualBox

This method hasn't been tested yet.

1. Unzip `os-demo.zip` to `C:\OSProject`.
2. VirtualBox needs a `.vdi` disk, so convert the image in PowerShell:

   ```text
   & "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" convertfromraw C:\OSProject\os.img C:\OSProject\os.vdi --format VDI
   ```

3. Create a new virtual machine with these settings:

   | Setting | Value |
   | --- | --- |
   | Type | Other |
   | Version | Other/Unknown (32-bit if offered) |
   | Memory | 128 MB or more |
   | Processors | 1 |
   | EFI | Off |
   | Hard disk | Use an existing file: `C:\OSProject\os.vdi` |
   | Storage controller | IDE (PIIX4), not SATA |
   | Pointing device | PS/2 Mouse |

4. Press **Start**. Files stay in the `.vdi` between runs.

If it runs very slowly, WSL or Hyper-V is probably sharing the laptop's virtualization.

## Using the OS

Log in with a demo account. The password stays hidden while you type it.

| Username | Password | Access |
| --- | --- | --- |
| `root` | `root` | Can do anything |
| `student` | `student` | Normal user, limited by file permissions |

These accounts belong to the OS, so your Windows or Ubuntu password won't work.
Type `help` to see the commands, and `exit` to log out.

| Area | Commands |
| --- | --- |
| Files | `ls`, `ls -l`, `cat`, `cp`, `mv`, `rm`, `touch`, `mkdir`, `rmdir`, `head`, `grep`, `wc` |
| Folders | `cd`, `pwd` |
| Editing | `edit file.txt` |
| C++ | `c++ file.cpp -o name`, `run file.cpp` |
| System | `ps`, `free`, `kill`, `sleep`, `whoami`, `chmod`, `clear`, `keytest` |
| Shell | `a \| b`, `a > file`, `a >> file`, `a < file`, `a &` |

**Keys:** Up and Down bring back old commands. Left, Right, Backspace, Delete, Home
and End edit the line. Ctrl+C stops a program. In the editor, Ctrl+S saves and
Ctrl+Q quits.

### Write and run a C++ program

```text
edit /tmp/hello.cpp
run /tmp/hello.cpp
```

Type this in the editor, press Ctrl+S to save and Ctrl+Q to quit, then use `run`:

```cpp
int main() {
    string name;
    cout << "Your name: ";
    cin >> name;
    cout << "Hello, " << name << endl;
}
```

More examples are in `/examples`, such as `run /examples/fibonacci.cpp`.

The compiler supports `int`, `char`, `bool`, `string`, arrays, functions, recursion,
`if`, `for`, `while`, `do`, `cin`, `cout` and `getline`. It doesn't support classes,
structs, pointers, `vector`, `switch`, or reading and writing files from C++.

Files in `/tmp` are lost when the OS closes. To keep your work, start the OS
without `-snapshot` and save in another folder, such as `/home/student`.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| `Command 'wsl' not found` | You're in Ubuntu. Type `exit`, then run it in PowerShell. Don't install the `wsl` package. |
| `The token '&&' is not a valid statement separator` | You're in PowerShell. Type `wsl ~` first. |
| `Login incorrect` | Use `root` / `root` or `student` / `student`. The password box is the second prompt. |
| `no such file or directory` when saving | The folder doesn't exist. The temp folder is `/tmp`, not `/temp`. |
| The keyboard does nothing | Click inside the QEMU window first. |
| The mouse is stuck in the window | Press Ctrl+Alt+G. |
| A key doesn't work | Run `keytest` in the OS and press the key. An arrow key should show 27, 91, and a letter. |
| Your files disappeared | That run used `-snapshot`, which throws changes away. |
| Windows blocks QEMU or `run.bat` | Send a screenshot to the project owner. Don't turn off Smart App Control. |
| WSL says virtualization is off | Open **Turn Windows features on or off**, tick **Virtual Machine Platform** and **Windows Subsystem for Linux**, then restart. |
| `pip install -r requirements.txt` fails | Those are Ubuntu packages. Use the install command in Method 2. |

### Ubuntu has no internet

Errors like `No route to host` or `Could not connect to server` mean Ubuntu has lost
its network, which often happens after the laptop sleeps or the Wi-Fi changes. Check
that the internet works in Windows, and log in to your Wi-Fi again if it asks.

Then run this in PowerShell, wait 10 seconds and open Ubuntu again:

```text
wsl --shutdown
```

If it still fails, run these in PowerShell to make Ubuntu share Windows' network:

```powershell
Set-Content -Path "$env:USERPROFILE\.wslconfig" -Value "[wsl2]`nnetworkingMode=mirrored"
wsl --shutdown
```

## Removing everything

Push or copy anything you want to keep first. The first command permanently deletes
everything inside Ubuntu. Your Windows files stay safe. Run these in PowerShell as
Administrator:

```text
wsl --unregister Ubuntu
wsl --uninstall
```
