//--------------------------------------------------------------------------//
/// Copyright (c) 2019 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>
#include <rbase/inc/console.h>

#include <algorithm>

#if RTM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <DIA/include/dia2.h>

inline static uint16_t read16(FILE* _file, uint16_t _pos)
{
	fseek(_file, _pos, SEEK_SET);
	uint8_t buf[2];
	fread(buf, 1, 2, _file);
	return	(uint32_t)buf[0] |
		(uint32_t)buf[1] << 8;
}

inline static uint32_t read32(FILE* _file, uint16_t _pos)
{
	fseek(_file, _pos, SEEK_SET);
	uint8_t buf[4];
	fread(buf, 1, 4, _file);
	return	(uint32_t)buf[0] |
		(uint32_t)buf[1] << 8 |
		(uint32_t)buf[2] << 16 |
		(uint32_t)buf[3] << 24;
}

#define RH_RET(_x) { fclose(file); return _x; }

int hasRichheader(char const* _filePath)
{
	FILE* file = fopen(_filePath, "rb");
	if (!file)
		return -1;

	uint16_t mz = read16(file, 0);
	if (mz != 0x5A4D)
		RH_RET(0);

	uint16_t numRel = read16(file, 6);
	uint16_t header = read16(file, 8);
	if (header < 4)
		RH_RET(0);

	uint16_t relOffset = read16(file, 0x18);
	uint16_t peOffset = read16(file, 0x3c);
	if (peOffset < header * 16)
		RH_RET(0);

	uint32_t t = read32(file, peOffset);
	if (t != 0x4550)
		RH_RET(0);

	if (numRel > 0)
		relOffset += 4 * numRel;

	if (relOffset % 16)
		relOffset += 16 - (relOffset % 16);

	uint16_t roffset = 0;
	for (uint16_t i = relOffset; i < peOffset; i += 4)
	{
		t = read32(file, i);
		if (t == 0x68636952)
		{
			roffset = i + 4;
			break;
		}
	}

	if (roffset == 0)
		RH_RET(0);

	RH_RET(1);
}

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
	bool findSymbol(const char* _path, char _outSymbolPath[2048], const char* _symbolStore);
}

