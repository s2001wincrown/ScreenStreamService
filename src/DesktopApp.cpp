#include "DesktopApp.h"
#include "ScreenStreamService.h"
#include <Logger.h>
#include <windows.h>
#include <filesystem>
#include <vector>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"ScreenStream";
constexpr wchar_t kWatchdogMutex[] = L"Local\\ScreenStreamService_Watchdog";
constexpr wchar_t kStopEvent[] = L"Local\\ScreenStreamService_Stop";
constexpr DWORD kRestartDelayMs = 5000;

std::wstring Quote(const std::wstring& value) {
	return L"\"" + value + L"\"";
}

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

bool LaunchWorker(const std::wstring& executablePath, PROCESS_INFORMATION& process) {
	std::wstring commandLine = Quote(executablePath) + L" --run";
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	const std::wstring workingDirectory =
		std::filesystem::path(executablePath).parent_path().wstring();
	if (!CreateProcessW(executablePath.c_str(), mutableCommand.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &process)) {
		const DWORD error = GetLastError();
		CLogger::LogError(L"Unable to launch desktop worker", error);
		return false;
	}
	CLogger::Message(0, L"Started desktop worker process %lu", process.dwProcessId);
	return true;
}
}

bool DesktopApp::InstallStartup(const std::wstring& executablePath) {
	HKEY key = nullptr;
	LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
	if (result != ERROR_SUCCESS) {
		CLogger::LogError(L"Unable to open current-user startup registry key",
			static_cast<DWORD>(result));
		SetLastError(static_cast<DWORD>(result));
		return false;
	}

	const std::wstring command = Quote(executablePath) + L" --watchdog";
	result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
		reinterpret_cast<const BYTE*>(command.c_str()),
		static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
	RegCloseKey(key);
	if (result != ERROR_SUCCESS) {
		CLogger::LogError(L"Unable to register current-user startup command",
			static_cast<DWORD>(result));
		SetLastError(static_cast<DWORD>(result));
		return false;
	}
	CLogger::LogMessage(L"Registered ScreenStream to start at current-user sign-in");
	return true;
}

bool DesktopApp::RemoveStartup() {
	HKEY key = nullptr;
	LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
	if (result == ERROR_FILE_NOT_FOUND) return true;
	if (result != ERROR_SUCCESS) {
		CLogger::LogError(L"Unable to open current-user startup registry key",
			static_cast<DWORD>(result));
		SetLastError(static_cast<DWORD>(result));
		return false;
	}

	result = RegDeleteValueW(key, kRunValue);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
		CLogger::LogError(L"Unable to remove current-user startup command",
			static_cast<DWORD>(result));
		SetLastError(static_cast<DWORD>(result));
		return false;
	}
	CLogger::LogMessage(L"Removed ScreenStream current-user startup command");
	return true;
}

bool DesktopApp::StartWatchdog(const std::wstring& executablePath) {
	HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, kWatchdogMutex);
	if (existing) {
		CloseHandle(existing);
		CLogger::LogMessage(L"Watchdog is already running");
		return true;
	}
	if (GetLastError() != ERROR_FILE_NOT_FOUND) {
		const DWORD error = GetLastError();
		CLogger::LogError(L"Unable to check whether watchdog is running", error);
		SetLastError(error);
		return false;
	}

	std::wstring commandLine = Quote(executablePath) + L" --watchdog";
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const std::wstring workingDirectory =
		std::filesystem::path(executablePath).parent_path().wstring();
	if (!CreateProcessW(executablePath.c_str(), mutableCommand.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &process)) {
		const DWORD error = GetLastError();
		CLogger::LogError(L"Unable to launch watchdog", error);
		SetLastError(error);
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	CLogger::LogMessage(L"Launched desktop watchdog");
	return true;
}

bool DesktopApp::RequestStop(bool ignoreNotRunning) {
	HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEvent);
	if (!stopEvent) {
		const DWORD error = GetLastError();
		if (ignoreNotRunning && error == ERROR_FILE_NOT_FOUND) return true;
		CLogger::LogError(L"Unable to open watchdog stop event", error);
		SetLastError(error);
		return false;
	}
	const BOOL signaled = SetEvent(stopEvent);
	const DWORD error = signaled ? ERROR_SUCCESS : GetLastError();
	CloseHandle(stopEvent);
	if (!signaled) {
		CLogger::LogError(L"Unable to signal watchdog stop event", error);
		SetLastError(error);
		return false;
	}
	CLogger::LogMessage(L"Requested desktop application shutdown");
	return true;
}

