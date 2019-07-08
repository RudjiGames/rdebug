//--------------------------------------------------------------------------//
/// Copyright (c) 2019 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>

namespace rdebug {

inline static bool charIsDigit(char _c)
{
	return ((_c >= '0') && (_c <= '9'));
}

inline static bool charIsEOL(char _c)
{
	return ((_c == '\r') || (_c == '\n'));
}

inline static bool charIsBlank(char _c)
{
	return ((_c == ' ') || (_c == '\t'));
}

inline static bool charIsTab(char _c)
{
	return (_c == '\t');
}

inline static bool charIsSpace(char _c)
{
	return (_c == ' ');
}

void parseAddr2LineSymbolInfo(const char* _str, StackFrame& _frame)
{
	size_t len = rtm::strLen(_str);

	size_t l = len;
	int idx = 0;
	while (--l)
	{
		// ugly but we own the data
		if (_str[l] == '\r') { const_cast<char*>(_str)[l] = '\0'; idx++; }
		if (_str[l] == '\n') const_cast<char*>(_str)[l] = '\0';

		if (idx == 2) break;
	}

	if (rtm::strCmp(_str,"??") != 0)
		rtm::strlCpy(_frame.m_func, sizeof(_frame.m_func), _str);

	const char* ptr = &_str[l+2];
	if (rtm::strCmp(ptr,"??:0") != 0)
	{
		rtm::strlCpy(_frame.m_file, sizeof(_frame.m_file), ptr);
		len = rtm::strLen(_frame.m_file);
		while (_frame.m_file[--len] != ':');
		_frame.m_file[len] = '\0';
		_frame.m_line = atoi(&_frame.m_file[len+1]);
	}
}

void parsePlayStationSymbolInfo(const char* _str, StackFrame& _frame)
{
	const char* add = "Address:       ";
	const char* dir = "Directory:     ";
	const char* fil = "File Name:     ";
	const char* lin = "Line Number:   ";
	const char* sym = "Symbol:        ";

	const char* address		= rtm::strStr(_str, add);
	const char* directory	= rtm::strStr(_str, dir);
	const char* file		= rtm::strStr(_str, fil);
	const char* line		= rtm::strStr(_str, lin);
	const char* symbol		= rtm::strStr(_str, sym);

	if (address && directory && file && line && symbol)
	{
		size_t len = rtm::strLen(_str);
		while (--len)
		{
			// ugly but we own the data
			if (_str[len] == '\r') const_cast<char*>(_str)[len] = '\0';
			if (_str[len] == '\n') const_cast<char*>(_str)[len] = '\0';
		}

		size_t offset = rtm::strLen(add);
		if ((rtm::strCmp(&directory[offset], "??") != 0) && (rtm::strCmp(&file[offset], "??") != 0))
		{
			rtm::strlCpy(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), &directory[offset]);
			rtm::strlCat(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), "/");
			rtm::strlCat(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), &file[offset]);
		}
		rtm::strlCpy(_frame.m_func, RTM_NUM_ELEMENTS(_frame.m_func), &symbol[offset]);
		_frame.m_line = atoi(&line[offset]);
	}
}

void parseFile(rtm_string& _dst, uint32_t& _line, const char*& _buffer)
{
	while (charIsBlank(*_buffer) && !charIsEOL(*_buffer)) ++_buffer;
	while (!charIsEOL(*_buffer))
	{
		if (*_buffer == ':')
		{
			if (charIsDigit(_buffer[1]))
			{
				_line = atoi(&_buffer[1]);
				break;
			}
		}

		_dst += *_buffer;
		++_buffer;
	}
}

void parseSym(rtm_string& _dst, const char*& _buffer)
{
	while (charIsBlank(*_buffer) && !charIsEOL(*_buffer)) ++_buffer;
	while (!charIsTab(*_buffer) && !charIsEOL(*_buffer))
	{
		_dst += *_buffer;
		++_buffer;
	}
}

bool isHex(char _c)
{
	if ((_c >= '0') && (_c <= '9'))
		return true;
	else
		if ((_c >= 'a') && (_c <= 'z'))
			return true;
		else
			if ((_c >= 'A') && (_c <= 'Z'))
				return true;
	return false;
}

uint64_t fromHex(char _c, bool& _stop)
{
	if ((_c >= '0') && (_c <= '9'))
		return (_c - '0');
	else
		if ((_c >= 'a') && (_c <= 'z'))
			return 10 + (_c - 'a');
		else
			if ((_c >= 'A') && (_c <= 'Z'))
				return 10 + (_c - 'A');
	_stop = true;
	return 0;
}

bool parseHex(uint64_t& _offset, const char*& _buffer)
{
	const char* buffer = _buffer;
	if (!buffer)
	{
		_offset = 0;
		return true;
	}

	while (charIsSpace(*buffer)) ++buffer;

	uint64_t offset = 0;
	if ((buffer[0] == '0') && (buffer[1] == 'x'))
		buffer = &buffer[2];
	
	if (!isHex(*buffer))
		return false;

	int cnt = 0;
	do
	{
		if (charIsSpace(*buffer))
			break;

		bool stop = false;
		offset = (offset << 4) | fromHex(*buffer, stop);
		++buffer;
		if (stop)
			return false;
		++cnt;
	}
	while (*buffer && isHex(*buffer) && !charIsSpace(*buffer));

	if ((cnt != 8) && (cnt != 16))
		return false;

	_offset = offset;
	_buffer = ++buffer;
	return true;
}

void parseSymbolMapLineGNU(const char* _line, SymbolMap& _symMap)
{
	// offset  [size] t/T symbol file:line
	Symbol sym;

	uint64_t offset = 0;
	bool charIsDigit = parseHex(offset, _line);
	if (!charIsDigit)
		return;

	uint64_t size = 0;
	parseHex(size, _line);

	char type = _line[0];
	++_line;

	if ((type != 't') && (type != 'T'))
		return;

	parseSym(sym.m_name, _line);
	sym.m_offset	= offset;
	sym.m_size		= size;
	
	parseFile(sym.m_file, sym.m_line, _line);

	_symMap.addSymbol(sym);
}

void parseSymbolMapLinePS3SNC(const char* _line, SymbolMap& _symMap)
{
	// offset  scope Function segment symbol
	Symbol sym;

	uint64_t offset = 0;
	bool charIsDigit = parseHex(offset, _line);
	if (!charIsDigit)
		return;

	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;

	if (rtm::strCmp(_line, "Function", rtm::strLen("Function")) != 0)
		return;

	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;

	parseSym(sym.m_name, _line);
	sym.m_offset = (uint32_t)offset;

	_symMap.addSymbol(sym);
}

void parseSymbolMapGNU(const char* _buffer, SymbolMap& _symMap)
{
	size_t len = rtm::strLen(_buffer);
	size_t pos = 0;

	while (pos < len)
	{
		const char* line = &_buffer[pos];
		
		parseSymbolMapLineGNU(line, _symMap);
		
		while (!charIsEOL(_buffer[pos]))
		   ++pos;

		while (charIsEOL(_buffer[pos]))
			++pos;
	}

	_symMap.sort();
}

void parseSymbolMapPS3(const char* _buffer, SymbolMap& _symMap)
{
	size_t len = rtm::strLen(_buffer);
	size_t pos = 0;

	while (pos < len)
	{
		const char* line = &_buffer[pos];
		
		parseSymbolMapLinePS3SNC(line, _symMap);
		
		while (!charIsEOL(_buffer[pos]))
		   ++pos;

		while (charIsEOL(_buffer[pos]))
			++pos;
	}

	_symMap.sort();
}

} // namespace rdebug
