//--------------------------------------------------------------------------//
/// Copyright 2026 Milos Tosic. All Rights Reserved.                       ///
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
#include <map>
#include <utility>

class PDBFile
{
	private:
		std::string			m_sFileName;
		IDiaDataSource*		m_pIDiaDataSource;
		IDiaSession*		m_pIDiaSession;
		IDiaSymbol*			m_pIDiaSymbol;
		bool				m_isStripped;

		// Function RVA-range -> symbol-index-ID cache. getSymbolID() answers any address inside an
		// already-resolved function from this map instead of issuing another DIA findSymbolByVA -
		// the per-address COM query was the dominant cost when generating unique symbol IDs for
		// large captures. Keyed by function start RVA; value is {endRVA, symIndexId}.
		std::map<uint64_t, std::pair<uint64_t, uint64_t> >	m_symbolRangeCache;

	public:
		PDBFile();
		~PDBFile();

		bool		load(const wchar_t* _filename);
		bool		isLoaded() const;
		bool		getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame);
		uint64_t	getSymbolID(uint64_t _address);
		void		close();

	private:
		bool		loadSymbolsFileWithoutValidation(const wchar_t* _PdbFileName);
};

#endif // RTM_PLATFORM_WINDOWS

#endif // RTM_RDEBUG_PDB_H
