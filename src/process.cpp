//--------------------------------------------------------------------------//
/// Copyright (c) 2017 by Milos Tosic. All Rights Reserved.                /// 
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

bool acquireDebugPrivileges(HANDLE _process)
{
	bool success = false;

	HANDLE hToken;
	TOKEN_PRIVILEGES tokenPriv;
	LUID luidDebug;
	if(OpenProcessToken(_process, TOKEN_ADJUST_PRIVILEGES, &hToken))
	{
		if(LookupPrivilegeValueA("", SE_DEBUG_NAME, &luidDebug))
		{
			tokenPriv.PrivilegeCount           = 1;
			tokenPriv.Privileges[0].Luid       = luidDebug;
			tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

			if(AdjustTokenPrivileges(hToken, FALSE, &tokenPriv, sizeof(tokenPriv), NULL, NULL) == 0)
				success = false;
			else
				success = true;
		}
		else
			success = false;
	}
	else
		success = false;

	return success;
}

bool processInjectDLL(const char* _executablePath, const char* _DLLPath, const char* _cmdLine, const char* _workingDir)
{
	STARTUPINFOW startInfo;
	PROCESS_INFORMATION pInfo;
    memset(&startInfo, 0, sizeof(STARTUPINFOW));
    memset(&pInfo, 0, sizeof(PROCESS_INFORMATION));
    startInfo.cb = sizeof(STARTUPINFOW);

	wchar_t cmdLine[32768];
	wcscpy(cmdLine, rtm::MultiToWide(_executablePath));
	wcscat(cmdLine, L" ");
	wcscat(cmdLine, rtm::MultiToWide(_cmdLine));

	if (CreateProcessW(0, cmdLine, NULL, NULL, FALSE,	
				CREATE_SUSPENDED, NULL, rtm::MultiToWide(_workingDir), &startInfo, &pInfo) != TRUE)
		return false;

	if (!acquireDebugPrivileges(pInfo.hProcess))
	{
		return false;
	}

	HMODULE kernel32 = GetModuleHandle("kernel32.dll");
	LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(kernel32, "LoadLibraryW");

	char dllPath[2048];
	strcpy(dllPath, _DLLPath);
	rtm::pathRemoveRelative(dllPath);

	rtm::MultiToWide dllPathWide(dllPath);
	size_t dllPathLen = wcslen(dllPathWide) + 1;
	LPVOID remoteMem = VirtualAllocEx(pInfo.hProcess, NULL, dllPathLen*2, MEM_COMMIT, PAGE_READWRITE);
	SIZE_T numBytesWritten;
	if (!WriteProcessMemory(pInfo.hProcess, remoteMem, dllPathWide, dllPathLen*2, &numBytesWritten))
	{
		TerminateProcess(pInfo.hProcess, 0);
		CloseHandle(pInfo.hProcess);
		return false;
	}

	HANDLE remoteThread = CreateRemoteThread(pInfo.hProcess, NULL, 0, loadLib, remoteMem, 0, NULL);

	if (remoteThread == NULL)
	{
		TerminateProcess(pInfo.hProcess, 0);
		CloseHandle(pInfo.hProcess);
		return false;
	}
	else
	{
		WaitForSingleObject(remoteThread, INFINITE);
	}

	ResumeThread(pInfo.hThread);
	return true;
}

bool processRun(const char* _cmdLine)
{
	STARTUPINFOW startInfo;
	PROCESS_INFORMATION pInfo;
    memset(&startInfo, 0, sizeof(STARTUPINFOW));
    memset(&pInfo, 0, sizeof(PROCESS_INFORMATION));
    startInfo.cb = sizeof(STARTUPINFOW);
	startInfo.dwFlags |= STARTF_USESHOWWINDOW;
	startInfo.wShowWindow = SW_HIDE;

	rtm::MultiToWide cmdLine(_cmdLine);

	if (CreateProcessW(0, rtm::MultiToWide(_cmdLine), NULL, NULL, FALSE, 0, NULL, NULL, &startInfo, &pInfo) != TRUE)
		return false;

	WaitForSingleObject(pInfo.hProcess, INFINITE);
	DWORD exitCode=0;
	bool ret = true;
	if (GetExitCodeProcess(pInfo.hProcess, &exitCode))
	{
		if (exitCode != 0)
			ret = false;
	}
	else
		ret = false;

	CloseHandle(pInfo.hProcess);
	return ret;
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

bool processRun(const char* _cmdLine)
{
	RTM_UNUSED(_cmdLine);
	return false;
}

#endif // RTM_PLATFORM_WINDOWS

} // namespace rdebug
