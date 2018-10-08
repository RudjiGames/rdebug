//--------------------------------------------------------------------------//
/// Copyright (c) 2018 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>

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
//#pragma comment(lib, "psapi.lib")
#endif // RTM_COMPILER_MSVC

typedef BOOL  (WINAPI * fnGetModuleInformation)(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb);
typedef BOOL  (WINAPI * fnEnumProcessModules)(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded);
typedef DWORD (WINAPI * fnGetModuleFileNameExW)(HANDLE  hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

static fnGetModuleInformation	gFn_getModuleInformation	= 0;
static fnEnumProcessModules		gFn_enumProcessModules		= 0;
static fnGetModuleFileNameExW	gFn_getModuleFileNameExW	= 0;

FARPROC loadFunc(HMODULE _kernel, HMODULE _psapi, const char* _name)
{
	FARPROC ret = ::GetProcAddress(_kernel, _name);
	if (!ret && (_psapi != 0))
		ret = ::GetProcAddress(_psapi, _name);
	return ret;
}

#endif // RTM_PLATFORM_WINDOWS

class PDBFile;

namespace rdebug {
	bool findSymbol(const char* _path, char _outSymbolPath[1024], const char* _symbolStore);
}

namespace rdebug {

void parseAddr2LineSymbolInfo(char* _str, StackFrame& _frame);
void parsePlayStationSymbolInfo(char* _str, StackFrame& _frame);

void parseSymbolMapGNU(char*  _buffer, SymbolMap& _symMap);
void parseSymbolMapPS3(char*  _buffer, SymbolMap& _symMap);

uintptr_t symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, Toolchain* _tc, const char* _executable)
{
	RTM_ASSERT(_moduleInfos || _tc, "Either module info array or toolchain desc can't be NULL");

	ResolveInfo* info = rtm_new<ResolveInfo>();
	const char* executablePath = 0;

	for (uint32_t i=0; i<_numInfos; ++i)
	{
		Module module;
		module.m_module		= _moduleInfos[i];
		module.m_moduleName	= rtm::pathGetFileName(module.m_module.m_modulePath);

		char tmpName[1024];
		strcpy(tmpName, module.m_moduleName); 
		rtm::strToUpper(tmpName);

		if ((strcmp(tmpName,"MTUNERDLL32.DLL") == 0) || (strcmp(tmpName,"MTUNERDLL64.DLL") == 0))
			module.m_isRTMdll = true;

		const char* ext = rtm::pathGetExt(tmpName);
		if (ext && 
			((strcmp(ext, "EXE") == 0) || (strcmp(ext, "ELF") == 0)))
			executablePath = _moduleInfos[i].m_modulePath;

		info->m_modules.push_back(module);
	}

	if (!executablePath)
		executablePath = _executable;

	info->m_executablePath	= info->scratch(executablePath);
	info->m_executableName	= rtm::pathGetFileName(info->m_executablePath);
	info->m_tc_type			= _tc->m_type;

	// nm				-C --print-size --numeric-sort --line-numbers [SYMBOL]
	// addr2line		-f -C -e [SYMBOL] 0xAddress
	// c++filt			-t -n [MANGLED NAME]

	// PS3

	// nm:				ps3bin.exe -dsy [SYMBOL]
	// addr2Line:		ps3bin.exe + " -a2l 0x%x -i " + [SYMBOL]
	// c++filt:			ps3name.exe [MANGLED NAME]

	rtm_string append_nm;
	rtm_string append_a2l;
	rtm_string append_cppf;

	rtm_string quote;

	if ((_tc->m_type == rdebug::Toolchain::GCC) ||
		(_tc->m_type == rdebug::Toolchain::PS4))
	{
		if (_tc->m_type == rdebug::Toolchain::GCC)
			quote = "\"";

		append_nm = "\" -C --print-size --numeric-sort --line-numbers " + quote;
		append_nm += executablePath;
		append_nm += quote;

		append_a2l = "\" -f -e " + quote;
		append_a2l += executablePath;
		append_a2l += quote + " 0x%x";

		append_cppf = "\" -t -n ";
	}

	if (_tc->m_type == rdebug::Toolchain::PS3SNC)
	{
		append_nm	= "\" -dsy \"";
		append_nm	+= executablePath;
		append_nm	+= "\"";

		append_a2l	= "\" -a2l 0x%x -i \"";
		append_a2l	+= executablePath;
		append_a2l	+= "\"";

		append_cppf	= "\" -t -n ";

	}

#if RTM_PLATFORM_WINDOWS
	append_nm	= ".exe" + append_nm;
	append_a2l	= ".exe" + append_a2l;
	append_cppf	= ".exe" + append_cppf;
#endif

	quote = "\"";

	switch (_tc->m_type)
	{
		case rdebug::Toolchain::MSVC:
			info->m_parseSym		= 0;
			info->m_parseSymMap		= 0;
			info->m_symbolStore		= info->scratch( _tc->m_toolchainPath );
			info->m_tc_addr2line	= 0;
			info->m_tc_nm			= 0;
			info->m_tc_cppfilt		= 0;
			break;

		case rdebug::Toolchain::GCC:
		case rdebug::Toolchain::PS4:
			info->m_parseSym		= parseAddr2LineSymbolInfo;
			info->m_parseSymMap		= parseSymbolMapGNU;
			info->m_symbolStore		= 0;
			info->m_tc_addr2line	= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "addr2line" + append_a2l).c_str() );
			info->m_tc_nm			= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "nm" + append_nm).c_str() );
			info->m_tc_cppfilt		= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "c++filt" + append_cppf).c_str() );
			break;

		case rdebug::Toolchain::PS3SNC:
			info->m_parseSym		= parsePlayStationSymbolInfo;
			info->m_parseSymMap		= parseSymbolMapPS3;
			info->m_symbolStore		= 0;
			info->m_tc_addr2line	= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "ps3bin" + append_a2l).c_str() );
			info->m_tc_nm			= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "ps3bin" + append_nm).c_str() );
			info->m_tc_cppfilt		= info->scratch( (quote + _tc->m_toolchainPath + _tc->m_toolchainPrefix + "ps3name" + append_cppf).c_str() );
			break;

		case rdebug::Toolchain::Unknown:
			RTM_ERROR("Should not reach here!");
	};

	return (uintptr_t)info;
}

