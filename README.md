\# HideDem



HideDem is a native Windows tray utility written in C++ that hides selected application windows from screen capture, the taskbar, and the Alt+Tab switcher.



Designed for streaming, screen sharing, and recording workflows where certain windows need to stay private on your desktop without interrupting your work.



\## Key Features



\- \*\*Screen Capture Protection\*\* — makes chosen windows invisible to screen recording software, streaming tools, and screen-sharing calls while keeping them visible to you.

\- \*\*Taskbar \& Alt+Tab Concealment\*\* — removes protected windows from the Windows taskbar and the Alt+Tab window switcher.

\- \*\*Minimize on Hide\*\* — optionally minimizes windows automatically when hiding them and restores them back when revealed.

\- \*\*Global Hotkey\*\* — quickly toggle protection for the active window using `Ctrl+Shift+Q`, with the ability to change the letter directly from the tray menu.

\- \*\*Minimal Resource Usage\*\* — built with pure Win32 API and optimized to consume virtually zero CPU and minimal memory while idling in the background.

\- \*\*Native System Integration\*\* — automatically adapts to Windows Light and Dark themes, supports English and Russian interfaces, and includes an optional Windows startup toggle.



\## Requirements



\- Windows 10 (version 2004 or newer) / Windows 11

\- 64-bit (x64) architecture



\## Usage



1\. Launch `HideDem.exe` — the icon will appear in the system tray.

2\. Focus any window you want to hide and press `Ctrl+Shift+Q` (or pick a window manually from the tray menu).

3\. Press `Ctrl+Shift+Q` again to restore the window, or use \*\*Restore All Windows\*\* in the tray menu.



All protected windows are automatically restored when you exit the application.



\## Building from Source



1\. Open `HideDem.vcxproj` in \*\*Visual Studio 2022\*\* (or newer) with the \*Desktop development with C++\* workload installed.

2\. Select the `Release` configuration and `x64` platform.

3\. Build the solution (`Ctrl+Shift+B`).



\## License



This project is licensed under the \[MIT License](LICENSE).

