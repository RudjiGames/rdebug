//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/symbols_map.h>

#include <algorithm>

namespace rdebug {

void SymbolMap::addSymbol(const char* _name, int64_t _offset, uint64_t _size, uint32_t _line, const char* _file)
{
	SymbolData data;
	data.m_offset		= _offset;
	data.m_size			= _size;
	data.m_line			= _line;
	data.m_stringsIndex = 0;
	
	if (!m_symbols.empty())
	{
		SymbolData& s = m_symbols[m_symbols.size()-1];
		// same offset means same symbol, so update it instead of adding a new one
		if (s.m_offset == _offset)
		{
			// reuse the same string index for the symbol with the same offset
			data.m_stringsIndex = s.m_stringsIndex;
			m_symbols[m_symbols.size()-1] = data;
			m_symbolStrings[data.m_stringsIndex].m_file = _file;
			m_symbolStrings[data.m_stringsIndex].m_name = _name;
			return;
		}
	}

	m_symbolStrings.push_back({ _file, _name });
	data.m_stringsIndex = (uint32_t)m_symbolStrings.size() - 1;
	m_symbols.push_back(data);
}

bool SymbolMap::findSymbol(uint64_t _address, Symbol& _symbol)
{
	size_t len = m_symbols.size();
	if (!len)
		return 0;

	// Handle single-element case
	if (len == 1)
	{
		SymbolData& sym = m_symbols[0];
		if (uint64_t(_address - sym.m_offset) < sym.m_size)
		{
			_symbol.m_offset	= sym.m_offset;
			_symbol.m_size		= sym.m_size;
			_symbol.m_line		= sym.m_line;
			_symbol.m_file		= m_symbolStrings[sym.m_stringsIndex].m_file;
			_symbol.m_name		= m_symbolStrings[sym.m_stringsIndex].m_name;
			return true;
		}
		return false;
	}

	size_t sidx = 0;
	size_t eidx = len - 1;

	while (eidx > sidx)
	{
		size_t midx = (sidx + eidx) / 2;
		SymbolData sym = m_symbols[midx];

		if (sym.m_offset < (int64_t)_address)
			sidx = midx;
		else
			eidx = midx;

		if (eidx-sidx == 1)
		{
			sym = m_symbols[sidx];

			if (uint64_t(_address - sym.m_offset) >= sym.m_size)
			{
				sym = m_symbols[eidx];
				if (uint64_t(_address - sym.m_offset) >= sym.m_size)
					return false;
			}

			_symbol.m_offset	= sym.m_offset;
			_symbol.m_size		= sym.m_size;
			_symbol.m_line		= sym.m_line;
			_symbol.m_file		= m_symbolStrings[sym.m_stringsIndex].m_file;
			_symbol.m_name		= m_symbolStrings[sym.m_stringsIndex].m_name;
			return true;
		}
	}
	return false;
}

static inline bool sortSymbols(const SymbolMap::SymbolData& _s1, const SymbolMap::SymbolData& _s2)
{
	return _s1.m_offset < _s2.m_offset;
}

static inline bool isInvalid(const SymbolMap::SymbolData& _sym)
{
	return _sym.m_size == 0;
}

void SymbolMap::sort()
{
	if (!m_symbols.size())
		return;

	std::sort(m_symbols.begin(), m_symbols.end(), sortSymbols);
	std::vector<SymbolData>::iterator it	= m_symbols.begin();
	std::vector<SymbolData>::iterator end	= m_symbols.end();

	while (it != end)
	{
		SymbolData& sym = *it;
		if (sym.m_size == 0)
		{
			std::vector<SymbolData>::iterator next = it + 1;
			if (next != end)
			{
				SymbolData& nextSym = *(it + 1);
				sym.m_size = nextSym.m_offset - sym.m_offset;
			}
			else
			{
				sym.m_size = 1; // if last symbol has zero size, set it to 1 to prevent it from being removed
			}
		}
		++it;
	}

	auto itInvalid = std::remove_if(m_symbols.begin(), m_symbols.end(), isInvalid);
	m_symbols.erase(itInvalid, m_symbols.end());
}

} // namespace rdebug
