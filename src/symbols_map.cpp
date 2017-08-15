//--------------------------------------------------------------------------//
/// Copyright (c) 2010-2017 Milos Tosic. All Rights Reserved.              ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/symbols_map.h>

#include <algorithm>

namespace rdebug {

Symbol::Symbol() :
	m_size(0),
	m_offset(0),
	m_line(0)
{
}

void SymbolMap::addSymbol(const Symbol& _sym)
{
	if (!m_symbols.empty())
	{
		Symbol& s = m_symbols[m_symbols.size()-1];
		if (s.m_offset == _sym.m_offset)
		{
			m_symbols[m_symbols.size()-1] = _sym;
			return;
		}
	}

	m_symbols.push_back(_sym);
}

Symbol* SymbolMap::findSymbol(uint64_t _address)
{
	size_t len = m_symbols.size();
	if (!len)
		return 0;

	size_t sidx = 0;
	size_t eidx = len - 1;

	while (eidx > sidx)
	{
		size_t midx = (sidx + eidx) / 2;
		Symbol& sym = m_symbols[midx];

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
					return 0;
			}

			return &sym;
		}
	}
	return 0;
}

static inline bool sortSymbols(const Symbol& _s1, const Symbol& _s2)
{
	return _s1.m_offset < _s2.m_offset;
}

static inline bool isInvalid(const Symbol& _sym)
{
	return _sym.m_size == 0;
}

void SymbolMap::sort()
{
	if (!m_symbols.size())
		return;

	std::sort(m_symbols.begin(), m_symbols.end(), sortSymbols);
	std::vector<Symbol>::iterator it	= m_symbols.begin();
	std::vector<Symbol>::iterator end	= m_symbols.end() - 1;

	while (it != end)
	{
		Symbol& sym = *it;
		if (sym.m_size == 0)
		{
			Symbol& nextSym = *(it+1);
			sym.m_size = nextSym.m_offset - 1;
		}
		++it;
	}

	std::remove_if(m_symbols.begin(), m_symbols.end(), isInvalid);
}

} // namespace rdebug
