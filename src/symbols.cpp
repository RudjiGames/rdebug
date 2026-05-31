//--------------------------------------------------------------------------//
/// Copyright 2026 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>
#include <rbase/inc/console.h>
#include <rbase/inc/hash.h>

#include "../3rd/rust-demangle.h"
#include "../3rd/rust-demangle.c"

#include <algorithm>
#include <thread>
#include <atomic>
#include <vector>

inline static uint16_t read16(FILE* _file, uint32_t _pos)
{
	if (fseek(_file, (long)_pos, SEEK_SET) != 0)
		return 0;
	uint8_t buf[2];
	if (fread(buf, 1, 2, _file) != 2)	// short/failed read -> 0 instead of using uninitialized bytes
		return 0;
	return (uint16_t)((uint32_t)buf[0] | (uint32_t)buf[1] << 8);
}

inline static uint32_t read32(FILE* _file, uint32_t _pos)
{
	if (fseek(_file, (long)_pos, SEEK_SET) != 0)
		return 0;
	uint8_t buf[4];
	if (fread(buf, 1, 4, _file) != 4)	// short/failed read -> 0 instead of using uninitialized bytes
		return 0;
	return (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
}

#if RTM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <DIA/include/dia2.h>

#define RH_RET(_x) { fclose(file); return _x; }

int hasRichHeader(char const* _filePath)
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

	uint32_t roffset = 0;	// only tested for non-zero ("Rich" tag found)
	// 32-bit counter: a uint16_t would wrap past 0xFFFF on a crafted/garbage peOffset and loop forever.
	for (uint32_t i = relOffset; i < peOffset; i += 4)
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

// Returns 1 if the PE file contains a CodeView (PDB) debug directory entry, 0 otherwise.
// Used as a fallback when no Rich Header is present to distinguish MSVC from GCC toolchains.
int hasPDBDebugInfo(char const* _filePath, std::string* _PDBpath = 0)
{
	FILE* file = fopen(_filePath, "rb");
	if (!file)
		return 0;

	// Verify MZ signature
	uint16_t mz = read16(file, 0);
	if (mz != 0x5A4D)
		RH_RET(0);

	// e_lfanew at offset 0x3C gives the PE header file offset
	uint32_t peOffset = read32(file, 0x3c);

	// Verify PE signature "PE\0\0"
	uint32_t peSig = read32(file, peOffset);
	if (peSig != 0x00004550)
		RH_RET(0);

	// COFF header: NumberOfSections at peOffset+6, SizeOfOptionalHeader at peOffset+20
	uint16_t numSections    = read16(file, peOffset + 6);
	uint16_t sizeOfOptHdr   = read16(file, peOffset + 20);

	// Optional header starts at peOffset+24; read magic to distinguish PE32/PE32+
	uint32_t optHdrOffset = peOffset + 24;
	uint16_t optMagic     = read16(file, optHdrOffset);

	// Data directories begin at offset 96 (PE32) or 112 (PE32+) within the optional header
	uint32_t dataDirBase;
	if (optMagic == 0x020B)       // PE32+ (64-bit)
		dataDirBase = optHdrOffset + 112;
	else                          // PE32 (32-bit), magic 0x010B
		dataDirBase = optHdrOffset + 96;

	// IMAGE_DIRECTORY_ENTRY_DEBUG is index 6; each entry is 8 bytes (RVA + Size)
	uint32_t debugDirRVA  = read32(file, dataDirBase + 6 * 8);
	uint32_t debugDirSize = read32(file, dataDirBase + 6 * 8 + 4);

	if (debugDirRVA == 0 || debugDirSize == 0)
		RH_RET(0);

	// Section headers start immediately after the optional header
	uint32_t sectionsBase = peOffset + 24 + sizeOfOptHdr;

	// Convert the debug directory RVA to a file offset via section headers (40 bytes each)
	uint32_t debugDirFileOff = 0;
	for (uint16_t s = 0; s < numSections; ++s)
	{
		uint32_t sectBase      = sectionsBase + (uint32_t)s * 40;
		uint32_t sectVA        = read32(file, sectBase + 12);
		uint32_t sectRawSize   = read32(file, sectBase + 16);
		uint32_t sectRawOffset = read32(file, sectBase + 20);

		if (debugDirRVA >= sectVA && debugDirRVA < sectVA + sectRawSize)
		{
			debugDirFileOff = sectRawOffset + (debugDirRVA - sectVA);
			break;
		}
	}

	if (debugDirFileOff == 0)
		RH_RET(0);

	// Walk IMAGE_DEBUG_DIRECTORY entries (each 28 bytes); look for CodeView (Type == 2)
	uint32_t numEntries = debugDirSize / 28;
	for (uint32_t e = 0; e < numEntries; ++e)
	{
		uint32_t entryBase = debugDirFileOff + e * 28;
		uint32_t type = read32(file, entryBase + 12);

		if (type == 2) // IMAGE_DEBUG_TYPE_CODEVIEW
		{
			uint32_t sizeOfData = read32(file, entryBase + 16);
			uint32_t ptrRawData = read32(file, entryBase + 24);

			if (ptrRawData != 0 && sizeOfData > 0)
			{
				uint32_t sig = read32(file, ptrRawData);
				// "RSDS" (0x53445352) = PDB 7.0,  "NB10" (0x3031424E) = PDB 2.0
				if (sig == 0x53445352 || sig == 0x3031424E)
				{
					if (_PDBpath)
					{
						// Skip signature (4 bytes) and GUID/timestamp based on format
						uint32_t pathOffset = ptrRawData + (sig == 0x53445352 ? 24 : 16);

						// Read null-terminated PDB path string
						fseek(file, pathOffset, SEEK_SET);
						char buffer[512];
						uint32_t bytesRead = (uint32_t)fread(buffer, 1, sizeof(buffer) - 1, file);
						buffer[bytesRead] = '\0';

						// Ensure null termination within bounds
						for (uint32_t i = 0; i < bytesRead; ++i)
						{
							if (buffer[i] == '\0')
								break;
							if (i == bytesRead - 1)
								buffer[i] = '\0';
						}

						*_PDBpath = buffer;
					}
					RH_RET(1);
				}
			}
		}
	}

	RH_RET(0);
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
	bool findSymbol(const char* _path, wchar_t _outSymbolPath[4096], const char* _symbolStore);
}

namespace rdebug {

void parseAddr2LineSymbolInfo(const char* _str, StackFrame& _frame);
void parsePlayStationSymbolInfo(const char* _str, StackFrame& _frame);

void parseSymbolMapGNU(const char*  _buffer, SymbolMap& _symMap);
void parseSymbolMapPS3(const char*  _buffer, SymbolMap& _symMap);

#if RTM_PLATFORM_WINDOWS
extern char g_symStore[ResolveInfo::SYM_SERVER_BUFFER_SIZE];

bool loadPDB(Module& _module)
{
	if (!_module.m_resolver->m_PDBFile)
	{
		_module.m_resolver->m_PDBFile = rtm_new<PDBFile>();
		wchar_t symbolPath[4096];	// must match findSymbol's wchar_t[4096] contract
		symbolPath[0] = L'\0';
		const char* symStore = _module.m_resolver->m_symbolStore ? _module.m_resolver->m_symbolStore : (const char*)g_symStore;
		findSymbol(_module.m_module.m_modulePath, symbolPath, symStore);

		if (wcscmp(symbolPath, L"") != 0)
		{
			if (_module.m_resolver->m_PDBFile->load(symbolPath))
			{
				rtm::WideToMulti pdbPath(symbolPath);
				rtm::Console::info("Symbols: loaded for '%s' from '%s'\n", _module.m_moduleName, pdbPath.m_ptr);
				return true;
			}
		}

		rtm::Console::warning("Symbols: NOT resolved for '%s' (binary: '%s')\n", _module.m_moduleName, _module.m_module.m_modulePath);
	}
	return _module.m_resolver->m_PDBFile->isLoaded();
}
#endif // RTM_PLATFORM_WINDOWS

char	 g_symStore[ResolveInfo::SYM_SERVER_BUFFER_SIZE] = { 0 };

void symbolSetServerSource(const char* _symStore)
{
	size_t len = rtm::strLen(_symStore);
	if (!len)
		return;

	rtm::strlCpy(g_symStore, ResolveInfo::SYM_SERVER_BUFFER_SIZE, _symStore);
}

static symbol_status_cb	g_statusCallback	= 0;
static void*			g_statusUserData	= 0;

void symbolResolverSetStatusCallback(symbol_status_cb _callback, void* _userData)
{
	g_statusCallback	= _callback;
	g_statusUserData	= _userData;
}

// Forwards a status message to the registered callback (if any). Used by the symbol-server
// download path in pdb_file.cpp so the host app can surface resolution progress.
void rdebugReportStatus(const char* _message)
{
	if (g_statusCallback && _message)
		g_statusCallback(_message, g_statusUserData);
}

// Reads the preferred image base from a PE binary's optional header (0 on failure).
// Needed to undo ASLR for addr2line: addr2line wants imageBase + RVA, but the captured
// address is loadBase + RVA, and with ASLR loadBase != imageBase.
static uint64_t getPEImageBase(const char* _filePath)
{
	FILE* file = fopen(_filePath, "rb");
	if (!file)
		return 0;

	uint64_t imageBase = 0;
	if (read16(file, 0) == 0x5A4D)						// "MZ"
	{
		uint32_t peOff = read32(file, 0x3C);
		if (read32(file, peOff) == 0x00004550)			// "PE\0\0"
		{
			const uint32_t optHdr = peOff + 24;			// skip PE sig (4) + COFF header (20)
			uint16_t magic = read16(file, optHdr);
			if (magic == 0x20B)							// PE32+  (ImageBase: 8 bytes @ optHdr+24)
				imageBase = (uint64_t)read32(file, optHdr + 24) | ((uint64_t)read32(file, optHdr + 28) << 32);
			else if (magic == 0x10B)					// PE32   (ImageBase: 4 bytes @ optHdr+28)
				imageBase = read32(file, optHdr + 28);
		}
	}

	fclose(file);
	return imageBase;
}

// Builds a module's symbol map from its toolchain 'nm' output. Safe to call concurrently for
// different resolvers (each owns its own map). The m_symbolMapInitialized guard makes it a
// no-op if the map is already built, so the lazy fallback path stays correct and idempotent.
static void initSymbolMap(ResolveInfo* _r)
{
	if (!_r || _r->m_symbolMapInitialized)
		return;

	if (!_r->m_tc_nm || (rtm::strLen(_r->m_tc_nm) == 0) || !_r->m_parseSymMap)
		return;

	char cmdline[4096 * 2];
	rtm::strlCpy(cmdline, RTM_NUM_ELEMENTS(cmdline), _r->m_tc_nm);

	const char* procOut = processGetOutputOf(cmdline, true);
	if (procOut)
	{
		if (!rtm::strStr(procOut, "No such file"))
			_r->m_parseSymMap(procOut, _r->m_symbolMap);
		_r->m_symbolMapInitialized = true;

		processReleaseOutput(procOut);
	}
}

uintptr_t symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, const char* _executable, module_load_cb _callback, void* _data)
{
	RTM_UNUSED_3(_callback, _data, _executable);
	RTM_ASSERT(_moduleInfos, "Either module info array or toolchain desc can't be NULL");

	Resolver* resolver = rtm_new<Resolver>();

	// A suspiciously small module count here usually means the capture's module list
	// was truncated at record time (see rmem writeModuleInfo) - frames in the missing
	// modules can never be resolved.
	rtm::Console::info("Symbol resolver: processing %u modules\n", _numInfos);

#if RTM_PLATFORM_WINDOWS
	// Pre-fetch module PDBs from the symbol server into the local cache in parallel, so the
	// per-module loadPDB() calls below hit the cache instead of downloading one at a time.
	// Pure network + file I/O (no DIA/COM/GUI), so it is safe to run on a thread pool; the
	// status message is emitted from this (the calling) thread only.
	extern bool rdebugPrefetchModulePdb(const char* _modulePath, const char* _symStore);
	if (g_symStore[0] && rtm::strStr(g_symStore, "http") && (_numInfos > 1))
	{
		rdebugReportStatus("Downloading symbols from symbol server ...");

		uint32_t hw = std::thread::hardware_concurrency();
		if (hw == 0)
			hw = 4;
		// Symbol-server downloads are network I/O-bound, so a handful of concurrent transfers is the
		// sweet spot; cap concurrency so a many-core box (e.g. an Unreal dev machine) doesn't spawn
		// dozens of simultaneous connections (which the server throttles anyway).
		const uint32_t kMaxDownloadThreads = 16;
		if (hw > kMaxDownloadThreads)
			hw = kMaxDownloadThreads;
		const uint32_t threadCount = (_numInfos < hw) ? _numInfos : hw;

		std::atomic<uint32_t> nextModule(0);
		auto worker = [&]()
		{
			for (;;)
			{
				const uint32_t i = nextModule.fetch_add(1);
				if (i >= _numInfos)
					break;
				rdebugPrefetchModulePdb(_moduleInfos[i].m_modulePath, g_symStore);
			}
		};

		std::vector<std::thread> pool;
		pool.reserve(threadCount - 1);
		for (uint32_t t = 1; t < threadCount; ++t)
			pool.emplace_back(worker);
		worker();
		for (size_t t = 0; t < pool.size(); ++t)
			pool[t].join();
	}
#endif // RTM_PLATFORM_WINDOWS

	for (uint32_t i=0; i<_numInfos; ++i)
	{
		Module module;
		module.m_module		= _moduleInfos[i];
		module.m_moduleName	= rtm::pathGetFileName(module.m_module.m_modulePath);
		module.m_resolver	= rtm_new<ResolveInfo>();

		char tmpName[1024];
		rtm::strlCpy(tmpName, RTM_NUM_ELEMENTS(tmpName), module.m_moduleName);
		rtm::strToUpper(tmpName);

		if ((rtm::striCmp(tmpName,"MTUNERDLL32.DLL") == 0) || (rtm::striCmp(tmpName,"MTUNERDLL64.DLL") == 0))
			module.m_isRTMdll = true;

		const char* ext	= rtm::pathGetExt(tmpName);
		const bool crossToolChain = ((rtm::striCmp(ext, "ELF") == 0) || (rtm::striCmp(ext, "SELF") == 0)) ? true : false;

		// on Windows, fix toolchain for each module
		if (!crossToolChain)
		{
#if RTM_PLATFORM_WINDOWS
			// No Rich Header — use PDB/CodeView debug info as fallback before assuming GCC
			int hasRH = hasRichHeader(module.m_module.m_modulePath);
			if (hasRH >= 0)
			{
				if (hasRH == 1)
					module.m_module.m_toolchain.m_type = rdebug::Toolchain::MSVC;
				else
				{
					// No Rich Header — use PDB/CodeView debug info as fallback before assuming GCC
					if (hasPDBDebugInfo(module.m_module.m_modulePath))
						module.m_module.m_toolchain.m_type = rdebug::Toolchain::MSVC;
					else
						module.m_module.m_toolchain.m_type = rdebug::Toolchain::GCC;
				}
			}
#endif // RTM_PLATFORM_WINDOWS
		}

		// Resolve each module against its OWN binary so shared-library / secondary
		// module frames resolve too (previously every module used the main executable).
		const char* moduleBinary = module.m_module.m_modulePath;

		// addr2line wants the address as it appears in the binary on disk:
		//  - ELF/SELF/PS (PIE, preferred base 0): that's the RVA -> subtract the load base.
		//  - Windows PE (MinGW/GCC): preferredImageBase + RVA. With ASLR (default in recent
		//    MinGW/MSYS2) the runtime load base differs from the on-disk image base, so undo
		//    the relocation by subtracting (loadBase - imageBase). Without this, ASLR'd builds
		//    resolve every frame to Unknown.
		if (module.m_module.m_toolchain.m_type != rdebug::Toolchain::MSVC)
		{
			if (crossToolChain)
			{
				module.m_resolver->m_baseAddress4addr2Line = module.m_module.m_baseAddress;
			}
			else
			{
				const uint64_t imageBase = getPEImageBase(module.m_module.m_modulePath);
				if (imageBase)
					module.m_resolver->m_baseAddress4addr2Line = module.m_module.m_baseAddress - imageBase;
			}
		}

		if (moduleBinary && moduleBinary[0])
		{
			module.m_resolver->m_executablePath = module.m_resolver->scratch(moduleBinary);
			module.m_resolver->m_executableName = module.m_resolver->m_executablePath ? rtm::pathGetFileName(module.m_resolver->m_executablePath) : 0;
		}

		std::string append_nm;
		std::string append_a2l;
		std::string append_cppf;

		std::string quote;

		if ((module.m_module.m_toolchain.m_type == rdebug::Toolchain::GCC) ||
			(module.m_module.m_toolchain.m_type == rdebug::Toolchain::PS4) ||
			(module.m_module.m_toolchain.m_type == rdebug::Toolchain::PS5))
		{
			if (module.m_module.m_toolchain.m_type == rdebug::Toolchain::GCC)
				quote = "\"";

			append_nm = "\" -C --print-size --numeric-sort --line-numbers " + quote;
			append_nm += moduleBinary;
			append_nm += quote;

			append_a2l = "\" -f -e " + quote;
			append_a2l += moduleBinary;
			append_a2l += quote + " 0x%llx";	// 64-bit: %x truncated 64-bit (e.g. mingw64) addresses

			append_cppf = "\" -t -n ";
		}

		if (module.m_module.m_toolchain.m_type == rdebug::Toolchain::PS3SNC)
		{
			append_nm = "\" -dsy \"";
			append_nm += moduleBinary;
			append_nm += "\"";

			append_a2l = "\" -a2l 0x%llx -i \"";
			append_a2l += moduleBinary;
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
			module.m_resolver->m_symbolStore	= 0;
			module.m_resolver->m_tc_addr2line	= 0;
			module.m_resolver->m_tc_nm			= 0;
			module.m_resolver->m_tc_cppfilt		= 0;
			break;

		case rdebug::Toolchain::GCC:
		case rdebug::Toolchain::PS4:
		case rdebug::Toolchain::PS5:
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

		module.m_resolver->m_symbolStore = module.m_resolver->scratch(module.m_module.m_toolchain.m_toolchainPath);

#if RTM_PLATFORM_WINDOWS
		if (loadPDB(module) && _callback)
			_callback(module.m_moduleName, _data);
#endif

		resolver->m_modules.push_back(module);
	}

	std::sort(resolver->m_modules.m_data, resolver->m_modules.m_data + resolver->m_modules.size(),
		[](const Module& a, const Module& b)
		{
			return a.m_module.m_baseAddress < b.m_module.m_baseAddress;
		});

#if RTM_PLATFORM_WINDOWS
	// Pre-extract per-module 'nm' symbol maps up front and in parallel instead of paying each
	// nm sub-process serially on first lookup during analysis. nm resolution only has a real
	// implementation on a Windows host (cross-toolchain GCC/PS captures), and each module owns
	// its own resolver/map, so the work is embarrassingly parallel with no shared state.
	{
		const uint32_t moduleCount = resolver->m_modules.size();
		if (moduleCount > 1)
		{
			uint32_t hw = std::thread::hardware_concurrency();
			if (hw == 0)
				hw = 4;
			const uint32_t threadCount = (moduleCount < hw) ? moduleCount : hw;

			std::atomic<uint32_t> nextModule(0);
			auto worker = [&]()
			{
				for (;;)
				{
					const uint32_t i = nextModule.fetch_add(1);
					if (i >= moduleCount)
						break;
					initSymbolMap(resolver->m_modules[i].m_resolver);
				}
			};

			std::vector<std::thread> pool;
			pool.reserve(threadCount - 1);
			for (uint32_t t=1; t<threadCount; ++t)
				pool.emplace_back(worker);
			worker();						// also do work on the calling thread
			for (size_t t=0; t<pool.size(); ++t)
				pool[t].join();
		}
		else if (moduleCount == 1)
			initSymbolMap(resolver->m_modules[0].m_resolver);
	}
#endif // RTM_PLATFORM_WINDOWS

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
	wchar_t symStoreBuffer[4096];
	if (0 == GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", (LPWSTR)symStoreBuffer, RTM_NUM_ELEMENTS(symStoreBuffer)))
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
				// EnumProcessModules reports the TOTAL bytes needed, which can exceed our buffer;
				// clamp so we never read past hMods, and never push past the modules array.
				uint32_t modCount = (uint32_t)(cbNeeded / sizeof(HMODULE));
				if (modCount > RTM_NUM_ELEMENTS(hMods))
					modCount = RTM_NUM_ELEMENTS(hMods);
				for (uint32_t i=0; i<modCount && modules.size()<Resolver::MAX_MODULES; ++i)
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
		while (cap && (modules.size() < Resolver::MAX_MODULES))	// FixedArray push_back is unchecked in release
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
	RTM_ASSERT(_resolver, "Invalid resolver!");
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

// (The DIA load callback that captures the resolved PDB path lives in pdb_file.cpp, alongside
// findSymbol() which actually drives loadDataForExe; the former duplicate here was dead code.)

inline const Module* addressGetModule(uintptr_t _resolver, uint64_t _address)
{
	const Resolver* resolver = (Resolver*)_resolver;

	int32_t minIndex = 0;
	int32_t maxIndex = resolver->m_modules.size() - 1;

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

struct StringData
{
	const static int STRING_DATA_SIZE = 32 * 1024 - 4;

	uint32_t m_length;
	char	 m_data[STRING_DATA_SIZE];
	StringData() : m_length(0) {}
};

void rustDemangleCallback(const char* data, size_t len, void* opaque)
{
	StringData* str = (StringData*)opaque;
	// Clamp to the space remaining so a very long mangled name can never write
	// past m_data (the buffer must also keep room for the null terminator).
	const uint32_t space = (str->m_length < StringData::STRING_DATA_SIZE)
							? (uint32_t)(StringData::STRING_DATA_SIZE - 1 - str->m_length)
							: 0;
	uint32_t toCopy = (len < space) ? (uint32_t)len : space;
	rtm::memCopy(&str->m_data[str->m_length], space, data, toCopy);
	str->m_length += toCopy;
	str->m_data[str->m_length] = '\0';
}

void symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame* _frame)
{
	rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), "Unknown");
	rtm::strlCpy(_frame->m_file, RTM_NUM_ELEMENTS(_frame->m_file), "Unknown");
	rdebug::addressToString(_address, _frame->m_func);
	_frame->m_line = 0;

	Resolver* resolver = (Resolver*)_resolver;
	if (!resolver)
		return;

	const Module* module = addressGetModule(_resolver, _address);
	if (!module)
		return;

	// The address maps to a known module - always show that module's name, even when
	// the function/file can't be resolved, so the tree shows e.g. "kernel32.dll"
	// instead of just the raw address.
	rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), rtm::pathGetFileName(module->m_module.m_modulePath));

#if RTM_PLATFORM_WINDOWS
	if (module->m_resolver->m_PDBFile && module->m_resolver->m_PDBFile->isLoaded())
	{
		bool found = module->m_resolver->m_PDBFile->getSymbolByAddress(_address - module->m_module.m_baseAddress, *_frame);
		rtm::strlCpy(_frame->m_moduleName, RTM_NUM_ELEMENTS(_frame->m_moduleName), rtm::pathGetFileName(module->m_module.m_modulePath));

		StringData str;
		if (rust_demangle_with_callback(_frame->m_func, 0, rustDemangleCallback, &str))
			rtm::strlCpy(_frame->m_func, RTM_NUM_ELEMENTS(_frame->m_func), str.m_data);

		if (found)
			return;
	}
#endif // RTM_PLATFORM_WINDOWS

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

					StringData str;
					if (rust_demangle_with_callback(_frame->m_func, 0, rustDemangleCallback, &str))
						rtm::strlCpy(_frame->m_func, RTM_NUM_ELEMENTS(_frame->m_func), str.m_data);

					processReleaseOutput(procOut);
				}
			}
	}
}

