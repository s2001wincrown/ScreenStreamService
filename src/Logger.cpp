/*
 *	$Header: /logger/loggertest/Logger.cpp 1     11/11/03 2:44a Administrator $
 *
 *	$History: Logger.cpp $
 * 
 * *****************  Version 1  *****************
 * User: Administrator Date: 11/11/03   Time: 2:44a
 * Created in $/logger/loggertest
 */
#include "Logger.h"
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace {
std::wstring GetSystemMessage(DWORD errorCode) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || !buffer) return L"Unknown error";

    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        message.pop_back();
    return message;
}

void SetOwnedString(wchar_t*& destination, const std::wstring& value) {
    delete[] destination;
    destination = new wchar_t[value.size() + 1];
    wcscpy_s(destination, value.size() + 1, value.c_str());
}

class ProcessLogLock {
public:
    ProcessLogLock() : mutex_(CreateMutexW(nullptr, FALSE,
        L"Local\\ScreenStreamService_LogWriter")) {
        if (!mutex_) {
            OutputDebugStringW(L"Unable to create ScreenStream log mutex.\n");
            return;
        }
        const DWORD waitResult = WaitForSingleObject(mutex_, INFINITE);
        ownsMutex_ = waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
        if (!ownsMutex_)
            OutputDebugStringW(L"Unable to acquire ScreenStream log mutex.\n");
    }

    ~ProcessLogLock() {
        if (ownsMutex_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
    }

    bool IsLocked() const { return ownsMutex_; }

private:
    HANDLE mutex_ = nullptr;
    bool ownsMutex_ = false;
};
}

DWORD CLogger::m_dwLogLevel = 0;
wchar_t* CLogger::m_pModuleName = nullptr;
wchar_t* CLogger::m_pLogPath = nullptr;
UINT CLogger::m_uiLogFileKeepDay = 7;
CLoggerCritic CLogger::m_cs;
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

//	dwLogLevel specifies a threshhold for message severity.  Messages
//	at or below dwLogLevel are written to the log pipe, other messages
//	are thrown away.
void CLogger::Initialise(DWORD dwLogLevel, const wchar_t* szLogPath, const wchar_t* szModuleName)
{
	wchar_t modulePath[MAX_PATH]{};
	const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
	std::filesystem::path executablePath;
	if (pathLength > 0 && pathLength < MAX_PATH)
		executablePath = modulePath;

	std::filesystem::path logPath;
	if (szLogPath) {
		logPath = szLogPath;
	} else {
		wchar_t localAppData[MAX_PATH]{};
		const DWORD localPathLength = GetEnvironmentVariableW(
			L"LOCALAPPDATA", localAppData, _countof(localAppData));
		if (localPathLength > 0 && localPathLength < _countof(localAppData))
			logPath = std::filesystem::path(localAppData) / L"ScreenStream";
		else
			logPath = executablePath.empty()
				? std::filesystem::path(L".")
				: executablePath.parent_path();
	}

	std::error_code error;
	std::filesystem::create_directories(logPath, error);
	if (error && !executablePath.empty() && logPath != executablePath.parent_path()) {
		OutputDebugStringW(L"Unable to create ScreenStream log directory; falling back beside executable.\n");
		logPath = executablePath.parent_path();
		error.clear();
		std::filesystem::create_directories(logPath, error);
	}
	if (error) {
		OutputDebugStringW(L"Unable to create ScreenStream log directory.\n");
	}

	const std::wstring moduleName = szModuleName
		? szModuleName
		: (executablePath.empty() ? L"ScreenStream" : executablePath.stem().wstring());
	SetOwnedString(m_pModuleName, moduleName);
	SetOwnedString(m_pLogPath, logPath.wstring());
	m_dwLogLevel = dwLogLevel;
}

void CLogger::Shutdown()
{
	delete[] m_pModuleName;
	m_pModuleName = nullptr;
	delete[] m_pLogPath;
	m_pLogPath = nullptr;
}

void CLogger::Message(WORD nType, const wchar_t* pszFormat, ...)
{
	if (!pszFormat) return;

	wchar_t message[LOG_BUFFERSIZE]{};
	va_list args;
	va_start(args, pszFormat);
	const int result = vswprintf_s(message, _countof(message), pszFormat, args);
	va_end(args);
	if (result >= 0) PrintMessage(nType, message);
}

void CLogger::PrintMessage(WORD /*nType*/, const wchar_t* strLog)
{
	//if ( Settings.General.DebugLog )
	LogMessage( strLog );
	//TRACE1("%s\n", strLog.Left(256));
}

void CLogger::LogError(const wchar_t* file, const int lineNumber,
					   const wchar_t* str, DWORD dwError)
{
	std::wstring message = str ? str : L"";
	if (dwError != ERROR_SUCCESS) {
		if (!message.empty()) message += L"; ";
		message += L"File: ";
		message += file ? file : L"unknown";
		message += L"; Line: ";
		message += std::to_wstring(lineNumber);
		message += L"; Error: ";
		message += std::to_wstring(dwError);
		message += L"; ";
		message += GetSystemMessage(dwError);
	}
	if (!message.empty()) LogMessage(message.c_str());
}

