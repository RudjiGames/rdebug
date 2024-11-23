//--------------------------------------------------------------------------//
/// Copyright 2024 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_RQT_SYMBOLS_TYPES_H
#define RTM_RQT_SYMBOLS_TYPES_H

#include <rdebug/inc/rdebug.h>
#include <rdebug/src/symbols_map.h>
#include <rbase/inc/containers.h>

class PDBFile;

namespace rdebug {

struct ResolveInfo
{
	static const uint32_t SYM_SERVER_BUFFER_SIZE = 32 * 1024;
	static const uint32_t SCRATCH_MEM_SIZE = 64 * 1024;

	typedef void(*fnParseSymbol)(const char* _buff, StackFrame& _frame);
	typedef void(*fnParseSymbolMap)(const char* _buffer, SymbolMap& _symMap);

	char*				m_scratch;
	uint32_t			m_scratchPos;
	const char*			m_tc_addr2line;
	const char*			m_tc_nm;
	const char*			m_tc_cppfilt;
	const char*			m_executablePath;
	const char*			m_executableName;
#if RTM_PLATFORM_WINDOWS
	PDBFile*			m_PDBFile;
#endif // RTM_PLATFORM_WINDOWS

	fnParseSymbol		m_parseSym;
	fnParseSymbolMap	m_parseSymMap;
	uint64_t			m_baseAddress4addr2Line;
	const char*			m_symbolStore;
	SymbolMap			m_symbolMap;
	bool				m_symbolMapInitialized;
	const char*			m_symbolCache;

	ResolveInfo();
	~ResolveInfo();

	char* scratch(const char* _str);
};

struct Module
{
	ModuleInfo		m_module;
	const char*		m_moduleName;
	bool			m_isRTMdll;
	ResolveInfo*	m_resolver;

	Module()
	{
		m_module.m_baseAddress		= 0;
		m_module.m_size				= 0;
		m_module.m_modulePath[0]	= '\0';
		m_moduleName				= 0;
		m_isRTMdll					= false;
	}
};

struct Resolver
{
	static const uint32_t MAX_MODULES = 512;

	typedef rtm::FixedArray<Module, MAX_MODULES> ModuleArray;

	ModuleArray	m_modules;
};

} // namespace rdebug

#endif // RTM_RQT_SYMBOLS_TYPES_H