uint64_t symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address)
{
	Resolver* resolver = (Resolver*)_resolver;
	if (!resolver)
		return _address;

	const Module* module = addressGetModule(_resolver, _address);
	if (!module)
		return _address;

	if (module->m_isRTMdll)
		return 0;

#if RTM_PLATFORM_WINDOWS
	if (module->m_resolver->m_PDBFile)
	{
		uint64_t id = module->m_resolver->m_PDBFile->getSymbolID(_address - module->m_module.m_baseAddress);
		return id + module->m_module.m_baseAddress;
	}
#endif // RTM_PLATFORM_WINDOWS

	// Lazy fallback: normally the map was pre-extracted (in parallel) at resolver creation,
	// but build it on demand if not (idempotent thanks to the m_symbolMapInitialized guard).
	initSymbolMap(module->m_resolver);

	// Look up using the on-disk address (undo ASLR / load-base) so it matches the symbol
	// values nm reported - same correction addr2line uses in symbolResolverGetFrame.
	rdebug::Symbol sym;
	const uint64_t symAddress = _address - module->m_resolver->m_baseAddress4addr2Line;
	if (module->m_resolver->m_symbolMap.findSymbol(symAddress, sym))
		return (uint64_t)rtm::hashStr(sym.m_name.c_str());
	else
		return _address;
}

} // namespace rdebug