typedef rtm::FixedArray<ModuleInfo, ResolveInfo::MAX_MODULES> ModuleInfoArray;

uintptr_t symbolResolverCreateForCurrentProcess()
{
#if RTM_PLATFORM_WINDOWS

	HMODULE kerneldll32	= ::GetModuleHandleA("kernel32");
	HMODULE psapiDLL	= ::LoadLibraryA("Psapi.dll");

	gFn_getModuleInformation	= (fnGetModuleInformation)loadFunc(kerneldll32, psapiDLL, "GetModuleInformation");
	gFn_enumProcessModules		= (fnEnumProcessModules)  loadFunc(kerneldll32, psapiDLL, "EnumProcessModules");
	gFn_getModuleFileNameExW	= (fnGetModuleFileNameExW)loadFunc(kerneldll32, psapiDLL, "GetModuleFileNameExW");

	Toolchain toolchain;

#if RTM_COMPILER_MSVC
	wchar_t symStoreBuffer[2048];
	if (0 == GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", (LPWSTR)symStoreBuffer, sizeof(symStoreBuffer)))
		wcscpy(symStoreBuffer, L"");
	rtm::WideToMulti symStore(symStoreBuffer);

	toolchain.m_type			= Toolchain::MSVC;
	strcpy(toolchain.m_toolchainPath, symStore);
#else
	toolchain.m_type			= Toolchain::GCC;
	strcpy(toolchain.m_toolchainPath, "");
#endif
	strcpy(toolchain.m_toolchainPrefix, "");

	ModuleInfoArray modules;

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

		    if (gFn_enumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded))
			{
				for (uint32_t i=0; i<(cbNeeded/sizeof(HMODULE)); ++i)
				{
					wchar_t szModName[MAX_PATH];

					MODULEINFO mi;
					gFn_getModuleInformation(GetCurrentProcess(), hMods[i], &mi, sizeof(mi) );
					
			        if (gFn_getModuleFileNameExW(GetCurrentProcess(), hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t)))
		            {
						rtm::WideToMulti modulePath(szModName);

					    uint64_t modBase = (uint64_t)mi.lpBaseOfDll;
						uint64_t modSize = (uint64_t)mi.SizeOfImage;
						ModuleInfo module;
						module.m_baseAddress	= modBase;
						module.m_size			= modSize;
						strcpy(module.m_modulePath, modulePath.m_ptr);
						modules.push_back(module);
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
			ModuleInfo module;
			module.m_baseAddress	= modBase;
			module.m_size			= modSize;
			strcpy(module.m_modulePath, exePath.m_ptr);
			modules.push_back(module);
			cap = Module32NextW(snapshot, &me);
		}

		CloseHandle(snapshot);
	}

	return symbolResolverCreate(&modules[0], modules.size(), &toolchain, 0);
#else
	return 0;
#endif
}

void symbolResolverDelete(uintptr_t _resolver)
{
	ResolveInfo* info = (ResolveInfo*)_resolver;
	if (info)
		rtm_delete<ResolveInfo>(info);
}

ResolveInfo::ResolveInfo()
{
	m_scratch				= (char*)rtm_alloc(sizeof(char) *  SCRATCH_MEM_SIZE);
	m_scratchPos			= 0;
	m_tc_type				= Toolchain::Unknown;
	m_tc_addr2line			= 0;
	m_tc_nm					= 0;
	m_tc_cppfilt			= 0;
	m_executablePath		= 0;
	m_executableName		= 0;
	m_parseSym				= 0;
	m_parseSymMap			= 0;
	m_baseAddress4addr2Line = 0;
	m_symbolStore			= 0;
	m_symbolMapInitialized	= false;
}