void CLogger::LogError(const wchar_t* str, DWORD dwError)
{
	std::wstring message = str ? str : L"";
	if (dwError != ERROR_SUCCESS) {
		if (!message.empty()) message += L"; ";
		message += L"Error: ";
		message += std::to_wstring(dwError);
		message += L"; ";
		message += GetSystemMessage(dwError);
	}
	if (!message.empty()) LogMessage(message.c_str());
}

void CLogger::LogMessage(const wchar_t* strLog)
{
	if (!strLog || !m_pLogPath || !m_pModuleName) return;

	CLogSimpleLock lock(&m_cs);
	ProcessLogLock processLock;
	if (!processLock.IsLocked()) return;
	const std::filesystem::path logFile =
		std::filesystem::path(m_pLogPath) / (std::wstring(m_pModuleName) + L".log");
	HANDLE file = CreateFileW(logFile.c_str(), GENERIC_WRITE | GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		OutputDebugStringW(L"Unable to open ScreenStream log file.\n");
		return;
	}

	LARGE_INTEGER fileSize{};
	if (GetFileSizeEx(file, &fileSize) && fileSize.QuadPart > 1024 * 1024) {
		CloseHandle(file);
		SYSTEMTIME time{};
		GetLocalTime(&time);
		wchar_t rotatedName[128]{};
		swprintf_s(rotatedName, _countof(rotatedName), L"%s%04u%02u%02u%02u%02u%02u.log",
			m_pModuleName, time.wYear, time.wMonth, time.wDay,
			time.wHour, time.wMinute, time.wSecond);
		const std::filesystem::path rotatedFile =
			std::filesystem::path(m_pLogPath) / rotatedName;
		MoveFileExW(logFile.c_str(), rotatedFile.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		file = CreateFileW(logFile.c_str(), GENERIC_WRITE | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			OutputDebugStringW(L"Unable to recreate rotated ScreenStream log file.\n");
			return;
		}
		fileSize.QuadPart = 0;
	}

	DWORD written = 0;
	if (fileSize.QuadPart == 0) {
		const WORD byteOrderMark = 0xFEFF;
		if (!WriteFile(file, &byteOrderMark, sizeof(byteOrderMark), &written, nullptr)) {
			OutputDebugStringW(L"Unable to write ScreenStream log byte-order mark.\n");
			CloseHandle(file);
			return;
		}
	}

	SYSTEMTIME time{};
	GetLocalTime(&time);
	wchar_t timestamp[64]{};
	swprintf_s(timestamp, _countof(timestamp), L"[%02u/%02u/%04u %02u:%02u:%02u.%03u] ",
		time.wDay, time.wMonth, time.wYear, time.wHour, time.wMinute,
		time.wSecond, time.wMilliseconds);
	LARGE_INTEGER endOfFile{};
	BOOL writeSucceeded = SetFilePointerEx(file, endOfFile, nullptr, FILE_END);
	if (writeSucceeded)
		writeSucceeded = WriteFile(file, timestamp,
			static_cast<DWORD>(wcslen(timestamp) * sizeof(wchar_t)), &written, nullptr);
	if (writeSucceeded)
		writeSucceeded = WriteFile(file, strLog,
			static_cast<DWORD>(wcslen(strLog) * sizeof(wchar_t)), &written, nullptr);
	static constexpr wchar_t newline[] = L"\r\n";
	if (writeSucceeded)
		writeSucceeded = WriteFile(file, newline, sizeof(newline) - sizeof(wchar_t),
			&written, nullptr);
	if (!writeSucceeded)
		OutputDebugStringW(L"Unable to append ScreenStream log entry.\n");
	CloseHandle(file);
}

void CLogger::CleanLogFile()
{
	if (!m_pLogPath || !m_pModuleName) return;

	const std::filesystem::path directory(m_pLogPath);
	const std::wstring prefix = m_pModuleName;
	const auto cutoff = std::filesystem::file_time_type::clock::now() -
		std::chrono::hours(24LL * m_uiLogFileKeepDay);
	std::error_code error;
	for (std::filesystem::directory_iterator it(directory, error), end;
		 !error && it != end; it.increment(error)) {
		if (!it->is_regular_file(error) || error) continue;
		const std::wstring filename = it->path().filename().wstring();
		if (filename.size() <= prefix.size() + 4 ||
			filename.compare(0, prefix.size(), prefix) != 0 ||
			it->path().extension() != L".log")
			continue;

		const auto modified = it->last_write_time(error);
		if (!error && modified < cutoff)
			std::filesystem::remove(it->path(), error);
		error.clear();
	}
}
