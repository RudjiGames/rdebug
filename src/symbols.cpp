//--------------------------------------------------------------------------//
/// Copyright (c) 2010-2017 Milos Tosic. All Rights Reserved.              ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>

#include <string.h>
#include <stdio.h>	// sprintf

#if RTM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <DIA/include/dia2.h>

#if RTM_COMPILER_MSVC
#pragma warning (disable: 4091) // 'typedef ': ignored on left of '' when no variable is declared
#include <DbgHelp.h>
#pragma warning (default: 4091)

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif // RTM_COMPILER_MSVC

#endif // RTM_PLATFORM_WINDOWS

class PDBFile;

namespace rdebug {
	bool findSymbol(const char* _path, char _outSymbolPath[1024], const char* _symbolStore);
}

const char* getFileName(const char* _path)
{
	size_t len = strlen(_path);
	while ((_path[len] != '/') && (_path[len] != '\\') && (len>0)) --len;
	return &_path[len + 1];
}

namespace rdebug {

void parseAddr2LineSymbolInfo(char* _str, StackFrame& _frame);
void parsePlayStationSymbolInfo(char* _str, StackFrame& _frame);

void parseSymbolMapGNU(std::string& _buffer, SymbolMap& _symMap);
void parseSymbolMapPS3(std::string& _buffer, SymbolMap& _symMap);

uintptr_t symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, Toolchain& _tc, const char* _executablePath)
{
	ResolveInfo* info = new ResolveInfo();
	
	info->m_executablePath	= info->scratch(_executablePath);
	info->m_executableName	= getFileName(info->m_executablePath);
	info->m_tc_type			= _tc.m_type;

	// nm				-C --print-size --numeric-sort --line-numbers [SYMBOL]
	// addr2line		-f -C -e [SYMBOL] 0xAddress
	// c++filt			-t -n [MANGLED NAME]

	// PS3

	// nm:				ps3bin.exe -dsy [SYMBOL]
	// addr2Line:		ps3bin.exe + " -a2l 0x%x -i " + [SYMBOL]
	// c++filt:			ps3name.exe [MANGLED NAME]

	std::string append_nm;
	std::string append_a2l;
	std::string append_cppf;

	if (_tc.m_type == rdebug::Toolchain::TC_GCC)
	{
		append_nm	= "\" -C --print-size --numeric-sort --line-numbers \"";
		append_nm	+= _executablePath;
		append_nm	+= "\"";

//		append_a2l	= " -f -C -e \"";
		append_a2l	= "\" -f -e \"";
		append_a2l	+= _executablePath;
		append_a2l	+= "\" 0x%x";

		append_cppf	= "\" -t -n ";
	}

	if (_tc.m_type == rdebug::Toolchain::TC_PS3SNC)
	{
		append_nm	= "\" -dsy \"";
		append_nm	+= _executablePath;
		append_nm	+= "\"";

		append_a2l	= "\" -a2l 0x%x -i \"";
		append_a2l	+= _executablePath;
		append_a2l	+= "\"";

		append_cppf	= "\" -t -n ";

	}

#if RTM_PLATFORM_WINDOWS
	append_nm	= ".exe" + append_nm;
	append_a2l	= ".exe" + append_a2l;
	append_cppf	= ".exe" + append_cppf;
#endif

	std::string quote("\"");

	switch (_tc.m_type)
	{
		case rdebug::Toolchain::TC_MSVC:
			info->m_parseSym		= 0;
			info->m_parseSymMap		= 0;
			info->m_symbolStore		= info->scratch( _tc.m_toolchainPath );
			info->m_tc_addr2line	= 0;
			info->m_tc_nm			= 0;
			info->m_tc_cppfilt		= 0;
			break;

		case rdebug::Toolchain::TC_GCC:
			info->m_parseSym		= parseAddr2LineSymbolInfo;
			info->m_parseSymMap		= parseSymbolMapGNU;
			info->m_symbolStore		= 0;
			info->m_tc_addr2line	= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "addr2line" + append_a2l).c_str() );
			info->m_tc_nm			= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "nm" + append_nm).c_str() );
			info->m_tc_cppfilt		= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "c++filt" + append_cppf).c_str() );
			break;

		case rdebug::Toolchain::TC_PS3SNC:
			info->m_parseSym		= parsePlayStationSymbolInfo;
			info->m_parseSymMap		= parseSymbolMapPS3;
			info->m_symbolStore		= 0;
			info->m_tc_addr2line	= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "ps3bin" + append_a2l).c_str() );
			info->m_tc_nm			= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "ps3bin" + append_nm).c_str() );
			info->m_tc_cppfilt		= info->scratch( (quote + _tc.m_toolchainPath + _tc.m_toolchainPrefix + "ps3name" + append_cppf).c_str() );
			break;
	};

	for (uint32_t i=0; i<_numInfos; ++i)
	{
		Module module;
		module.m_module		= _moduleInfos[i];
		module.m_moduleName	= getFileName(module.m_module.m_modulePath);

		if (!((strcmp(module.m_module.m_modulePath, _executablePath) == 0) && (_tc.m_type != rdebug::Toolchain::TC_MSVC)))
		{
			char tmpName[1024];
			strcpy(tmpName, module.m_moduleName); 
			rtm::strToUpper(tmpName);

			if ((strcmp(tmpName,"MTUNERDLL32.DLL") == 0) || (strcmp(tmpName,"MTUNERDLL64.DLL") == 0))
				module.m_isRTMdll = true;

			info->m_modules.push_back(module);
		}
	}

	return (uintptr_t)info;
}

