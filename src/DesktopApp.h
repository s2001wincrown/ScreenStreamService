#pragma once

#include <string>

class DesktopApp {
public:
	static bool InstallStartup(const std::wstring& executablePath);
	static bool RemoveStartup();
	static bool StartWatchdog(const std::wstring& executablePath);
	static bool RequestStop(bool ignoreNotRunning = false);
	static int RunWatchdog(const std::wstring& executablePath);
	static int RunWorker();
};