ResolveInfo::~ResolveInfo()
{
#if RTM_PLATFORM_WINDOWS
	for (uint32_t i = 0; i < m_modules.size(); ++i)
		if (m_modules[i].m_PDBFile)
			rtm_delete<PDBFile>(m_modules[i].m_PDBFile);
#endif // RTM_PLATFORM_WINDOWS
	rtm_free(m_scratch);
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
		virtual ~DiaLoadCallBack() {}

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

void loadPDB(Module& _module, const char* _symbolStore)
{
	if (!_module.m_PDBFile)
	{
		_module.m_PDBFile = rtm_new<PDBFile>();
		char symbolPath[1024];
		strcpy(symbolPath, "");
		findSymbol(_module.m_module.m_modulePath, symbolPath, _symbolStore);
		_module.m_PDBFile->load(symbolPath);
	}
}
#endif // RTM_PLATFORM_WINDOWS

void symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame* _frame)
{
	strcpy(_frame->m_moduleName, "Unknown");
	strcpy(_frame->m_file, "Unknown");
	strcpy(_frame->m_func, "Unknown");
	_frame->m_line = 0;

	ResolveInfo* info = (ResolveInfo*)_resolver;
	if (!info)
		return;

#if RTM_PLATFORM_WINDOWS
	if (info->m_tc_type == rdebug::Toolchain::MSVC)
	for (uint32_t i = 0; i<info->m_modules.size(); ++i)
	{
		if (info->m_modules[i].m_module.checkAddress(_address))
		{
			Module& module = info->m_modules[i];
			loadPDB(module, info->m_symbolStore);
			module.m_PDBFile->getSymbolByAddress(_address - module.m_module.m_baseAddress, *_frame);
			strcpy(_frame->m_moduleName, rtm::pathGetFileName(module.m_module.m_modulePath));
			return;
		}
	}
#endif // RTM_PLATFORM_WINDOWS

	if (info->m_tc_addr2line && (strlen(info->m_tc_addr2line) != 0))
	{
		strcpy(_frame->m_moduleName, info->m_executableName);

		char cmdline[8192 * 2];
#if RTM_PLATFORM_WINDOWS && RTM_COMPILER_MSVC
		sprintf_s(cmdline, 8192 * 2, info->m_tc_addr2line, _address - info->m_baseAddress4addr2Line);
#else
		sprintf(cmdline, /*4096*2,*/ info->m_tc_addr2line, _address - info->m_baseAddress4addr2Line);
#endif
		char* procOut = processGetOutputOf(cmdline, true);
		if (procOut)
		{
			info->m_parseSym(&procOut[0], *_frame);
			rtm::pathMakeAbsolute(_frame->m_file);
			processReleaseOutput(procOut);
		}

		if (strcmp(_frame->m_func, "Unknown") != 0)
			if (strlen(info->m_tc_cppfilt) != 0)
			{
#if RTM_PLATFORM_WINDOWS && RTM_COMPILER_MSVC
				sprintf_s(cmdline, 4096 * 2, "%s%s", info->m_tc_cppfilt, _frame->m_func);
#else
				sprintf(cmdline, /*4096 * 2,*/ "%s%s", info->m_tc_cppfilt, _frame->m_func);
#endif
				procOut = processGetOutputOf(cmdline, true);
				if (procOut)
				{
					size_t len = strlen(procOut);
					size_t s = 0;
					while (s < len)
					{
						if ((procOut[s] == '\r') ||
							(procOut[s] == '\n'))
						{
							procOut[s] = 0;
							break;
						}
						++s;
					}
					strcpy(_frame->m_func, procOut);

					processReleaseOutput(procOut);
				}
			}
	}
}

uint64_t symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address, bool* _isRTMdll)
{
	if (_isRTMdll)
		*_isRTMdll = false;

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
			if (_isRTMdll)
				*_isRTMdll = module.m_isRTMdll;
			return (id == 0) ? _address : id + module.m_module.m_baseAddress;
		}
	}
#endif // RTM_PLATFORM_WINDOWS

	if (info->m_tc_nm && (strlen(info->m_tc_nm) != 0) && (!info->m_symbolMapInitialized))
	{
		char cmdline[4096*2];
		strcpy(cmdline, info->m_tc_nm);

		char* procOut = processGetOutputOf(cmdline, true);

		if (procOut)
		{
			info->m_parseSymMap(procOut, info->m_symbolMap);
			info->m_symbolMapInitialized = true;

			processReleaseOutput(procOut);
		}
	}

	rdebug::Symbol* sym = info->m_symbolMap.findSymbol(_address);
	if (sym)
		return (uintptr_t)sym->m_offset + info->m_baseAddress4addr2Line;
	else
		return _address;
}

} // namespace rdebug
