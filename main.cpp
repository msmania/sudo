// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx

#include <windows.h>
#include <strsafe.h>
#include <string>
#include <map>

#define MAX_CONSOLE_BUFFER 4096
#define CHILD_PROCESS_EXECUTION_TIMEOUT_DEFAULT 5
#define CHILD_PROCESS_EXECUTION_TIMEOUT_MAX 60

class CLog {
private:
    HANDLE mStdOut;
    HANDLE mStdErr;
    LPWSTR MessageBuffer;

    void LogInternal(HANDLE Console, LPCWSTR Prefix, LPCWSTR MsgFmt, va_list args) {
        if (MessageBuffer != nullptr) {
            size_t Len = 0;
            if (Prefix == nullptr
                || FAILED(StringCchLength(Prefix, MAX_CONSOLE_BUFFER, &Len))) {
                Len = 0;
            }

            LPWSTR p = MessageBuffer;
            DWORD BytesLeft = MAX_CONSOLE_BUFFER;
            if (Len > 0
                && SUCCEEDED(StringCchCopy(MessageBuffer, MAX_CONSOLE_BUFFER, Prefix))) {
                p += (DWORD)Len;
                BytesLeft -= (DWORD)Len;
            }

            if (SUCCEEDED(StringCchVPrintf(p, BytesLeft, MsgFmt, args))
                && SUCCEEDED(StringCchLength(MessageBuffer, MAX_CONSOLE_BUFFER, &Len))) {
                DWORD BytesWritten = 0;
                if (Console != nullptr) {
                    WriteConsole(Console, MessageBuffer, (DWORD)Len, &BytesWritten, nullptr);
                }
#ifdef USE_USER32
                else if (FallbackToMessageBox) {
                    MessageBox(nullptr, MessageBuffer, L"SUDO", MB_OK);
                }
#endif
            }
        }
    }

    void Release() {
        if (MessageBuffer != nullptr) {
            HeapFree(GetProcessHeap(), 0, MessageBuffer);
            MessageBuffer = nullptr;
        }
    }

public:
    bool FallbackToMessageBox;

    CLog()
        : mStdOut(nullptr),
          mStdErr(nullptr),
          MessageBuffer(nullptr),
          FallbackToMessageBox(false) {
        MessageBuffer = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, MAX_CONSOLE_BUFFER * sizeof(WCHAR));
    }

    void Init() {
        mStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        mStdErr = GetStdHandle(STD_ERROR_HANDLE);
    }

    virtual ~CLog() {
        Release();
    }

    void Info(LPCWSTR MsgFmt, ...) {
        va_list args;
        va_start(args, MsgFmt);
        LogInternal(mStdOut, nullptr, MsgFmt, args);
        va_end(args);
    }

    void Error(LPCWSTR MsgFmt, ...) {
        va_list args;
        va_start(args, MsgFmt);
        LogInternal(mStdErr, L"E>", MsgFmt, args);
        va_end(args);
    }
};

CLog Log;

class CPipe {
private:
    HANDLE mPipeForRead;
    HANDLE mPipeForWrite;

    void Release() {
        if (mPipeForRead != nullptr) {
            CloseHandle(mPipeForRead);
            mPipeForRead = nullptr;
        }
        if (mPipeForWrite != nullptr) {
            CloseHandle(mPipeForWrite);
            mPipeForWrite = nullptr;
        }
    }

public:
    CPipe() : mPipeForRead(nullptr), mPipeForWrite(nullptr) {}