namespace rdebug {

void parseAddr2LineSymbolInfo(const char* _str, StackFrame& _frame);
void parsePlayStationSymbolInfo(const char* _str, StackFrame& _frame);

void parseSymbolMapGNU(const char*  _buffer, SymbolMap& _symMap);
void parseSymbolMapPS3(const char*  _buffer, SymbolMap& _symMap);

#if RTM_PLATFORM_WINDOWS
void loadPDB(Module& _module)
{
	if (!_module.m_resolver->m_PDBFile)
	{
		_module.m_resolver->m_PDBFile = rtm_new<PDBFile>();
		char symbolPath[1024];
		rtm::strlCpy(symbolPath, RTM_NUM_ELEMENTS(symbolPath), "");
		findSymbol(_module.m_module.m_modulePath, symbolPath, _module.m_resolver->m_symbolStore);
		_module.m_resolver->m_PDBFile->load(symbolPath);
	}
}
#endif // RTM_PLATFORM_WINDOWS

uintptr_t symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, const char* _executable, module_load_cb _callback, void* _data)
{
	RTM_ASSERT(_moduleInfos, "Either module info array or toolchain desc can't be NULL");

	Resolver* resolver = rtm_new<Resolver>();

	const char* executablePath = 0;
	const char* exeName = _executable ? rtm::pathGetFileName(_executable) : 0;

	for (uint32_t i=0; i<_numInfos; ++i)
	{
		Module module;
		module.m_module		= _moduleInfos[i];
		module.m_moduleName	= rtm::pathGetFileName(module.m_module.m_modulePath);
		module.m_resolver	= rtm_new<ResolveInfo>();

		char tmpName[1024];
		rtm::strlCpy(tmpName, RTM_NUM_ELEMENTS(tmpName), module.m_moduleName);
		rtm::strToUpper(tmpName);

		if ((rtm::strCmp(tmpName,"MTUNERDLL32.DLL") == 0) || (rtm::strCmp(tmpName,"MTUNERDLL64.DLL") == 0))
			module.m_isRTMdll = true;

		const char* ext		= rtm::pathGetExt(tmpName);

		// on Windows, fix toolchain for each module
		if ((rtm::strCmp(ext, "EXE") == 0) || (rtm::strCmp(ext, "DLL") == 0))
		{
#if RTM_PLATFORM_WINDOWS
			int hasRH = hasRichheader(module.m_module.m_modulePath);
			if (hasRH >= 0)
				module.m_module.m_toolchain.m_type = (hasRH == 1) ? rdebug::Toolchain::MSVC : rdebug::Toolchain::GCC;
#endif // RTM_PLATFORM_WINDOWS
		}

		if (ext)
		{
			if ((rtm::strCmp(ext, "EXE") == 0) || (rtm::strCmp(ext, "ELF") == 0))
				executablePath = _moduleInfos[i].m_modulePath;

			if (((rtm::strCmp(module.m_moduleName, exeName) == 0)) &&
				!((rtm::strCmp(ext, "EXE") == 0) || (rtm::strCmp(ext, "DLL") == 0)))
					module.m_resolver->m_baseAddress4addr2Line = module.m_module.m_baseAddress;
		}

		if (executablePath)
		{
			module.m_resolver->m_executablePath = module.m_resolver->scratch(executablePath);
			module.m_resolver->m_executableName = module.m_resolver->m_executablePath ? rtm::pathGetFileName(module.m_resolver->m_executablePath) : 0;
		}

		rtm_string append_nm;
		rtm_string append_a2l;
		rtm_string append_cppf;

		rtm_string quote;

		if ((module.m_module.m_toolchain.m_type == rdebug::Toolchain::GCC) ||
			(module.m_module.m_toolchain.m_type == rdebug::Toolchain::PS4))
		{
			if (module.m_module.m_toolchain.m_type == rdebug::Toolchain::GCC)
				quote = "\"";

			append_nm = "\" -C --print-size --numeric-sort --line-numbers " + quote;
			append_nm += executablePath;
			append_nm += quote;

			append_a2l = "\" -f -e " + quote;
			append_a2l += executablePath;
			append_a2l += quote + " 0x%x";

			append_cppf = "\" -t -n ";
		}

		if (module.m_module.m_toolchain.m_type == rdebug::Toolchain::PS3SNC)
		{
			append_nm = "\" -dsy \"";
			append_nm += executablePath;
			append_nm += "\"";

			append_a2l = "\" -a2l 0x%x -i \"";
			append_a2l += executablePath;
			append_a2l += "\"";

			append_cppf = "\" -t -n ";
		}

#if RTM_PLATFORM_WINDOWS
		append_nm = ".exe" + append_nm;
		append_a2l = ".exe" + append_a2l;
		append_cppf = ".exe" + append_cppf;
#endif

		quote = "\"";

		switch (module.m_module.m_toolchain.m_type)
		{
		case rdebug::Toolchain::MSVC:
			module.m_resolver->m_parseSym		= 0;
			module.m_resolver->m_parseSymMap	= 0;
			module.m_resolver->m_symbolStore	= module.m_resolver->scratch(module.m_module.m_toolchain.m_toolchainPath);
			module.m_resolver->m_tc_addr2line	= 0;
			module.m_resolver->m_tc_nm			= 0;
			module.m_resolver->m_tc_cppfilt		= 0;
			break;

		case rdebug::Toolchain::GCC:
		case rdebug::Toolchain::PS4:
			module.m_resolver->m_parseSym		= parseAddr2LineSymbolInfo;
			module.m_resolver->m_parseSymMap	= parseSymbolMapGNU;
			module.m_resolver->m_symbolStore	= 0;
			module.m_resolver->m_tc_addr2line	= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "addr2line" + append_a2l).c_str());
			module.m_resolver->m_tc_nm			= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "nm" + append_nm).c_str());
			module.m_resolver->m_tc_cppfilt		= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "c++filt" + append_cppf).c_str());
			break;

		case rdebug::Toolchain::PS3SNC:
			module.m_resolver->m_parseSym		= parsePlayStationSymbolInfo;
			module.m_resolver->m_parseSymMap	= parseSymbolMapPS3;
			module.m_resolver->m_symbolStore	= 0;
			module.m_resolver->m_tc_addr2line	= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "ps3bin" + append_a2l).c_str());
			module.m_resolver->m_tc_nm			= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "ps3bin" + append_nm).c_str());
			module.m_resolver->m_tc_cppfilt		= module.m_resolver->scratch((quote + module.m_module.m_toolchain.m_toolchainPath + module.m_module.m_toolchain.m_toolchainPrefix + "ps3name" + append_cppf).c_str());
			break;

		case rdebug::Toolchain::Unknown:
			rtm::Console::info("Toolchain is not configured, no symbols can be resolved!\n");
		};

