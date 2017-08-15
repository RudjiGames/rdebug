//--------------------------------------------------------------------------//
/// Copyright (c) 2017 by Milos Tosic. All Rights Reserved.                /// 
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef __RTM_QT_UTIL_SYMBOLS_MAP_H__
#define __RTM_QT_UTIL_SYMBOLS_MAP_H__

#include <rbase/inc/containers.h>
#include <vector>
#include <string>

namespace rdebug {

struct Symbol
{
	int64_t			m_offset;
	uint64_t		m_size;
	uint32_t		m_line;
	std::string		m_file;
	std::string		m_name;

	Symbol();
};

struct SymbolMap
{
	std::vector<Symbol>	m_symbols;

	void	addSymbol(const Symbol& _sym);
	void	sort();
	Symbol* findSymbol(uint64_t _address);
};

} // namespace rdebug

#endif // __RTM_QT_UTIL_SYMBOLS_MAP_H__
