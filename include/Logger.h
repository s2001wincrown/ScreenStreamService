/*
 *	$Header: /logger/loggertest/Logger.h 1     11/11/03 2:44a Administrator $
 *
 *	$History: Logger.h $
 * 
 * *****************  Version 1  *****************
 * User: Administrator Date: 11/11/03   Time: 2:44a
 * Created in $/logger/loggertest
 */
#pragma once
#include <windows.h>

#define LOG_BUFFERSIZE	8192
#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define __WFILE__ WIDEN(__FILE__)


class CLoggerCritic : public CRITICAL_SECTION
{
public:
	CLoggerCritic( void )
	{
	  ::InitializeCriticalSection( this );
	}

	~CLoggerCritic( void )
	{
	  ::DeleteCriticalSection( this );
	}
};

class CLogSimpleLock
{
public:
	CLogSimpleLock( CLoggerCritic *pSection ) : m_pSection( pSection )
	{
		if( m_pSection != NULL )
			EnterCriticalSection( m_pSection );
	}

	~CLogSimpleLock(  )
	{
		if( m_pSection != NULL )
			LeaveCriticalSection( m_pSection );

		m_pSection = NULL;
	}

private:
	CLoggerCritic *m_pSection;
};

class CLogger
{
public:
	static void		Initialise(DWORD dwLogLevel = 0, const wchar_t* szLogPath = NULL, const wchar_t* szModuleName = NULL);
	static void		Shutdown();

	static void		Message(WORD nType, const wchar_t* pszFormat, ...);
	static void		PrintMessage(WORD nType, const wchar_t* strLog);
	static void		LogMessage(const wchar_t* strLog);
	static void		LogError(const wchar_t* file, const int lineNumber, const wchar_t* str = NULL, DWORD dwError = ::GetLastError()); // = __WFILE__  = __LINE__
	static void		LogError(const wchar_t* str = NULL, DWORD dwError = ::GetLastError());

	static void		CleanLogFile();

private:
	static DWORD		m_dwLogLevel;
	static wchar_t*     m_pModuleName;
	static wchar_t*     m_pLogPath;

	static CLoggerCritic m_cs;
	static UINT          m_uiLogFileKeepDay;
};
