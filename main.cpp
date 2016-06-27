// https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx

#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

#define BUFSIZE 4096
#define MAX_CONSOLE_BUFFER 4096

HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
HANDLE g_hInputFile = NULL;

void CreateChildProcess(LPCWSTR CommandString);
void WriteToPipe(void);
void ReadFromPipe(void);
void ErrorExit(PTSTR);

void Log(HANDLE Console, LPCWSTR Msg) {
    size_t MsgLen = 0;
    if (SUCCEEDED(StringCchLength(Msg, 4096, &MsgLen))) {
        DWORD BytesWritten = 0;
        WriteConsole(Console, Msg, (DWORD)MsgLen, &BytesWritten, nullptr);
    }
}

void LogError(HANDLE Console, LPCWSTR Label, DWORD ErrorCode) {
    WCHAR Msg[MAX_CONSOLE_BUFFER];
    if (SUCCEEDED(StringCbPrintf(Msg, sizeof(Msg), L"%s failed - %08x\n", Label, ErrorCode))) {
        Log(Console, Msg);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
   SECURITY_ATTRIBUTES saAttr;

    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        ErrorExit(TEXT("AttachConsole"));

// Set the bInheritHandle flag so pipe handles are inherited.

   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
   saAttr.bInheritHandle = TRUE;
   saAttr.lpSecurityDescriptor = NULL;

// Create a pipe for the child process's STDOUT.

   if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) )
      ErrorExit(TEXT("StdoutRd CreatePipe"));

// Ensure the read handle to the pipe for STDOUT is not inherited.

   if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
      ErrorExit(TEXT("Stdout SetHandleInformation"));

// Create a pipe for the child process's STDIN.

   if (! CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
      ErrorExit(TEXT("Stdin CreatePipe"));

// Ensure the write handle to the pipe for STDIN is not inherited.

   if ( ! SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0) )
      ErrorExit(TEXT("Stdin SetHandleInformation"));

// Create the child process.

   CreateChildProcess(pCmdLine);

// Write to the pipe that is the standard input for a child process.
// Data is written to the pipe's buffers, so it is not necessary to wait
// until the child process is running before writing data.

   WriteToPipe();

// Read from pipe that is the standard output for child process.

   ReadFromPipe();

// The remaining open handles are cleaned up when this process terminates.
// To avoid resource leaks in a larger application, close handles explicitly.

   return 0;
}

void CreateChildProcess(LPCWSTR CommandString)
// Create a child process that uses the previously created pipes for STDIN and STDOUT.
{
   WCHAR CommandStringCopy[MAX_PATH];
   PROCESS_INFORMATION piProcInfo;
   STARTUPINFO siStartInfo;
   BOOL bSuccess = FALSE;

    if (FAILED(StringCbCopy(CommandStringCopy, sizeof(CommandStringCopy), CommandString))) {
        ErrorExit(TEXT("StringCbCopy"));
    }

// Set up members of the PROCESS_INFORMATION structure.

   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

// Set up members of the STARTUPINFO structure.
// This structure specifies the STDIN and STDOUT handles for redirection.

   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO);
   siStartInfo.hStdError = g_hChildStd_OUT_Wr;
   siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   siStartInfo.hStdInput = g_hChildStd_IN_Rd;
   siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

// Create the child process.

   bSuccess = CreateProcess(NULL,
      CommandStringCopy,     // command line
      NULL,          // process security attributes
      NULL,          // primary thread security attributes
      TRUE,          // handles are inherited
      0,             // creation flags
      NULL,          // use parent's environment
      NULL,          // use parent's current directory
      &siStartInfo,  // STARTUPINFO pointer
      &piProcInfo);  // receives PROCESS_INFORMATION

   // If an error occurs, exit the application.
   if ( ! bSuccess )
      ErrorExit(TEXT("CreateProcess"));
   else
   {
      WaitForSingleObject(piProcInfo.hProcess, INFINITE);

      // Close handles to the child process and its primary thread.
      // Some applications might keep these handles to monitor the status
      // of the child process, for example.

      CloseHandle(piProcInfo.hProcess);
      CloseHandle(piProcInfo.hThread);
   }
}

void WriteToPipe(void)

// Read from a file and write its contents to the pipe for the child's STDIN.
// Stop when there is no more data.
{
   DWORD dwRead, dwWritten;
   CHAR chBuf[BUFSIZE];
   BOOL bSuccess = FALSE;

    if (g_hInputFile != NULL) {
        for (;;)
        {
          bSuccess = ReadFile(g_hInputFile, chBuf, BUFSIZE, &dwRead, NULL);
          if ( ! bSuccess || dwRead == 0 ) break;

          bSuccess = WriteFile(g_hChildStd_IN_Wr, chBuf, dwRead, &dwWritten, NULL);
          if ( ! bSuccess ) break;
        }
    }

// Close the pipe handle so the child process stops reading.

   if ( ! CloseHandle(g_hChildStd_IN_Wr) )
      ErrorExit(TEXT("StdInWr CloseHandle"));
}

void ReadFromPipe(void)

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT.
// Stop when there is no more data.
{
   DWORD dwRead, dwWritten, TotalBytesAvailable;
   CHAR chBuf[BUFSIZE];
   BOOL bSuccess = FALSE;
   HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

   for (;;)
   {
      bSuccess = PeekNamedPipe(g_hChildStd_OUT_Rd, nullptr, 0, nullptr, &TotalBytesAvailable, nullptr);
      if (!bSuccess || !TotalBytesAvailable) break;

      bSuccess = ReadFile( g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL);
      if( ! bSuccess || dwRead == 0 ) break;

      bSuccess = WriteFile(hParentStdOut, chBuf,
                           dwRead, &dwWritten, NULL);
      if (! bSuccess ) break;
   }
}

void ErrorExit(PTSTR lpszFunction) {
    LogError(GetStdHandle(STD_ERROR_HANDLE), lpszFunction, GetLastError());
    ExitProcess(1);
}