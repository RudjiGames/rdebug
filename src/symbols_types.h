//--------------------------------------------------------------------------//
/// Copyright (c) 2010-2017 Milos Tosic. All Rights Reserved.              ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef __RTM_QT_UTIL_SYMBOLS_TYPES_H__
#define __RTM_QT_UTIL_SYMBOLS_TYPES_H__

#include <rdebug/inc/rdebug.h>
#include <rdebug/src/symbols_map.h>
#include <rbase/inc/containers.h>

class PDBFile;

namespace rdebug {

struct Module
{
	ModuleInfo		m_module;
	const char*		m_moduleName;
	bool			m_isRTMdll;
#if RTM_PLATFORM_WINDOWS
	PDBFile*		m_PDBFile;
#endif // RTM_PLATFORM_WINDOWS

	Module()
	{
		m_module.m_baseAddress		= 0;
		m_module.m_size				= 0;
		m_module.m_modulePath		= 0;
		m_moduleName				= 0;
		m_isRTMdll = false;
#if RTM_PLATFORM_WINDOWS
		m_PDBFile					= 0;
#endif // RTM_PLATFORM_WINDOWS
	}
};

struct ResolveInfo
{
	static const uint32_t SCRATCH_MEM_SIZE	= 128*1024;
	static const uint32_t MAX_MODULES		= 256;

	typedef rtm::FixedArray<Module, MAX_MODULES> ModuleArray;

	typedef void (*fnParseSymbol)(char* _buff, StackFrame& _frame);
	typedef void (*fnParseSymbolMap)(std::string& _buffer, SymbolMap& _symMap);

	char*				m_scratch;
	uint32_t			m_scratchPos;
	ModuleArray			m_modules;
	Toolchain::Type		m_tc_type;
	const char*			m_tc_addr2line;
	const char*			m_tc_nm;
	const char*			m_tc_cppfilt;
	const char*			m_executablePath;
	const char*			m_executableName;
	fnParseSymbol		m_parseSym;
	fnParseSymbolMap	m_parseSymMap;
	uint64_t			m_baseAddress4addr2Line;
	const char*			m_symbolStore;
	SymbolMap			m_symbolMap;
	bool				m_symbolMapInitialized;

	ResolveInfo();
	~ResolveInfo();

	char* scratch(const char* _str);
};

} // namespace rdebug

#endif // __RTM_QT_UTIL_SYMBOLS_TYPES_H__
