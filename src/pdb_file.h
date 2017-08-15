//--------------------------------------------------------------------------//
/// Copyright (c) 2017 by Milos Tosic. All Rights Reserved.                /// 
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef __RTM_RDEBUG_PDB_H__
#define __RTM_RDEBUG_PDB_H__

#include <rbase/inc/platform.h>

#if RTM_PLATFORM_WINDOWS

#include <rdebug/inc/rdebug.h>
#include <rdebug/src/symbols_map.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <comdef.h>
#include <DIA/include/dia2.h>

class PDBFile
{
	private:
		std::wstring		m_sFileName;
		IDiaDataSource*		m_pIDiaDataSource;
		IDiaSession*		m_pIDiaSession;
		IDiaSymbol*			m_pIDiaSymbol;
		bool				m_isStripped;
		rdebug::SymbolMap	m_symMap;

	public:
		PDBFile();
		~PDBFile();

		bool		load(const wchar_t* _filename);
		void		getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame);
		uintptr_t	getSymbolID(uint64_t _address);
		void		close();

	private:
		bool		loadSymbolsFileWithoutValidation(const wchar_t* _PdbFileName);
};

#endif // RTM_PLATFORM_WINDOWS

#endif // __RTM_RDEBUG_PDB_H__