uintptr_t symbolResolverCreateForCurrentProcess()
{
#if RTM_PLATFORM_WINDOWS
	ResolveInfo* info = new ResolveInfo();

	//uint32_t buffPtr = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
	if (snapshot != INVALID_HANDLE_VALUE)
	{
		MODULEENTRY32W me;
		BOOL cap = Module32FirstW(snapshot, &me);
		if (!cap)
		{
			// fall back on enumerating modules
			HMODULE hMods[1024];
		    DWORD cbNeeded;

		    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded))
			{
				for (uint32_t i=0; i<(cbNeeded/sizeof(HMODULE)); ++i)
				{
					wchar_t szModName[MAX_PATH];

					MODULEINFO mi;
					GetModuleInformation(GetCurrentProcess(), hMods[i], &mi, sizeof(mi) );
					
			        if (GetModuleFileNameExW(GetCurrentProcess(), hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t)))
		            {
						rtm::WideToMulti modulePath(szModName);

					    uint64_t modBase = (uint64_t)mi.lpBaseOfDll;
						uint64_t modSize = (uint64_t)mi.SizeOfImage;
						Module module;
						module.m_module.m_baseAddress	= modBase;
						module.m_module.m_baseAddress	= modBase;
						module.m_module.m_size			= modSize;
						module.m_module.m_modulePath	= info->scratch(modulePath.m_ptr);	
						module.m_moduleName				= getFileName(module.m_module.m_modulePath);
						info->m_modules.push_back(module);
			        }
			    }
			}
		}
		else
		while (cap)
		{
			rtm::WideToMulti exePath(me.szExePath);

			uint64_t modBase = (uint64_t)me.modBaseAddr;
			uint64_t modSize = (uint64_t)me.modBaseSize;
			Module module;
			module.m_module.m_baseAddress	= modBase;
			module.m_module.m_size			= modSize;
			module.m_module.m_modulePath	= info->scratch(exePath.m_ptr);
			module.m_moduleName				= getFileName(module.m_module.m_modulePath);
			cap = Module32NextW(snapshot, &me);
		}

		CloseHandle(snapshot);
	}

	return (uintptr_t)info;
#else
	return 0;
#endif
}

void symbolResolverDelete(uintptr_t _resolver)
{
	ResolveInfo* info = (ResolveInfo*)_resolver;
	if (info)
		delete info;
}

ResolveInfo::ResolveInfo()
{
	m_scratch			= new char[SCRATCH_MEM_SIZE];
	m_scratchPos		= 0;
	m_tc_addr2line		= "";
	m_tc_nm				= "";
	m_tc_cppfilt		= "";
	m_executablePath	= "";

	m_parseSym				= 0;
	m_baseAddress4addr2Line = 0;
	m_symbolMapInitialized	= false;
}

ResolveInfo::~ResolveInfo()
{
#if RTM_PLATFORM_WINDOWS
	for (uint32_t i = 0; i < m_modules.size(); ++i)
		if (m_modules[i].m_PDBFile)
			delete m_modules[i].m_PDBFile;
#endif // RTM_PLATFORM_WINDOWS
	delete m_scratch;
}

char* ResolveInfo::scratch(const char* _str)
{
	RTM_ASSERT(_str != 0, "null string!");
	size_t len = strlen(_str) + 1;
	RTM_ASSERT(m_scratchPos + len < SCRATCH_MEM_SIZE, "Scratch buffer full!");
	char* ret = &m_scratch[m_scratchPos];
	strcpy(ret, _str);
	m_scratchPos += (uint32_t)len;
	return ret;
}

#if RTM_PLATFORM_WINDOWS