    bool Create(bool InheritFlagForRead, bool InheritFlagForWrite) {
        bool Ret = false;
        Release();

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = nullptr;
        sa.bInheritHandle = TRUE;

        if (CreatePipe(&mPipeForRead, &mPipeForWrite, &sa, 0)) {
            if (SetHandleInformation(mPipeForRead, HANDLE_FLAG_INHERIT, InheritFlagForRead ? HANDLE_FLAG_INHERIT : 0)
                && SetHandleInformation(mPipeForRead, HANDLE_FLAG_INHERIT, InheritFlagForWrite ? HANDLE_FLAG_INHERIT : 0)) {
                Ret = true;
            }
            else {
                Log.Error(L"SetHandleInformation failed - %08x\n", GetLastError());
            }
        }
        else {
            Log.Error(L"CreatePipe failed - %08x\n", GetLastError());
        }

        if (!Ret) Release();

        return Ret;
    }

    virtual ~CPipe() {
        Release();
    }

    HANDLE R() { return mPipeForRead; }
    HANDLE W() { return mPipeForWrite; }
};

// Create a child process that uses the previously created pipes for STDIN and STDOUT.
bool CreateChildProcess(LPCWSTR CommandString,
                        HANDLE ChildStdin,
                        HANDLE ChildStdout,
                        HANDLE ChildStderr,
                        DWORD TimeoutInSec) {
    bool Ret = false;
    WCHAR CommandStringCopy[MAX_PATH];

    if (SUCCEEDED(StringCbCopy(CommandStringCopy, sizeof(CommandStringCopy), CommandString))) {
        PROCESS_INFORMATION pi = {};
        STARTUPINFO si = {};

        // Set up members of the STARTUPINFO structure.
        // This structure specifies the STDIN and STDOUT handles for redirection.
        si.cb = sizeof(STARTUPINFO);
        si.hStdError = ChildStderr;
        si.hStdOutput = ChildStdout;
        si.hStdInput = ChildStdin;
        si.dwFlags |= STARTF_USESTDHANDLES;

        // Create the child process.
        Log.Info(L"Spawning a process (Timeout = %d sec.)\n", TimeoutInSec);
        if (CreateProcess(nullptr, CommandStringCopy, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, TimeoutInSec * 1000);

            // Close handles to the child process and its primary thread.
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            Ret = true;
        }
        else {
            Log.Error(L"CreateProcess failed - %08x\n", GetLastError());
        }
    }
    else {
        Log.Error(L"StringCbCopy failed - %08x\n", GetLastError());
    }
    return Ret;
}

// Placeholder
// We can write data to ChildStdin to pass it to STDIN of the child process.
bool WriteToPipe(HANDLE ChildStdin) {
    UNREFERENCED_PARAMETER(ChildStdin);
    return true;
}

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT.
// Stop when there is no more data.
bool ReadFromPipe(HANDLE ChildStdout) {
    HANDLE StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD BytesRead, BytesWritten, TotalBytesAvailable;
    BYTE Buffer[MAX_CONSOLE_BUFFER];
    do {
        if (!PeekNamedPipe(ChildStdout, nullptr, 0, nullptr, &TotalBytesAvailable, nullptr)) {
            Log.Error(L"PeekNamedPipe failed - %08x\n", GetLastError());
            break;
        }
        if (TotalBytesAvailable == 0) break;
        if (!ReadFile(ChildStdout, Buffer, sizeof(Buffer), &BytesRead, nullptr)) {
            Log.Error(L"ReadFile failed - %08x\n", GetLastError());
            break;
        }
        if (!WriteFile(StdOut, Buffer, BytesRead, &BytesWritten, nullptr)) {
            Log.Error(L"WriteFile failed - %08x\n", GetLastError());
            break;
        }
    } while(TotalBytesAvailable > 0);
    return TotalBytesAvailable == 0;
}

class CCommandOptions {
private:
    enum CharType {
        Whitespace,
        Prefix,
        EOS,
        Other,
    };
    enum SupportedOption {
        empty,
        timeout,
        showhelp,
    };

    static std::map<std::wstring, SupportedOption> SupportedOptions;

    static CharType GetCharType(wchar_t c) {
        CharType t = Other;
        switch(c) {
            case L' ': t = Whitespace; break;
            case L'/': t = Prefix; break;
            case L'\0': t = EOS; break;
            default: t = Other; break;
        }
        return t;
    }

