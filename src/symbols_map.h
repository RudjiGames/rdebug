//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_RDEBUG_SYMBOLS_MAP_H
#define RTM_RDEBUG_SYMBOLS_MAP_H

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
};

struct SymbolMap
{
	struct SymbolData
	{
		int64_t			m_offset;
		uint64_t		m_size;
		uint32_t		m_line;
		uint32_t		m_stringsIndex;
	};

	struct SymbolStrings
	{
		std::string		m_file;
		std::string		m_name;
	};

	std::vector<SymbolData>		m_symbols;
	std::vector<SymbolStrings>	m_symbolStrings;

	void	addSymbol(const char* _name, int64_t _offset, uint64_t _size, uint32_t _line, const char* _file);
	void	sort();
	bool	findSymbol(uint64_t _address, Symbol& _symbol);
};

} // namespace rdebug

#endif // RTM_RDEBUG_SYMBOLS_MAP_H
