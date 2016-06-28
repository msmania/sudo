// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx

#include <windows.h>
#include <strsafe.h>

#define MAX_CONSOLE_BUFFER 4096
#define CHILD_PROCESS_EXECUTION_TIMEOUT 5000

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
                        HANDLE ChildStderr) {
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
        if (CreateProcess(nullptr, CommandStringCopy, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, CHILD_PROCESS_EXECUTION_TIMEOUT);

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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    CPipe SharedStdout, SharedStdin;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        Log.FallbackToMessageBox = true;
        Log.Error(L"AttachConsole failed - %08x\nProbably the parent process does not have a console.", GetLastError());
        Log.FallbackToMessageBox = false;
        goto Exit;
    }

    Log.Init();
    Log.Info(L"\n[sudo] START\n");

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
    if (!CreateChildProcess(pCmdLine,
                            SharedStdin.R(),
                            SharedStdout.W(),
                            SharedStdout.W())) {
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