    class Extract {
    private:
        PWCHAR Start;
        PWCHAR End;
        WCHAR OriginalChar;
    public:
        Extract(PWCHAR start, PWCHAR end) : Start(start), End(end) {
            OriginalChar = *End;
            *End = L'\0';
        }
        ~Extract() {
            *End = OriginalChar;
        }
        LPCWSTR str() {return Start;}
    };

    // isKey=true:  return true means we're good to go to the next state
    // isKey=false: return true means the value was processed correctly
    bool Push(bool isKey, LPCWSTR s) {
        static SupportedOption pushedKey = empty;
        bool Ret = false;
        if (isKey) {
            std::map<std::wstring, SupportedOption>::iterator it = SupportedOptions.find(s);
            if (it != SupportedOptions.end()) {
                pushedKey = it->second;
                switch(pushedKey) {
                case showhelp:
                    _ShowHelp = true;
                    Ret = true;
                    break;
                case timeout:
                    break;
                default:
                    _Invalid = true;
                    break;
                }
            }
            else {
                _Invalid = true;
            }
        }
        else {
            DWORD dw;
            switch(pushedKey) {
            case timeout:
                dw = _wtoi(s);
                if (dw > 0) {
                    _Timeout = min(dw, CHILD_PROCESS_EXECUTION_TIMEOUT_MAX);
                    Ret = true;
                }
                else {
                    _Invalid = true;
                }
                break;
            default:
                Ret = false;
                break;
            }
            pushedKey = empty;
        }
        return Ret;
    }

public:
    bool _Invalid;
    bool _ShowHelp;
    DWORD _Timeout;
    PWSTR _Cmdline;

    static void Init() {
        SupportedOptions.insert(std::pair<std::wstring, SupportedOption>(L"/t", timeout));
        SupportedOptions.insert(std::pair<std::wstring, SupportedOption>(L"/?", showhelp));
    }

    CCommandOptions(PWSTR CmdLine)
        : _Invalid(true),
          _ShowHelp(false),
          _Timeout(CHILD_PROCESS_EXECUTION_TIMEOUT_DEFAULT),
          _Cmdline(nullptr) {
        PWCHAR p = CmdLine;
        PWCHAR anchor = nullptr;
        enum State {Start, Key, Value, Finish} s = Start;
        bool FindValue = false;

        while (s != Finish) {
            CharType t = GetCharType(*p);
            if (s == Start && t == Whitespace) {
                ++p;
            }
            else if (s == Start && t == Prefix) {
                anchor = p;
                s = Key;
                ++p;
            }
            else if (s == Start && t == EOS) {
                if (FindValue) {
                    _Invalid = true;
                }
                else {
                    _Invalid = false;
                }
                _Cmdline = p;
                s = Finish;
            }
            else if (s == Start && t == Other) {
                if (FindValue) {
                    anchor = p;
                    s = Value;
                    ++p;
                }
                else {
                    _Invalid = false;
                    _Cmdline = p;
                    s = Finish;
                }
            }
            else if (s == Key && t == Whitespace) {
                Extract e(anchor, p);
                if (Push(/*isKey*/true, e.str())) {
                    FindValue = false;
                }
                else {
                    FindValue = true; // Find a value in next iteration
                }
                s = Start;
                ++p;
            }
            else if (s == Key && t == Prefix) {
                ++p;
            }
            else if (s == Key && t == EOS) {
                Extract e(anchor, p);
                if (Push(/*isKey*/true, e.str())) {
                    _Invalid = false;
                }
                else {
                    _Invalid = true;
                }
                _Cmdline = p;
                s = Finish;
            }
            else if (s == Key && t == Other) {
                ++p;
            }
            else if (s == Value && t == Whitespace) {
                Extract e(anchor, p);
                if (Push(/*isKey*/false, e.str())) {
                    s = Start;
                    FindValue = false;
                    ++p;
                }
                else {
                    _Cmdline = anchor;
                    s = Finish;
                }
            }
            else if (s == Value && t == Prefix) {
                ++p;
            }
            else if (s == Value && t == EOS) {
                Extract e(anchor, p);
                if (Push(/*isKey*/false, e.str())) {
                    _Cmdline = p;
                }
                else {
                    _Cmdline = anchor;
                }
                s = Finish;
            }
            else if (s == Value && t == Other) {
                ++p;
            }
        }
    }
};

