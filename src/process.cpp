//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>

#include <rbase/inc/path.h>

#if RTM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // RTM_PLATFORM_WINDOWS

namespace rdebug {

#if RTM_PLATFORM_WINDOWS

const DWORD g_bufferSize	= 4096*160;

bool processIs64bitBinary(const char* _path)
{
	rtm::MultiToWide path(_path);

	DWORD type;
	if (GetBinaryTypeW(path, &type))
	{
		if (SCS_64BIT_BINARY == type)
			return true;
	}
	return false;
}

//
//  SetPrivilege enables/disables process token privilege.
//
BOOL SetPrivilege(HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnablePrivilege)
{
	LUID luid;
	BOOL bRet = FALSE;

	if (LookupPrivilegeValue(NULL, lpszPrivilege, &luid))
	{
		TOKEN_PRIVILEGES tp;

		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = (bEnablePrivilege) ? SE_PRIVILEGE_ENABLED : 0;
		//
		//  Enable the privilege or disable all privileges.
		//
		if (AdjustTokenPrivileges(hToken, FALSE, &tp, NULL, (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL))
		{
			//
			//  Check to see if you have proper access.
			//  You may get "ERROR_NOT_ALL_ASSIGNED".
			//
			bRet = (GetLastError() == ERROR_SUCCESS);
		}
	}
	return bRet;
}

bool acquireDebugPrivileges(HANDLE _process)
{
	HANDLE hToken;
	if (OpenProcessToken(_process, TOKEN_ADJUST_PRIVILEGES, &hToken))
	{
		SetPrivilege(hToken, SE_DEBUG_NAME, TRUE);
		CloseHandle(hToken);
		return true;
	}
	return false;
}

bool processInjectDLL(const char* _executablePath, const char* _DLLPath, const char* _cmdLine, const char* _workingDir, uint32_t* _pid)
{
	STARTUPINFOW startInfo;
	PROCESS_INFORMATION pInfo;
    memset(&startInfo, 0, sizeof(STARTUPINFOW));
    memset(&pInfo, 0, sizeof(PROCESS_INFORMATION));
    startInfo.cb = sizeof(STARTUPINFOW);

	wchar_t cmdLine[32768];
	wcscpy(cmdLine, rtm::MultiToWide(_executablePath));
	wcscat(cmdLine, L" ");
	wcscat(cmdLine, rtm::MultiToWide(_cmdLine, false));

	if (CreateProcessW(0, cmdLine, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, rtm::MultiToWide(_workingDir), &startInfo, &pInfo) != TRUE)
		return false;

	if (!acquireDebugPrivileges(pInfo.hProcess))
	{
		TerminateProcess(pInfo.hProcess, 0);
		CloseHandle(pInfo.hThread);
		CloseHandle(pInfo.hProcess);
		return false;
	}

	HMODULE kernel32 = GetModuleHandle("kernel32.dll");
	LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(kernel32, "LoadLibraryW");

	char dllPath[2048];
	rtm::strlCpy(dllPath, RTM_NUM_ELEMENTS(dllPath), _DLLPath);
	rtm::pathCanonicalize(dllPath);

	rtm::MultiToWide dllPathWide(dllPath);
	size_t dllPathLen = wcslen(dllPathWide) + 1;
	LPVOID remoteMem = VirtualAllocEx(pInfo.hProcess, NULL, dllPathLen*2, MEM_COMMIT, PAGE_READWRITE);
	SIZE_T numBytesWritten;
	if (!WriteProcessMemory(pInfo.hProcess, remoteMem, dllPathWide, dllPathLen*2, &numBytesWritten))
	{
		VirtualFreeEx(pInfo.hProcess, remoteMem, 0, MEM_RELEASE);
		TerminateProcess(pInfo.hProcess, 0);
		CloseHandle(pInfo.hThread);
		CloseHandle(pInfo.hProcess);
		return false;
	}

	HANDLE remoteThread = CreateRemoteThread(pInfo.hProcess, NULL, 0, loadLib, remoteMem, 0, NULL);

	if (remoteThread == NULL)
	{
		VirtualFreeEx(pInfo.hProcess, remoteMem, 0, MEM_RELEASE);
		TerminateProcess(pInfo.hProcess, 0);
		CloseHandle(pInfo.hThread);
		CloseHandle(pInfo.hProcess);
		return false;
	}
	else
	{
		WaitForSingleObject(remoteThread, INFINITE);
		CloseHandle(remoteThread);
	}

	VirtualFreeEx(pInfo.hProcess, remoteMem, 0, MEM_RELEASE);
	ResumeThread(pInfo.hThread);
	if (_pid)
		*_pid = pInfo.dwProcessId;

	CloseHandle(pInfo.hThread);
	CloseHandle(pInfo.hProcess);
	return true;
}

bool processRun(const char* _cmdLine, bool _hideWindow, uint32_t* _exitCode)
{
	STARTUPINFOW startInfo;
	PROCESS_INFORMATION pInfo;
    memset(&startInfo, 0, sizeof(STARTUPINFOW));
    memset(&pInfo, 0, sizeof(PROCESS_INFORMATION));
    startInfo.cb = sizeof(STARTUPINFOW);

	if (_hideWindow)
	{
		startInfo.dwFlags		= STARTF_USESHOWWINDOW;
		startInfo.wShowWindow	= SW_HIDE;
	}

	rtm::MultiToWide cmdLine(_cmdLine, false);

	if (CreateProcessW(0, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &startInfo, &pInfo) != TRUE)
		return false;

	WaitForSingleObject(pInfo.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(pInfo.hProcess, &exitCode);

	if (_exitCode)
		*_exitCode = exitCode;

	CloseHandle(pInfo.hThread);
	CloseHandle(pInfo.hProcess);
	return true;
}

struct PipeHandles
{
	HANDLE m_stdIn_Read;
	HANDLE m_stdIn_Write;
	HANDLE m_stdOut_Read;
	HANDLE m_stdOut_Write;

	PipeHandles()
	{
		m_stdIn_Read	= NULL;
		m_stdIn_Write	= NULL;
		m_stdOut_Read	= NULL;
		m_stdOut_Write	= NULL;
	}

	~PipeHandles()
	{
		CloseHandle(m_stdIn_Read);
		CloseHandle(m_stdIn_Write);
		CloseHandle(m_stdOut_Read);
		CloseHandle(m_stdOut_Write);
	}
};

BOOL createChildProcess(const char* _cmdLine, PipeHandles* _handles, bool _redirectIO, std::string& _buffer)
{
	PROCESS_INFORMATION piProcInfo; 
	STARTUPINFOW siStartInfo;
	BOOL bSuccess = FALSE; 
 
	ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
	ZeroMemory( &siStartInfo, sizeof(STARTUPINFOW) );
	siStartInfo.cb = sizeof(STARTUPINFOW);

	if (_redirectIO)
	{
		siStartInfo.hStdError	= _handles->m_stdOut_Write;
		siStartInfo.hStdOutput	= _handles->m_stdOut_Write;
		siStartInfo.hStdInput	= _handles->m_stdIn_Read;
		siStartInfo.dwFlags		= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		siStartInfo.wShowWindow	= SW_HIDE;
	}
	else
		siStartInfo.dwFlags		= STARTF_USESTDHANDLES;

	rtm::MultiToWide cmdLine(_cmdLine, false);
	bSuccess = CreateProcessW(NULL, cmdLine.m_ptr, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);  

	if (bSuccess) 
	{
		HANDLE h[2];
		h[0] = piProcInfo.hThread;
		h[1] = piProcInfo.hProcess;

		std::string buffer, wbuffer;
		buffer.resize(g_bufferSize + 1); // +1 for null terminator
		wbuffer.resize(g_bufferSize + 1);
		
		for (;;)
		{
			bool stillRunning = (WaitForSingleObject(piProcInfo.hProcess,0) == WAIT_TIMEOUT);
			
			DWORD dwRead = 0;
			DWORD bytesAvailable = 0;
			PeekNamedPipe(_handles->m_stdOut_Read, NULL, 0, NULL, &bytesAvailable, NULL);
			bSuccess = bytesAvailable && (ReadFile(_handles->m_stdOut_Read, &buffer[0], g_bufferSize, &dwRead, NULL) == TRUE);

			if (bSuccess)
			{
				buffer[dwRead] = '\0';
				_buffer += buffer.c_str();
			}
			
			if (!stillRunning)
				break;
		}

		WaitForMultipleObjects(2,h,TRUE,999);
		CloseHandle(piProcInfo.hThread);
		CloseHandle(piProcInfo.hProcess);
	}
	return bSuccess;
}

void CreatePipes(PipeHandles* _handles)
{
	SECURITY_ATTRIBUTES saAttr; 
 
	saAttr.nLength				= sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle		= TRUE;
	saAttr.lpSecurityDescriptor	= NULL; 

	CreatePipe(&_handles->m_stdOut_Read, &_handles->m_stdOut_Write, &saAttr, g_bufferSize);
	SetHandleInformation(_handles->m_stdOut_Read, HANDLE_FLAG_INHERIT, 0);

	saAttr.nLength				= sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle		= TRUE;
	saAttr.lpSecurityDescriptor	= NULL; 

	CreatePipe(&_handles->m_stdIn_Read, &_handles->m_stdIn_Write, &saAttr, g_bufferSize);
	SetHandleInformation(_handles->m_stdIn_Write, HANDLE_FLAG_INHERIT, 0);
}

DWORD ReadFromPipe(std::string& _buffer, PipeHandles* _handles)
{ 
	DWORD dwRead = 0;
	BOOL bSuccess = FALSE;

	char utf8buffer[g_bufferSize * 2];
	for (;;) 
	{ 
		DWORD bytesAvailable = 0;
		PeekNamedPipe(_handles->m_stdOut_Read, NULL, 0, NULL, &bytesAvailable, NULL);
		bSuccess = bytesAvailable && (ReadFile(_handles->m_stdOut_Read, utf8buffer, 8192, &dwRead, NULL) == TRUE);
		if (!bSuccess)
			break;

		utf8buffer[dwRead] = '\0';
		_buffer += utf8buffer;

		if (dwRead < 8192)
			break;
	}

	return dwRead;
} 

bool processGetOutputOf(const char* _cmdLine, std::string& _buffer, bool _redirect)
{
	PipeHandles handles;
	CreatePipes(&handles);

	BOOL success = createChildProcess(_cmdLine, &handles, _redirect, _buffer);
	
	if (!success)
		return false;

	ReadFromPipe(_buffer, &handles);
	return true;
}

char* processGetOutputOf(const char* _cmdLine, bool _redirectIO)
{
	std::string buffer;
	processGetOutputOf(_cmdLine, buffer, _redirectIO);
	size_t len = rtm::strLen(buffer.c_str());
	if (len)
	{
		char* res = (char*)rtm_alloc(sizeof(char) * (len + 1));
		rtm::strlCpy(res, (uint32_t)(len + 1), buffer.c_str());
		return res;
	}
	return 0;
}

#else // RTM_PLATFORM_WINDOWS

bool processIs64bitBinary(const char* _path)
{
	RTM_UNUSED(_path);
	return false;
}

bool processInjectDLL(const char* _executablePath, const char* _DLLPath, const char* _cmdLine, const char* _workingDir)
{
	RTM_UNUSED(_executablePath);
	RTM_UNUSED(_DLLPath);
	RTM_UNUSED(_workingDir);
	RTM_UNUSED(_cmdLine);
	return false;
}

bool processRun(const char* _cmdLine, bool _hideWindow, uint32_t* _exitCode)
{
	RTM_UNUSED(_cmdLine);
	RTM_UNUSED(_hideWindow);
	RTM_UNUSED(_exitCode);
	return false;
}

char* processGetOutputOf(const char* _cmdLine, bool _redirectIO)
{
	RTM_UNUSED(_cmdLine);
	RTM_UNUSED(_redirectIO);
	return 0;
}

#endif // RTM_PLATFORM_WINDOWS

void processReleaseOutput(const char* _output)
{
	if (_output)
		rtm_free((void*)_output);
}

void addressToString(uint64_t _address, char* _buffer)
{
	union { uint64_t a; char h[8]; uint32_t d[2]; } c;
	c.a = _address;
	_buffer[0] = '0';
	_buffer[1] = 'x';

	int ptr = 2;
	for (int i=c.d[1]?0:4; i<8; ++i)
	{
		char cc = c.h[7 - i];
		_buffer[ptr++] = rtm::charToHexNum((char)(cc >> 4));
		_buffer[ptr++] = rtm::charToHexNum((char)(cc & 0xf));
	}
	_buffer[ptr] = '\0';
}

} // namespace rdebug
