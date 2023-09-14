//--------------------------------------------------------------------------//
/// Copyright 2023 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_RDEBUG_PDB_H
#define RTM_RDEBUG_PDB_H

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
		rtm_string			m_sFileName;
		IDiaDataSource*		m_pIDiaDataSource;
		IDiaSession*		m_pIDiaSession;
		IDiaSymbol*			m_pIDiaSymbol;
		bool				m_isStripped;

	public:
		PDBFile();
		~PDBFile();

		bool		load(const char* _filename);
		void		getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame);
		uint64_t	getSymbolID(uint64_t _address);
		void		close();

	private:
		bool		loadSymbolsFileWithoutValidation(const char* _PdbFileName);
};

#endif // RTM_PLATFORM_WINDOWS

#endif // RTM_RDEBUG_PDB_H
