//--------------------------------------------------------------------------//
/// Copyright (c) 2023 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_RDEBUG_PDB_H
#define RTM_RDEBUG_PDB_H

#include <rbase/inc/platform.h>
#include <unordered_map>

#if RTM_PLATFORM_WINDOWS

#include <rdebug/inc/rdebug.h>
#include <rdebug/src/symbols_map.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <comdef.h>
#include <DIA/include/dia2.h>

struct FunctionSymbol
{
	std::string name;
	uint32_t rva;
	uint32_t size;
	const PDB::CodeView::DBI::Record* frameProc;
};

struct Section
{
	uint16_t index;
	uint32_t offset;
	size_t   lineIndex;
};

struct Filename
{
	uint32_t fileChecksumOffset;
	uint32_t namesFilenameOffset;
	PDB::CodeView::DBI::ChecksumKind checksumKind;
	uint8_t  checksumSize;
	uint8_t  checksum[32];
};

struct Line
{
	uint32_t lineNumber;
	uint32_t codeSize;
	uint32_t rva;
	size_t   filenameIndex;
};

class PDBFile
{
	private:
		rtm_string									m_sFileName;
		bool										m_isStripped;
		MemoryMappedFile::Handle					m_pdbFile;
		PDB::DBIStream								m_dbiStream;
		PDB::TPIStream								m_tpiStream;
		std::vector<FunctionSymbol>					m_functionSymbols;
		//std::vector<LineSymbol>						m_lineSymbols;

		std::vector<Section>						m_sections;
		std::vector<Filename>						m_filenames;
		std::vector<Line>							m_lines;
		std::unordered_map<uint32_t, std::string>	m_filenames_map;

	public:
		PDBFile();
		~PDBFile();

		bool		load(const char* _filename);
		void		getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame);
		uint64_t	getSymbolID(uint64_t _address);
		void		close();
};

#endif // RTM_PLATFORM_WINDOWS

#endif // RTM_RDEBUG_PDB_H