#if RTM_PLATFORM_WINDOWS
		if (module.m_module.m_toolchain.m_type == rdebug::Toolchain::MSVC)
		{
			loadPDB(module);
			if (_callback)
				_callback(module.m_moduleName, _data);

		}
#endif

		resolver->m_modules.push_back(module);
	}

	std::sort(&resolver->m_modules[0], &resolver->m_modules[resolver->m_modules.size()-1],
		[](const Module& a, const Module& b)
		{ 
			return a.m_module.m_baseAddress < b.m_module.m_baseAddress; 
		});

	return (uintptr_t)resolver;
}

uintptr_t symbolResolverCreateForCurrentProcess()
{
#if RTM_PLATFORM_WINDOWS
	rtm::FixedArray<ModuleInfo, Resolver::MAX_MODULES> modules;

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
	rtm::strlCpy(toolchain.m_toolchainPath, RTM_NUM_ELEMENTS(toolchain.m_toolchainPath), symStore);
#else
	toolchain.m_type			= Toolchain::GCC;
	rtm::strlCpy(toolchain.m_toolchainPath, RTM_NUM_ELEMENTS(toolchain.m_toolchainPath), "");
#endif
	rtm::strlCpy(toolchain.m_toolchainPrefix, RTM_NUM_ELEMENTS(toolchain.m_toolchainPrefix), "");


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
						rtm::strlCpy(module.m_modulePath, RTM_NUM_ELEMENTS(module.m_modulePath), modulePath.m_ptr);
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
			rtm::strlCpy(module.m_modulePath, RTM_NUM_ELEMENTS(module.m_modulePath), exePath.m_ptr);
			modules.push_back(module);
			cap = Module32NextW(snapshot, &me);
		}

		CloseHandle(snapshot);
	}

	return symbolResolverCreate(&modules[0], modules.size(), 0);
#else
	return 0;
#endif
}

void symbolResolverDelete(uintptr_t _resolver)
{
	Resolver* resolver = (Resolver*)_resolver;

	for (uint32_t i=0; i<resolver->m_modules.size(); ++i)
	{
		Module& module = resolver->m_modules[i];
		if (module.m_resolver)
			rtm_delete<ResolveInfo>(module.m_resolver);
	}

	if (resolver)
		rtm_delete<Resolver>(resolver);
}

ResolveInfo::ResolveInfo()
{
	m_scratch				= (char*)rtm_alloc(sizeof(char) *  SCRATCH_MEM_SIZE);
	m_scratchPos			= 0;
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
	m_symbolCache			= 0;
#if RTM_PLATFORM_WINDOWS
	m_PDBFile				= 0;
#endif // RTM_PLATFORM_WINDOWS
}

ResolveInfo::~ResolveInfo()
{
#if RTM_PLATFORM_WINDOWS
	if (m_PDBFile)
		rtm_delete<PDBFile>(m_PDBFile);
#endif // RTM_PLATFORM_WINDOWS
	rtm_free(m_scratch);
}

char* ResolveInfo::scratch(const char* _str)
{
	RTM_ASSERT(_str != 0, "null string!");
	size_t len = rtm::strLen(_str) + 1;
	RTM_ASSERT(m_scratchPos + len < SCRATCH_MEM_SIZE, "Scratch buffer full!");
	char* ret = &m_scratch[m_scratchPos];
	rtm::strlCpy(ret, SCRATCH_MEM_SIZE - m_scratchPos, _str);
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
#endif // RTM_PLATFORM_WINDOWS

inline const Module* addressGetModule(uintptr_t _resolver, uint64_t _address)
{
	const Resolver* resolver = (Resolver*)_resolver;

	uint32_t minIndex = 0;
	uint32_t maxIndex = resolver->m_modules.size() - 1;

	while (minIndex <= maxIndex)
	{
		uint32_t curIndex = minIndex + (maxIndex - minIndex ) / 2;

		const Module& module = resolver->m_modules[curIndex];
		if (module.m_module.checkAddress(_address))
			return &module;

		if (_address < module.m_module.m_baseAddress)
			maxIndex = curIndex - 1;
		else
			minIndex = curIndex + 1;
	}

	return 0;
}

void symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame* _frame)
{
	rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), "Unknown");
	rtm::strlCpy(_frame->m_file, RTM_NUM_ELEMENTS(_frame->m_file), "Unknown");
	rtm::strlCpy(_frame->m_func, RTM_NUM_ELEMENTS(_frame->m_func), "Unknown");
	_frame->m_line = 0;

	Resolver* resolver = (Resolver*)_resolver;
	if (!resolver)
		return;

	const Module* module = addressGetModule(_resolver, _address);
	if (!module)
		return;

	if (module->m_module.m_toolchain.m_type == rdebug::Toolchain::MSVC)
	{
#if RTM_PLATFORM_WINDOWS
		module->m_resolver->m_PDBFile->getSymbolByAddress(_address - module->m_module.m_baseAddress, *_frame);
		rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), rtm::pathGetFileName(module->m_module.m_modulePath));
		return;