int DesktopApp::RunWatchdog(const std::wstring& executablePath) {
	SetLastError(ERROR_SUCCESS);
	HANDLE mutex = CreateMutexW(nullptr, TRUE, kWatchdogMutex);
	if (!mutex) {
		CLogger::LogError(L"Unable to create watchdog mutex", GetLastError());
		return 1;
	}
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(mutex);
		CLogger::LogMessage(L"Another watchdog instance is already running");
		return 0;
	}

	HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, kStopEvent);
	if (!stopEvent) {
		CLogger::LogError(L"Unable to create watchdog stop event", GetLastError());
		CloseHandle(mutex);
		return 1;
	}
	CLogger::LogMessage(L"Watchdog started in the interactive user session");

	int result = 0;
	for (;;) {
		PROCESS_INFORMATION process{};
		if (!LaunchWorker(executablePath, process)) {
			if (WaitForSingleObject(stopEvent, kRestartDelayMs) == WAIT_OBJECT_0) break;
			continue;
		}

		HANDLE waits[] = { process.hProcess, stopEvent };
		const DWORD waitResult = WaitForMultipleObjects(_countof(waits), waits, FALSE, INFINITE);
		if (waitResult == WAIT_OBJECT_0 + 1) {
			CLogger::LogMessage(L"Watchdog stopping desktop worker");
			if (WaitForSingleObject(process.hProcess, 10000) == WAIT_TIMEOUT) {
				CLogger::LogMessage(L"Worker did not stop in time; terminating it");
				if (!TerminateProcess(process.hProcess, ERROR_PROCESS_ABORTED))
					CLogger::LogError(L"Unable to terminate desktop worker", GetLastError());
				WaitForSingleObject(process.hProcess, INFINITE);
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			break;
		}
		if (waitResult != WAIT_OBJECT_0) {
			const DWORD error = GetLastError();
			CLogger::LogError(L"Watchdog wait failed", error);
			result = 1;
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			break;
		}

		DWORD exitCode = ERROR_GEN_FAILURE;
		if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
			CLogger::LogError(L"Unable to read desktop worker exit code", GetLastError());
		} else {
			CLogger::Message(0, L"Desktop worker exited with code %lu (0x%08lX); restarting",
				exitCode, exitCode);
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		if (WaitForSingleObject(stopEvent, kRestartDelayMs) == WAIT_OBJECT_0) break;
	}

	CLogger::LogMessage(L"Watchdog stopped");
	CloseHandle(stopEvent);
	CloseHandle(mutex);
	return result;
}

int DesktopApp::RunWorker() {
	std::wstring executablePath;
	if (!GetExecutablePath(executablePath)) return 1;

	HANDLE stopEvent = OpenEventW(SYNCHRONIZE, FALSE, kStopEvent);
	if (!stopEvent) {
		CLogger::LogError(L"Worker must be started by the watchdog", GetLastError());
		return 1;
	}

	const std::filesystem::path configPath =
		std::filesystem::path(executablePath).parent_path() / L"Config.ini";
	ScreenStreamService service;
	DWORD errorCode = ERROR_GEN_FAILURE;
	if (!service.Initialize(configPath, &errorCode) || !service.Start(&errorCode)) {
		CLogger::LogError(L"Desktop application startup failed", errorCode);
		CloseHandle(stopEvent);
		return 1;
	}

	CLogger::LogMessage(L"Desktop application running in the interactive user session");
	const DWORD waitResult = WaitForSingleObject(stopEvent, INFINITE);
	if (waitResult != WAIT_OBJECT_0) {
		CLogger::LogError(L"Worker wait for shutdown failed", GetLastError());
		service.Stop();
		CloseHandle(stopEvent);
		return 1;
	}

	service.Stop();
	CloseHandle(stopEvent);
	CLogger::LogMessage(L"Desktop application stopped cleanly");
	return 0;
}
