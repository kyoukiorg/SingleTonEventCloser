# Roblox Multi-Instance Unlocker

This tool allows you to run multiple instances of Roblox on Windows by closing the internal "singleton" event handle that normally prevents more than one Roblox client from running at the same time.

## What does it do?

- **Finds all running RobloxPlayerBeta.exe processes.**
- **Scans their open handles for the special `ROBLOX_singletonEvent` handle.**
- **Closes this handle in each process, allowing you to launch additional Roblox clients.**
- **Provides a simple GUI with a button to perform the unlock, and a status message.**

This is useful if you want to play on multiple Roblox accounts at once, or need to test things in multiple clients.

## How to Build Manually

### Requirements

- Windows (tested on Windows 10/11)
- MinGW-w64 or Microsoft Visual Studio (MSVC)
- C compiler (GCC or MSVC)
- No external dependencies

### Building with MinGW-w64 (GCC)

1. Open a terminal in the project directory.
2. Run:

   ```
   gcc roblox.c -o SingleTonCloser.exe -lcomctl32 -mwindows
   ```

   - If you get errors about missing `comctl32`, you can remove `-lcomctl32` (it's only needed for some controls).

### Building with Microsoft Visual Studio (MSVC)

1. Open a "Developer Command Prompt for VS".
2. Run:

   ```
   cl roblox.c /Fe:SingleTonCloser.exe /link user32.lib gdi32.lib shell32.lib psapi.lib comctl32.lib
   ```

### Running

- Double-click the resulting `SingleTonCloser.exe` (or run from terminal).
- Click the "Allow Multiple Instances" button.
- If successful, you will see a message and can now open more Roblox clients.

## Notes

- **You must have at least one Roblox client running before using this tool.**
- **You may need to run as Administrator if you get access denied errors.**
- The tool does not patch or modify Roblox files; it only closes a Windows handle in memory.
- A log file (`roblox_kill_log.txt`) is created for troubleshooting.

## Disclaimer

This tool is provided for educational purposes. Use at your own risk. Not affiliated with Roblox Corporation.
