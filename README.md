# Overview

This project SUDO is a sudo-like program for Windows, that allows users to run non-interactive console programs with the elevated privileges from a non-elevated command prompt.

## Usage

### Requirements

- Windows
- Git for Windows
- Visual Studio

### Supported Platforms

- Windows 7 and later

I have not tested on Vista, but I hope this works on Vista, too.

### Building SUDO

Open a Developer Command Prompt of your Visual Studio, then:

    > cd <your favourite directory for git repos>
    > git clone https://github.com/msmania/sudo.git
    > cd sudo
    > nmake

Binaries can be found in a sub-folder .\bin or .\bin64.

### Running SUDO

Just add sudo.exe in front of a command you want to run with the elevated privileges on a non-privileged command prompt.
When you run the command with sudo.exe, the UAC prompt is displayed.  If you click Yes, the output from the target command is displayed on your non-privileged command prompt.

    > sudo.exe powershell "get-vm | ?{$_.State -eq 'Running'}"

    [sudo] START
    Spawning a process (Timeout = 5 sec.)

    Name State   CPUUsage(%) MemoryAssigned(M) Uptime           Status             Version
    ---- -----   ----------- ----------------- ------           ------             -------
    VM1  Running 3           4096              00:00:12.0970000 Operating normally 7.1

    [sudo] END
    Press Enter to exit ...

    >