class DiaLoadCallBack : public IDiaLoadCallback2
{
	private:
		uint32_t	m_RefCount;
		wchar_t*	m_Buffer;

	public:
		DiaLoadCallBack(wchar_t inBuffer[1024]) : m_RefCount(0), m_Buffer(inBuffer) {}

    //	IUnknown
	ULONG STDMETHODCALLTYPE AddRef() { m_RefCount++; return m_RefCount; }
	ULONG STDMETHODCALLTYPE Release() { if (--m_RefCount == 0) delete this; return m_RefCount; }
    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID rid, void **ppUnk )
	{
		if (ppUnk == NULL) 
			return E_INVALIDARG;

		if (rid == IID_IDiaLoadCallback2)
			*ppUnk = (IDiaLoadCallback2 *)this;
		else if (rid == IID_IDiaLoadCallback)
			*ppUnk = (IDiaLoadCallback *)this;
		else if (rid == IID_IUnknown)
			*ppUnk = (IUnknown *)this;
		else
			*ppUnk = NULL;
		if ( *ppUnk != NULL ) 
		{
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	//	Rest
	HRESULT STDMETHODCALLTYPE NotifyDebugDir(BOOL, DWORD, BYTE[]) { return S_OK; }
    HRESULT STDMETHODCALLTYPE NotifyOpenDBG(LPCOLESTR, HRESULT) { return S_OK; }

    HRESULT STDMETHODCALLTYPE NotifyOpenPDB(LPCOLESTR pdbPath, HRESULT resultCode)
	{
		if (resultCode == S_OK)
			wcscpy(m_Buffer, pdbPath);
		return S_OK; 
	}

    HRESULT STDMETHODCALLTYPE RestrictRegistryAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictSymbolServerAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictOriginalPathAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictReferencePathAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictDBGAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictSystemRootAccess() { return S_OK; }
};

HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

BOOL createChildProcess(char* _cmdLine)
{
	PROCESS_INFORMATION piProcInfo; 
	STARTUPINFOW siStartInfo;
	BOOL bSuccess = FALSE; 
 
	ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

	ZeroMemory( &siStartInfo, sizeof(STARTUPINFOW) );
	siStartInfo.cb = sizeof(STARTUPINFOW); 
	siStartInfo.hStdError = g_hChildStd_OUT_Wr;
	siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
	siStartInfo.hStdInput = g_hChildStd_IN_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	siStartInfo.wShowWindow = SW_HIDE;
 
	rtm::MultiToWide cmdLine(_cmdLine);
	bSuccess = CreateProcessW(NULL, cmdLine.m_ptr, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);  

	if (bSuccess) 
	{
		HANDLE h[2];
		h[0] = piProcInfo.hThread;
		h[1] = piProcInfo.hProcess;
		WaitForMultipleObjects(2,h,TRUE,999);
		CloseHandle(piProcInfo.hThread);
		CloseHandle(piProcInfo.hProcess);
	}
	return bSuccess;
}

void CreatePipes()
{
	SECURITY_ATTRIBUTES saAttr; 
 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL; 

	CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 4096*160);
	SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL; 

	CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 4096*160);
	SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

DWORD ReadFromPipe(std::string& _buffer)
{ 
	DWORD dwRead;
	BOOL bSuccess = FALSE;
	HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	char	utf8buffer[8192+1];
	for (;;) 
	{ 
		bSuccess = ReadFile(g_hChildStd_OUT_Rd, utf8buffer, 8192, &dwRead, NULL);
		if (!bSuccess)
			break;

		utf8buffer[dwRead] = '\0';

		if (dwRead <= 8192*2)
			_buffer += utf8buffer;
		else
			_buffer += '\0';

		if (dwRead < 8192)
			break;
	}
	CloseHandle(hParentStdOut);
	return dwRead;
} 

bool getOutputOf(char* _cmdline, std::string& _buffer)
{
	CreatePipes();

	BOOL success = createChildProcess(_cmdline);
	
	if (!success)
		return false;

	ReadFromPipe(_buffer);

	CloseHandle(g_hChildStd_IN_Rd);
	CloseHandle(g_hChildStd_IN_Wr);
	CloseHandle(g_hChildStd_OUT_Rd);
	CloseHandle(g_hChildStd_OUT_Wr);

	g_hChildStd_IN_Rd = NULL;
	g_hChildStd_IN_Wr = NULL;
	g_hChildStd_OUT_Rd = NULL;
	g_hChildStd_OUT_Wr = NULL;
	return true;
}
#else // RTM_PLATFORM_WINDOWS