std::map<std::wstring, CCommandOptions::SupportedOption> CCommandOptions::SupportedOptions;

void Test_CCommandOptions() {
    WCHAR TestStrings[][64] = {
        L"/t 10 ipconfig /all",
        L"/t 10  /? \"C:\\Program Files\\Internet Explorer\\iexplore.exe\"",
        L"/t  10  //a    //? 10 cmd /c echo hello",
        L"//t 10 b //a ipconfig /all",
        L"    /t 10 cmd /c echo hello",
        L"abc /t 10 cmd /c echo hello",
        L"",
        L"  ",
        L"/",
        L"/  ",
        L"/t",
        L"/?",
        L"/? a",
        L"/t a",
        L"/a ",
        L"  /a",
        L"  //a  ",
        L"////",
        L"////  ",
    };
    for (auto &s : TestStrings) {
        Log.Info(L"\n[%s]\n", s);
        CCommandOptions opt(s);
        if (!opt._Invalid) {
            Log.Info(L"timeout = %d, showhelp = %c, Command = [%s]\n",
                opt._Timeout,
                opt._ShowHelp ? L'T' : L'F',
                opt._Cmdline);
        }
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    CCommandOptions::Init();

    CCommandOptions Opt(pCmdLine);
    CPipe SharedStdout, SharedStdin;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        Log.FallbackToMessageBox = true;
        Log.Error(L"AttachConsole failed - %08x\nProbably the parent process does not have a console.", GetLastError());
        Log.FallbackToMessageBox = false;
        goto Exit;
    }

    Log.Init();
    Log.Info(L"\n[sudo] START\n");

#ifdef _TEST
    Test_CCommandOptions();
    goto Exit;
#endif

    if (Opt._Invalid || Opt._ShowHelp) {
        if (Opt._Invalid)
            Log.Info(L"sudo - invalid arguments\n\n");
        else
            Log.Info(L"sudo - execute an elevated command\n\n");
        Log.Info(
            L"usage: sudo [/?]\n"
            L"usage: sudo [/t timeout] cmd [arguments]\n\n"
            L"Options:\n"
            L"  /?    display help message and exit\n"
            L"  /t    specify a timeout in seconds to wait for a child process to end\n"
        );
        goto Exit;
    }

    // Create a pipe for the child process's STDOUT.
    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if (!SharedStdout.Create(/*Read*/false, /*Write*/true)) {
        Log.Error(L"Failed to prepare a pipe for the child process's STDOUT\n");
        goto Exit;
    }

    // Create a pipe for the child process's STDIN.
    if (!SharedStdin.Create(/*Read*/true, /*Write*/false)) {
        Log.Error(L"Failed to prepare a pipe for the child process's STDIN\n");
        goto Exit;
    }

    // Create the child process.
    if (!CreateChildProcess(Opt._Cmdline,
                            SharedStdin.R(),
                            SharedStdout.W(),
                            SharedStdout.W(),
                            Opt._Timeout)) {
        Log.Error(L"Failed to launch a child process\n");
        goto Exit;
    }

    // Read from pipe that is the standard output for child process.
    if (!ReadFromPipe(SharedStdout.R())) {
        Log.Error(L"Failed to read data from STDOUT of the child process\n");
        goto Exit;
    }

Exit:
    Log.Info(L"\n[sudo] END\nPress Enter to exit ...\n");
    return 0;
}