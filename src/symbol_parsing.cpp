//--------------------------------------------------------------------------//
/// Copyright (c) 2010-2017 Milos Tosic. All Rights Reserved.              ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>

#include <string.h>

namespace rdebug {

const char* getFileName(const char* _path)
{
	size_t len = strlen(_path);
	while ((_path[len] != '/') && (_path[len] != '\\') && (len>0)) --len;
	return &_path[len + 1];
}

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

void parseAddr2LineSymbolInfo(char* _str, StackFrame& _frame)
{
	size_t len = strlen(_str);

	size_t l = len;
	int idx = 0;
	while (--l)
	{
		if (_str[l] == '\r') { _str[l] = '\0'; idx ++; }
		if (_str[l] == '\n') _str[l] = '\0';
		if (idx == 2) break;
	}

	if (strcmp(_str,"??") != 0)
		strcpy(_frame.m_func, _str);

	char* ptr = &_str[l+2];
	if (strcmp(ptr,"??:0") != 0)
	{
		strcpy(_frame.m_file, ptr);
		len = strlen(_frame.m_file);
		while (_frame.m_file[--len] != ':');
		_frame.m_file[len] = '\0';
		_frame.m_line = atoi(&_frame.m_file[len+1]);
	}
}

void parsePlayStationSymbolInfo(char* _str, StackFrame& _frame)
{
	const char* add = "Address:       ";
	const char* dir = "Directory:     ";
	const char* fil = "File Name:     ";
	const char* lin = "Line Number:   ";
	const char* sym = "Symbol:        ";

	char* address = strstr(_str, add);
	char* directory = strstr(_str, dir);
	char* file = strstr(_str, fil);
	char* line = strstr(_str, lin);
	char* symbol = strstr(_str, sym);

	if (address && directory && file && line && symbol)
	{
		size_t len = strlen(_str);
		while (--len)
		{
			if (_str[len] == '\r') _str[len] = '\0';
			if (_str[len] == '\n') _str[len] = '\0';
		}

		size_t offset = strlen(add);
		if ((strcmp(&directory[offset], "??") != 0) && (strcmp(&file[offset], "??") != 0))
		{
			strcpy(_frame.m_file, &directory[offset]);
			strcat(_frame.m_file, "/");
			strcat(_frame.m_file, &file[offset]);
		}
		strcpy(_frame.m_func, &symbol[offset]);
		_frame.m_line = atoi(&line[offset]);
	}
}

void parseFile(std::string& _dst, uint32_t& _line, char*& _buffer)
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

void parseSym(std::string& _dst, char*& _buffer)
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

bool parseHex(uint64_t& _offset, char*& _buffer)
{
	char* buffer = _buffer;
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

void parseSymbolMapLineGNU(char* _line, SymbolMap& _symMap)
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
	sym.m_size			= size;
	
	parseFile(sym.m_file, sym.m_line, _line);

	_symMap.addSymbol(sym);
}

void parseSymbolMapLinePS3SNC(char* _line, SymbolMap& _symMap)
{
	// offset  scope Function segment symbol
	Symbol sym;

	uint64_t offset = 0;
	bool charIsDigit = parseHex(offset, _line);
	if (!charIsDigit)
		return;

	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;

	if (strncmp(_line, "Function", strlen("Function")) != 0)
		return;

	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while (!charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;
	while ( charIsSpace(*_line) && !charIsEOL(*_line)) ++_line;

	parseSym(sym.m_name, _line);
	sym.m_offset = (uint32_t)offset;

	_symMap.addSymbol(sym);
}

void parseSymbolMapGNU(std::string& _buffer, SymbolMap& _symMap)
{
	size_t len = _buffer.size();
	size_t pos = 0;

	while (pos < len)
	{
		char* line = &_buffer[pos];
		
		parseSymbolMapLineGNU(line, _symMap);
		
		while (!charIsEOL(_buffer[pos]))
		   ++pos;

		while (charIsEOL(_buffer[pos]))
			++pos;
	}

	_symMap.sort();
}

void parseSymbolMapPS3(std::string& _buffer, SymbolMap& _symMap)
{
	size_t len = _buffer.size();
	size_t pos = 0;

	while (pos < len)
	{
		char* line = &_buffer[pos];
		
		parseSymbolMapLinePS3SNC(line, _symMap);
		
		while (!charIsEOL(_buffer[pos]))
		   ++pos;

		while (charIsEOL(_buffer[pos]))
			++pos;
	}

	_symMap.sort();
}

} // namespace rdebug