#endif // RTM_PLATFORM_WINDOWS
	}
	else
	{
		if (module->m_resolver->m_tc_addr2line && (module->m_resolver->m_tc_addr2line[0] != '\0'))
		{
			rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), module->m_resolver->m_executableName);

			constexpr int MAX_CMDLINE_SIZE = 16384 + 8192;
			char cmdline[MAX_CMDLINE_SIZE];
#if RTM_PLATFORM_WINDOWS && RTM_COMPILER_MSVC
			sprintf_s(cmdline, MAX_CMDLINE_SIZE, module->m_resolver->m_tc_addr2line, _address - module->m_resolver->m_baseAddress4addr2Line);
#else
			snprintf(cmdline, MAX_CMDLINE_SIZE, module->m_resolver->m_tc_addr2line, _address - module->m_resolver->m_baseAddress4addr2Line);
#endif
			char* procOut = processGetOutputOf(cmdline, true);
			if (procOut && !rtm::strStr(procOut, "No such file"))
			{
				module->m_resolver->m_parseSym(&procOut[0], *_frame);
				rtm::pathCanonicalize(_frame->m_file);
				processReleaseOutput(procOut);
			}

			if (rtm::strCmp(_frame->m_func, "Unknown") != 0)
				if (rtm::strLen(module->m_resolver->m_tc_cppfilt) != 0)
				{
#if RTM_PLATFORM_WINDOWS && RTM_COMPILER_MSVC
					sprintf_s(cmdline, MAX_CMDLINE_SIZE, "%s%s", module->m_resolver->m_tc_cppfilt, _frame->m_func);
#else
					snprintf(cmdline, MAX_CMDLINE_SIZE, "%s%s", module->m_resolver->m_tc_cppfilt, _frame->m_func);
#endif
					procOut = processGetOutputOf(cmdline, true);
					if (procOut)
					{
						size_t len = rtm::strLen(procOut);
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
						rtm::strlCpy(_frame->m_func, RTM_NUM_ELEMENTS(_frame->m_func), procOut);

						processReleaseOutput(procOut);
					}
				}
		}
	}
}

uint64_t symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address, int& _skipCount)
{
	Resolver* resolver = (Resolver*)_resolver;
	if (!resolver)
		return _address;

	const Module* module = addressGetModule(_resolver, _address);
	if (!module)
		return _address;

	if (module->m_module.m_toolchain.m_type == rdebug::Toolchain::MSVC)
	{
#if RTM_PLATFORM_WINDOWS
		uint64_t id = module->m_resolver->m_PDBFile->getSymbolID(_address - module->m_module.m_baseAddress);
		if (module->m_isRTMdll)
			_skipCount++;
		return id + module->m_module.m_baseAddress;
#endif // RTM_PLATFORM_WINDOWS
	}
	else
	{
		if (module->m_resolver->m_tc_nm && (rtm::strLen(module->m_resolver->m_tc_nm) != 0) && (!module->m_resolver->m_symbolMapInitialized))
		{
			char cmdline[4096 * 2];
			rtm::strlCpy(cmdline, RTM_NUM_ELEMENTS(cmdline), module->m_resolver->m_tc_nm);

			const char* procOut = processGetOutputOf(cmdline, true);

			if (procOut)
			{
				if (!rtm::strStr(procOut, "No such file"))
					module->m_resolver->m_parseSymMap(procOut, module->m_resolver->m_symbolMap);
				module->m_resolver->m_symbolMapInitialized = true;

				processReleaseOutput(procOut);
			}
		}

		rdebug::Symbol* sym = module->m_resolver->m_symbolMap.findSymbol(_address);
		if (sym)
			return (uint64_t)sym->m_nameHash;
		else
			return _address;
	}
}

} // namespace rdebug