bool getOutputOf(char* _cmdline, std::string& _buffer)
{
	RTM_UNUSED(_cmdline);
	RTM_UNUSED(_buffer);
	return false;
}

#endif // RTM_PLATFORM_WINDOWS


#if RTM_PLATFORM_WINDOWS
void loadPDB(Module& _module, const char* _symbolStore)
{
	if (!_module.m_PDBFile)
	{
		_module.m_PDBFile = new PDBFile();
		char symbolPath[1024];
		strcpy(symbolPath, "");
		findSymbol(_module.m_module.m_modulePath, symbolPath, _symbolStore);
		rtm::MultiToWide symbolPathWide(symbolPath);
		_module.m_PDBFile->load(symbolPathWide.m_ptr);
	}
}
#endif // RTM_PLATFORM_WINDOWS

void symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame& _frame)
{
	_frame.m_moduleName[0] = '\0';
	_frame.m_file[0] = '\0';
	_frame.m_func[0] = '\0';
	_frame.m_line = 0;

	ResolveInfo* info = (ResolveInfo*)_resolver;
	if (!info)
		return;

#if RTM_PLATFORM_WINDOWS
	for (uint32_t i = 0; i<info->m_modules.size(); ++i)
	{
		if (info->m_modules[i].m_module.checkAddress(_address))
		{
			Module& module = info->m_modules[i];
			loadPDB(module, info->m_symbolStore);
			module.m_PDBFile->getSymbolByAddress(_address - module.m_module.m_baseAddress, _frame);
			strcpy(_frame.m_moduleName, getFileName(module.m_module.m_modulePath));
			return;
		}
	}
#endif // RTM_PLATFORM_WINDOWS

	if (info->m_tc_addr2line && (strlen(info->m_tc_addr2line) != 0))
	{
		strcpy(_frame.m_moduleName, info->m_executableName);

		char cmdline[4096 * 2];
#if RTM_PLATFORM_WINDOWS
		sprintf_s(cmdline, 4096 * 2, info->m_tc_addr2line, _address - info->m_baseAddress4addr2Line);
#else
		sprintf(cmdline, /*4096*2,*/ info->m_tc_addr2line, _address - info->m_baseAddress4addr2Line);
#endif
		std::string buffer;
		getOutputOf(cmdline, buffer);
		info->m_parseSym(&buffer[0], _frame);

		if (strcmp(_frame.m_func, "Unknown") != 0)
			if (strlen(info->m_tc_cppfilt) != 0)
			{
#if RTM_PLATFORM_WINDOWS
				sprintf_s(cmdline, 4096 * 2, "%s%s", info->m_tc_cppfilt, _frame.m_func);
#else
				sprintf(cmdline, /*4096 * 2,*/ "%s%s", info->m_tc_cppfilt, _frame.m_func);
#endif
				getOutputOf(cmdline, buffer);
				size_t len = strlen(buffer.c_str());
				size_t s = 0;
				while (s < len)
				{
					if ((buffer[s] == '\r') ||
						(buffer[s] == '\n'))
					{
						buffer[s] = 0;
						break;
					}
					++s;
				}
				strcpy(_frame.m_func, buffer.c_str());
			}
	}
}

uint64_t symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address, bool& _isRTMdll)
{
	_isRTMdll = false;
	ResolveInfo* info = (ResolveInfo*)_resolver;
	if (!info)
		return 0;

#if RTM_PLATFORM_WINDOWS
	for (uint32_t i=0; i<info->m_modules.size(); ++i)
	{
		if (info->m_modules[i].m_module.checkAddress(_address))
		{
			Module& module = info->m_modules[i];
			loadPDB(module, info->m_symbolStore);
			uintptr_t id = module.m_PDBFile->getSymbolID(_address - module.m_module.m_baseAddress);
			_isRTMdll = module.m_isRTMdll;
			return (id == 0) ? _address : id + module.m_module.m_baseAddress;
		}
	}
#endif // RTM_PLATFORM_WINDOWS

	if (info->m_tc_nm && (strlen(info->m_tc_nm) != 0) && (!info->m_symbolMapInitialized))
	{
		char cmdline[4096*2];
		strcpy(cmdline, info->m_tc_nm);

		std::string buffer;
		getOutputOf(cmdline, buffer);

		info->m_parseSymMap(buffer, info->m_symbolMap);
		info->m_symbolMapInitialized = true;
	}

	rdebug::Symbol* sym = info->m_symbolMap.findSymbol(_address);
	if (sym)
		return (uintptr_t)sym->m_offset + info->m_baseAddress4addr2Line;
	else
		return _address;
}

} // namespace rdebug
