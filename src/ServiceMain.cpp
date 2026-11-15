#include "DesktopApp.h"
#include <Logger.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <iostream>

namespace {
bool GetExecutablePath(std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, _countof(buffer));
    if (length == 0 || length >= _countof(buffer)) {
        const DWORD error = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        CLogger::LogError(L"GetModuleFileNameW failed", error);
        SetLastError(error);
        return false;
    }
    path.assign(buffer, length);
    return true;
}

void AttachToParentConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

void PrintFailure(const wchar_t* operation) {
    const DWORD error = GetLastError();
    CLogger::LogError(operation, error);
    std::wcerr << operation << L", error: " << error << std::endl;
}

int RunCommandLine(int argc, wchar_t* argv[]) {
    AttachToParentConsole();
    CLogger::Initialise(0);
    CLogger::Message(0, L"Process started with %d argument(s)", argc - 1);

    const wchar_t* command = argc >= 2 ? argv[1] : L"--start";
    std::wstring executablePath;
    if (!GetExecutablePath(executablePath)) {
        PrintFailure(L"Unable to determine executable path");
        return 1;
    }

    if (_wcsicmp(command, L"--install") == 0) {
        if (!DesktopApp::InstallStartup(executablePath)) {
            PrintFailure(L"Startup registration failed");
            return 1;
        }
        std::wcout << L"ScreenStream will start when this user signs in." << std::endl;
        return 0;
    }
    if (_wcsicmp(command, L"--uninstall") == 0) {
        if (!DesktopApp::RequestStop(true) || !DesktopApp::RemoveStartup()) {
            PrintFailure(L"Unable to stop or remove ScreenStream startup entry");
            return 1;
        }
        std::wcout << L"ScreenStream startup entry removed." << std::endl;
        return 0;
    }
    if (_wcsicmp(command, L"--start") == 0) {
        if (!DesktopApp::StartWatchdog(executablePath)) {
            PrintFailure(L"Unable to start ScreenStream watchdog");
            return 1;
        }
        std::wcout << L"ScreenStream watchdog started (or is already running)." << std::endl;
        return 0;
    }
    if (_wcsicmp(command, L"--stop") == 0) {
        if (!DesktopApp::RequestStop()) {
            PrintFailure(L"Unable to stop ScreenStream");
            return 1;
        }
        std::wcout << L"ScreenStream stop requested." << std::endl;
        return 0;
    }
    if (_wcsicmp(command, L"--watchdog") == 0) {
        CLogger::CleanLogFile();
        return DesktopApp::RunWatchdog(executablePath);
    }
    if (_wcsicmp(command, L"--run") == 0)
        return DesktopApp::RunWorker();

    std::wcerr << L"Usage: ScreenStream.exe [--install|--uninstall|--start|--stop]" << std::endl;
    CLogger::LogMessage(L"Unknown command line option");
    return 2;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        CLogger::Initialise(0);
        CLogger::LogError(L"CommandLineToArgvW failed", GetLastError());
        return 1;
    }
    const int result = RunCommandLine(argc, argv);
    LocalFree(argv);
    return result;
}
