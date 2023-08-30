//--------------------------------------------------------------------------//
/// Copyright (c) 2023 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <execution>

#if RTM_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if RTM_COMPILER_MSVC
#pragma warning (disable: 4091) // 'typedef ': ignored on left of '' when no variable is declared
#include <DbgHelp.h>
#include <DIA/include/diacreate.h>
#include <DIA/include/dia2.h>
#pragma warning (default: 4091)
#else
typedef uint16_t  __wchar_t;
#include <guiddef.h>
const GUID CLSID_DiaSource			= { 0X2735412E, 0X7F64, 0X5B0F, 0X8F, 0X00, 0X5D, 0X77, 0XAF, 0XBE, 0X26, 0X1E };
const GUID IID_IDiaDataSource		= { 0X79F1BB5F, 0XB66E, 0X48E5, 0XB6, 0XA9, 0X15, 0X45, 0XC3, 0X23, 0XCA, 0X3D };
const GUID IID_IDiaLoadCallback2	= {	0x4688A074, 0x5A4D, 0x4486, 0xAE, 0xA8, 0x7B, 0x90, 0x71, 0x1D, 0x9F, 0x7C };
const GUID IID_IDiaLoadCallback		= { 0xC32ADB82, 0x73F4, 0x421B, 0x95, 0xD5, 0xA4, 0x70, 0x6E, 0xDF, 0x5D, 0xBE };
#endif // RTM_COMPILER_MSVC


#ifndef UNDNAME_COMPLETE
#define UNDNAME_COMPLETE                 (0x0000)  // Enable full undecoration
#define UNDNAME_NO_LEADING_UNDERSCORES   (0x0001)  // Remove leading underscores from MS extended keywords
#define UNDNAME_NO_MS_KEYWORDS           (0x0002)  // Disable expansion of MS extended keywords
#define UNDNAME_NO_FUNCTION_RETURNS      (0x0004)  // Disable expansion of return type for primary declaration
#define UNDNAME_NO_ALLOCATION_MODEL      (0x0008)  // Disable expansion of the declaration model
#define UNDNAME_NO_ALLOCATION_LANGUAGE   (0x0010)  // Disable expansion of the declaration language specifier
#define UNDNAME_NO_MS_THISTYPE           (0x0020)  // NYI Disable expansion of MS keywords on the 'this' type for primary declaration
#define UNDNAME_NO_CV_THISTYPE           (0x0040)  // NYI Disable expansion of CV modifiers on the 'this' type for primary declaration
#define UNDNAME_NO_THISTYPE              (0x0060)  // Disable all modifiers on the 'this' type
#define UNDNAME_NO_ACCESS_SPECIFIERS     (0x0080)  // Disable expansion of access specifiers for members
#define UNDNAME_NO_THROW_SIGNATURES      (0x0100)  // Disable expansion of 'throw-signatures' for functions and pointers to functions
#define UNDNAME_NO_MEMBER_TYPE           (0x0200)  // Disable expansion of 'static' or 'virtual'ness of members
#define UNDNAME_NO_RETURN_UDT_MODEL      (0x0400)  // Disable expansion of MS model for UDT returns
#define UNDNAME_32_BIT_DECODE            (0x0800)  // Undecorate 32-bit decorated names
#define UNDNAME_NAME_ONLY                (0x1000)  // Crack only the name for primary declaration;

#define UNDNAME_NO_ARGUMENTS             (0x2000)  // Don't undecorate arguments to function
#define UNDNAME_NO_SPECIAL_SYMS          (0x4000)  // Don't undecorate special names (v-table, vcall, vector xxx, metatype, etc)
#endif

#define UND_CODE (					\
	UNDNAME_NO_THROW_SIGNATURES		| \
	UNDNAME_NO_SPECIAL_SYMS			| \
	UNDNAME_NO_MEMBER_TYPE			| \
	UNDNAME_COMPLETE				| \
	UNDNAME_NO_LEADING_UNDERSCORES	| \
	UNDNAME_NO_THISTYPE				| \
	UNDNAME_NO_ACCESS_SPECIFIERS	| \
	UNDNAME_NO_ALLOCATION_LANGUAGE	| \
	UNDNAME_NO_ALLOCATION_MODEL		| \
	UNDNAME_32_BIT_DECODE			| \
	0)


inline bool operator < (const FunctionSymbol& _s1, const FunctionSymbol& _s2)
{
	return _s1.rva < _s2.rva;
}

inline bool operator < (const Line& _s1, const Line& _s2)
{
	return _s1.rva < _s2.rva;
}


static bool isError(PDB::ErrorCode _errorCode)
{
	return _errorCode != PDB::ErrorCode::Success;
}

static bool hasValidDBIStreams(const PDB::RawFile& rawPdbFile, const PDB::DBIStream& dbiStream)
{
	// check whether the DBI stream offers all sub-streams we need
	if (isError(dbiStream.HasValidImageSectionStream(rawPdbFile)))
	{
		return false;
	}
		
	if (isError(dbiStream.HasValidPublicSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (isError(dbiStream.HasValidGlobalSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (isError(dbiStream.HasValidSectionContributionStream(rawPdbFile)))
	{
		return false;
	}

	return true;
}

namespace rdebug {

class DiaLoadCallBack : public IDiaLoadCallback2
{
	private:
		uint32_t	m_refCount;
		wchar_t*	m_buffer;

	public:
		DiaLoadCallBack(wchar_t inBuffer[1024]) : m_refCount(0), m_buffer(inBuffer) {}
		virtual ~DiaLoadCallBack() {}

    //	IUnknown
	ULONG	STDMETHODCALLTYPE AddRef()
	{
		m_refCount++;
		return m_refCount;
	}
	ULONG	STDMETHODCALLTYPE Release()
	{
		if (--m_refCount == 0)
			delete this; 
		return m_refCount;
	}
    HRESULT	STDMETHODCALLTYPE QueryInterface( REFIID rid, void **ppUnk )
	{
		if (ppUnk == 0)
			return E_INVALIDARG;

		if		(rid == IID_IDiaLoadCallback2)	*ppUnk = (IDiaLoadCallback2*)this;
		else if (rid == IID_IDiaLoadCallback)	*ppUnk = (IDiaLoadCallback*)this;
		else if (rid == IID_IUnknown)			*ppUnk = (IUnknown*)this;
		else									*ppUnk = 0;

		if (*ppUnk != 0) 
		{
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	//	Rest
	HRESULT STDMETHODCALLTYPE NotifyOpenPDB(LPCOLESTR _pdbPath, HRESULT _resultCode)
	{
		if (_resultCode == S_OK)
			wcscpy(m_buffer, _pdbPath);
#if RTM_DEBUG
		else
		{
			OutputDebugStringW(L"Try open \"");
			OutputDebugStringW(_pdbPath);
			OutputDebugStringW(L"\" failed.\n");
		}
#endif
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE NotifyOpenDBG(LPCOLESTR, HRESULT) { return S_OK; }
	HRESULT STDMETHODCALLTYPE NotifyDebugDir(BOOL, DWORD, BYTE[]) { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictRegistryAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictSymbolServerAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictOriginalPathAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictReferencePathAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictDBGAccess() { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictSystemRootAccess() { return S_OK; }
};

HRESULT createDiaDataSource(void** _ptr)
{
	HRESULT hr = ::CoCreateInstance(CLSID_DiaSource, 0, CLSCTX_INPROC_SERVER, IID_IDiaDataSource, _ptr);

#if RTM_COMPILER_MSVC
	if(FAILED(hr))	hr = NoRegCoCreate(L"msdia140.dll", __uuidof(DiaSource), __uuidof(IDiaDataSource), _ptr);
#endif // RTM_COMPILER_MSVC
	return hr;
}

bool findSymbol(const char* _path, char _outSymbolPath[2048], const char* _symbolStore)
{
	IDiaDataSource* pIDiaDataSource = NULL;

	HRESULT hr = createDiaDataSource((void**)&pIDiaDataSource);
	
	if(FAILED(hr))
		return false;

	// The local file path must use '\\', and http(s) url must use '/'
	// Do not change the the slashes only if you detect each part in the _symbolStore.
	rtm::MultiToWide symbolStore(_symbolStore, false);

	wchar_t symStoreBuffer[4096];
	wcscpy(symStoreBuffer, L"");

	// The file path is not needed in the search path, loadDataForExe will find from src path automatically.
	// The semicolon is necessary between each path (or srv*).
	char moduleNameM[512];
	const char* srcPath = _path;
	if (!srcPath || (rtm::strLen(srcPath) == 0))
	{
		wchar_t moduleName[512];
		GetModuleFileNameW(NULL, moduleName, sizeof(wchar_t)*512);
		rtm::strlCpy(moduleNameM, RTM_NUM_ELEMENTS(moduleName), rtm::WideToMulti(moduleName));
		srcPath = moduleNameM;
	}

	if (symbolStore.size() > 1) // not ("" or null)
	{
		wcscat(symStoreBuffer, symbolStore);
	}

	{
		size_t len = wcslen(symStoreBuffer);
		GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", (LPWSTR)&symStoreBuffer[len], sizeof(symStoreBuffer));
	}

	wchar_t outSymbolPath[2048];
	DiaLoadCallBack callback(outSymbolPath);
	callback.AddRef();

	hr = pIDiaDataSource->loadDataForExe((LPOLESTR)rtm::MultiToWide(srcPath), (LPOLESTR)symStoreBuffer, &callback);

	if (FAILED(hr))
	{
		pIDiaDataSource->Release();

		// Hacky desperate attempt to find PDB file where EXE resides
		const char* exe = rtm::strStr(srcPath, ".exe");
		if (exe)
		{
			char pdb[2048];
			rtm::strlCpy(pdb, RTM_NUM_ELEMENTS(pdb), srcPath);
			rtm::strlCpy(&pdb[exe-srcPath], RTM_NUM_ELEMENTS(pdb) - uint32_t(exe-srcPath), ".pdb");
			if (INVALID_FILE_ATTRIBUTES != GetFileAttributesA(pdb))
			{
				rtm::strlCpy(_outSymbolPath, 2048, pdb);
				return true;
			}
		}

		return false;
	}

	rtm::WideToMulti result(outSymbolPath);
	rtm::strlCpy(_outSymbolPath, 2048, result);
	
	pIDiaDataSource->Release();
	return true;
}

} // namespace rdebug

const char*	s_PDB_File_Extension = "pdb";

PDBFile::PDBFile()
{
	memset(&m_pdbFile, 0, sizeof(m_pdbFile));
	m_isStripped = false;
}

PDBFile::~PDBFile()
{
	close();
}

void PDBFile::close()
{
	m_functionSymbols.clear();
	//m_lineSymbols.clear();

	if (m_pdbFile.baseAddress)
		MemoryMappedFile::Close(m_pdbFile);
}

bool PDBFile::load(const char* _filename)
{
	if (!_filename) return false;
	if (rtm::strLen(_filename) == 0) return false;

	m_pdbFile = MemoryMappedFile::Open(_filename);
	if (!m_pdbFile.baseAddress)
		return false;

	if (isError(PDB::ValidateFile(m_pdbFile.baseAddress)))
	{
		MemoryMappedFile::Close(m_pdbFile);
		return false;
	}

	const PDB::RawFile rawPdbFile = PDB::CreateRawFile(m_pdbFile.baseAddress);
	if (isError(PDB::HasValidDBIStream(rawPdbFile)))
	{
		MemoryMappedFile::Close(m_pdbFile);
		return false;
	}

	const PDB::InfoStream infoStream(rawPdbFile);
	if (infoStream.UsesDebugFastLink())
	{
		MemoryMappedFile::Close(m_pdbFile);
		return false;
	}

	//const auto h = infoStream.GetHeader();
	//printf("Version %u, signature %u, age %u, GUID %08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x\n",
	//	static_cast<uint32_t>(h->version), h->signature, h->age,
	//	h->guid.Data1, h->guid.Data2, h->guid.Data3,
	//	h->guid.Data4[0], h->guid.Data4[1], h->guid.Data4[2], h->guid.Data4[3], h->guid.Data4[4], h->guid.Data4[5], h->guid.Data4[6], h->guid.Data4[7]);

	m_dbiStream = PDB::CreateDBIStream(rawPdbFile);
	if (!hasValidDBIStreams(rawPdbFile, m_dbiStream))
	{
		MemoryMappedFile::Close(m_pdbFile);
		return false;
	}

	m_tpiStream = PDB::CreateTPIStream(rawPdbFile);
	if (PDB::HasValidTPIStream(rawPdbFile) != PDB::ErrorCode::Success)
	{
		MemoryMappedFile::Close(m_pdbFile);
		return false;
	}

	const PDB::ImageSectionStream imageSectionStream = m_dbiStream.CreateImageSectionStream(rawPdbFile);
	const PDB::ModuleInfoStream	  moduleInfoStream   = m_dbiStream.CreateModuleInfoStream(rawPdbFile);
	const PDB::CoalescedMSFStream symbolRecordStream = m_dbiStream.CreateSymbolRecordStream(rawPdbFile);

	std::vector<Section> sections;
	std::vector<Filename> filenames;
	std::vector<Line> lines;

	std::unordered_set<uint32_t> seenFunctionRVAs;

	const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();

// ---------------------------
// Collect function symbols
// ---------------------------
	for (const PDB::ModuleInfoStream::Module& module : modules)
	{
		if (module.HasSymbolStream())
		{
			const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawPdbFile);

			moduleSymbolStream.ForEachSymbol([this, &seenFunctionRVAs, &imageSectionStream](const PDB::CodeView::DBI::Record* record)
			{
				// only grab function symbols from the module streams
				const char* name = nullptr;
				uint32_t rva = 0u;
				uint32_t size = 0u;

				if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_FRAMEPROC)
				{
					m_functionSymbols[m_functionSymbols.size() - 1].frameProc = record;
					return;
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_THUNK32)
				{
					if (record->data.S_THUNK32.thunk == PDB::CodeView::DBI::ThunkOrdinal::TrampolineIncremental)
					{
						// we have never seen incremental linking thunks stored inside a S_THUNK32 symbol, but better safe than sorry
						name = "ILT";
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_THUNK32.section, record->data.S_THUNK32.offset);
						size = 5u;
					}
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_TRAMPOLINE)
				{
					// incremental linking thunks are stored in the linker module
					name = "ILT";
					rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_TRAMPOLINE.thunkSection, record->data.S_TRAMPOLINE.thunkOffset);
					size = 5u;
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32)
				{
					name = record->data.S_LPROC32.name;
					rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section, record->data.S_LPROC32.offset);
					size = record->data.S_LPROC32.codeSize;
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32)
				{
					name = record->data.S_GPROC32.name;
					rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section, record->data.S_GPROC32.offset);
					size = record->data.S_GPROC32.codeSize;
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32_ID)
				{
					name = record->data.S_LPROC32_ID.name;
					rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section, record->data.S_LPROC32_ID.offset);
					size = record->data.S_LPROC32_ID.codeSize;
				}
				else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32_ID)
				{
					name = record->data.S_GPROC32_ID.name;
					rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section, record->data.S_GPROC32_ID.offset);
					size = record->data.S_GPROC32_ID.codeSize;
				}

				if (rva == 0u)
					return;

				m_functionSymbols.push_back(FunctionSymbol{ name, rva, size, nullptr });
				seenFunctionRVAs.emplace(rva);
			});
		}
	}

// ---------------------------
// Get public function symbols
// ---------------------------

	const PDB::PublicSymbolStream publicSymbolStream = m_dbiStream.CreatePublicSymbolStream(rawPdbFile);
	{
		const PDB::ArrayView<PDB::HashRecord> hashRecords = publicSymbolStream.GetRecords();

		for (const PDB::HashRecord& hashRecord : hashRecords)
		{
			const PDB::CodeView::DBI::Record* record = publicSymbolStream.GetRecord(symbolRecordStream, hashRecord);
			if ((PDB_AS_UNDERLYING(record->data.S_PUB32.flags) & PDB_AS_UNDERLYING(PDB::CodeView::DBI::PublicSymbolFlags::Function)) == 0u) // ignore everything that is not a function
				continue;

			const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_PUB32.section, record->data.S_PUB32.offset);
			if (rva == 0u) // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
				continue;

			// check whether we already know this symbol from one of the module streams
			const auto it = seenFunctionRVAs.find(rva);
			if (it != seenFunctionRVAs.end()) // we know this symbol already, ignore it
				continue;

			// this is a new function symbol, so store it.
			// note that we don't know its size yet.
			m_functionSymbols.push_back(FunctionSymbol { record->data.S_PUB32.name, rva, 0u, nullptr });
		}
	}

	seenFunctionRVAs.clear();

// ---------------------------
// Get public function symbols
// ---------------------------

	const size_t symbolCount = m_functionSymbols.size();
	if (symbolCount != 0u)
	{
		size_t foundCount = 0u;

		// we have at least 1 symbol.
		// compute missing symbol sizes by computing the distance from this symbol to the next.
		// note that this includes "int 3" padding after the end of a function. if you don't want that, but the actual number of bytes of
		// the function's code, your best bet is to use a disassembler instead.
		for (size_t i = 0u; i < symbolCount - 1u; ++i)
		{
			FunctionSymbol& currentSymbol = m_functionSymbols[i];
			if (currentSymbol.size != 0u)
			{
				// the symbol's size is already known
				continue;
			}

			const FunctionSymbol& nextSymbol = m_functionSymbols[i + 1u];
			const size_t size = nextSymbol.rva - currentSymbol.rva;
			(void)size; // unused
			++foundCount;
		}

		// we know have the sizes of all symbols, except the last.
		// this can be found by going through the contributions, if needed.
		FunctionSymbol& lastSymbol = m_functionSymbols[symbolCount - 1u];
		if (lastSymbol.size != 0u)
		{
			// bad luck, we can't deduce the last symbol's size, so have to consult the contributions instead.
			// we do a linear search in this case to keep the code simple.
			const PDB::SectionContributionStream sectionContributionStream = m_dbiStream.CreateSectionContributionStream(rawPdbFile);
			const PDB::ArrayView<PDB::DBI::SectionContribution> sectionContributions = sectionContributionStream.GetContributions();
			for (const PDB::DBI::SectionContribution& contribution : sectionContributions)
			{
				const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(contribution.section, contribution.offset);
				if (rva == 0u)
				{
					//printf("Contribution has invalid RVA\n");
					continue;
				}

				if (rva == lastSymbol.rva)
				{
					lastSymbol.size = contribution.size;
					break;
				}
				
				if (rva > lastSymbol.rva)
				{
					// should have found the contribution by now
					//printf("Unknown contribution for symbol %s at RVA 0x%X", lastSymbol.name.c_str(), lastSymbol.rva);
					break;
				}
			}
		}
	}


// ---------------------------
// Get line symbols
// ---------------------------
	std::unordered_map<uint32_t, std::string> filenames_map;

	for (const PDB::ModuleInfoStream::Module& module : modules)
	{
		if (infoStream.HasNamesStream())
		{
			if (!module.HasLineStream())
				continue;

			const PDB::NamesStream namesStream				= infoStream.CreateNamesStream(rawPdbFile);
			const PDB::ModuleLineStream moduleLineStream	= module.CreateLineStream(rawPdbFile);

			const size_t moduleFilenamesStartIndex = filenames.size();
			const PDB::CodeView::DBI::FileChecksumHeader* moduleFileChecksumHeader = nullptr;

			moduleLineStream.ForEachSection([&moduleLineStream, &namesStream, &moduleFileChecksumHeader, &sections, &filenames, &lines, &imageSectionStream, &filenames_map](const PDB::CodeView::DBI::LineSection* lineSection)
			{
				if (lineSection->header.kind == PDB::CodeView::DBI::DebugSubsectionKind::S_LINES)
				{
					moduleLineStream.ForEachLinesBlock(lineSection,
						[&lineSection, &sections, &filenames, &lines, &imageSectionStream](const PDB::CodeView::DBI::LinesFileBlockHeader* linesBlockHeader, const PDB::CodeView::DBI::Line* blocklines, const PDB::CodeView::DBI::Column* /*blockColumns*/)
						{
							const PDB::CodeView::DBI::Line& firstLine = blocklines[0];

							const uint16_t sectionIndex = lineSection->linesHeader.sectionIndex;
							const uint32_t sectionOffset = lineSection->linesHeader.sectionOffset;
							const uint32_t fileChecksumOffset = linesBlockHeader->fileChecksumOffset;

							const size_t filenameIndex = filenames.size();

							// there will be duplicate filenames for any real world pdb. 
							// ideally the filenames would be stored in a map with the filename or checksum as the key.
							// but that would complicate the logic in this example and therefore just use a vector to make it easier to understand.
							filenames.push_back({ fileChecksumOffset, 0, PDB::CodeView::DBI::ChecksumKind::None, 0, {0} });

							sections.push_back({ sectionIndex, sectionOffset, lines.size() });

							// initially set code size of first line to 0, will be updated in loop below.
							uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(sectionIndex, sectionOffset + blocklines[0].offset);
							lines.push_back({ firstLine.linenumStart, 0, rva, filenameIndex });						 							

							uint32_t prevLineNum = firstLine.linenumStart;
							for (uint32_t i = 1, size = linesBlockHeader->numLines; i < size; ++i)
							{
								const PDB::CodeView::DBI::Line& line = blocklines[i];

								// calculate code size of previous line by using the current line offset.
								lines.back().codeSize = line.offset - blocklines[i - 1].offset;

								uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(sectionIndex, sectionOffset + line.offset);
								sections.push_back({ sectionIndex, sectionOffset + line.offset, lines.size() });
								if (line.linenumStart > 1 << 23) {
									lines.push_back({prevLineNum, 0, rva, filenameIndex });
								}
								else {
									lines.push_back({line.linenumStart, 0, rva, filenameIndex });
									prevLineNum = line.linenumStart;
								}
							}

							// calc code size of last line
							lines.back().codeSize = lineSection->linesHeader.codeSize - blocklines[linesBlockHeader->numLines - 1].offset;
						});
				}
				else if (lineSection->header.kind == PDB::CodeView::DBI::DebugSubsectionKind::S_FILECHECKSUMS)
				{
					// how to read checksums and their filenames from the Names Stream
					moduleLineStream.ForEachFileChecksum(lineSection, [&namesStream, &filenames_map](const PDB::CodeView::DBI::FileChecksumHeader* fileChecksumHeader)
						{
							const char* filename = namesStream.GetFilename(fileChecksumHeader->filenameOffset);
							filenames_map[fileChecksumHeader->filenameOffset] = filename;
							(void)filename;
						});

					// store the checksum header for the module, as there might be more lines after the checksums.
					// so lines will get their checksum header values assigned after processing all line sections in the module.
					PDB_ASSERT(moduleFileChecksumHeader == nullptr, "Module File Checksum Header already set");
					moduleFileChecksumHeader = &lineSection->checksumHeader;
				}
				else if (lineSection->header.kind == PDB::CodeView::DBI::DebugSubsectionKind::S_INLINEELINES)
				{
					if (lineSection->inlineeHeader.kind == PDB::CodeView::DBI::InlineeSourceLineKind::Signature)
					{
						//moduleLineStream.ForEachInlineeSourceLine(lineSection, [&namesStream, &filenames_map](const PDB::CodeView::DBI::InlineeSourceLine* inlineeSourceLine)
						//	{
						//		const char* filename = namesStream.GetFilename(inlineeSourceLine->fileChecksumOffset);
						//		if (filename)
						//			filenames_map[inlineeSourceLine->fileChecksumOffset] = filename;
						//	});
					}
					else
					{
						moduleLineStream.ForEachInlineeSourceLineEx(lineSection, [](const PDB::CodeView::DBI::InlineeSourceLineEx* inlineeSourceLineEx)
							{
								for (uint32_t i = 0; i < inlineeSourceLineEx->extraLines; ++i)
								{
									const uint32_t checksumOffset = inlineeSourceLineEx->extrafileChecksumOffsets[i];
									(void)checksumOffset;
								}
							});
					}
				}
				else
				{
					//PDB_ASSERT(false, "Line Section kind 0x%X not handled", static_cast<uint32_t>(lineSection->header.kind));
				}
			});

			// assign checksum values for each filename added in this module
			for (size_t i = moduleFilenamesStartIndex, size = filenames.size(); i < size; ++i)
			{
				Filename& filename = filenames[i];
	
				// look up the filename's checksum header in the module's checksums section
				const PDB::CodeView::DBI::FileChecksumHeader* checksumHeader = PDB::Pointer::Offset<const PDB::CodeView::DBI::FileChecksumHeader*>(moduleFileChecksumHeader, filename.fileChecksumOffset);
	
				PDB_ASSERT(checksumHeader->checksumKind >= PDB::CodeView::DBI::ChecksumKind::None && 
							checksumHeader->checksumKind <= PDB::CodeView::DBI::ChecksumKind::SHA256,
							"Invalid checksum kind %hhu", checksumHeader->checksumKind);
	
				// store checksum values in filname struct
				filename.namesFilenameOffset = checksumHeader->filenameOffset;
				filename.checksumKind = checksumHeader->checksumKind;
				filename.checksumSize = checksumHeader->checksumSize;
				std::memcpy(filename.checksum, checksumHeader->checksum, checksumHeader->checksumSize);
			}
		}

// ---------------------------
// Sort sections
// ---------------------------

		std::sort(std::execution::par_unseq, sections.begin(), sections.end(), [](const Section& lhs, const Section& rhs)
		{
			if (lhs.index == rhs.index)
			{
				return lhs.offset < rhs.offset;
			}

			return lhs.index < rhs.index;
		});

		m_sections		= sections;
		m_filenames		= filenames;
		m_lines			= lines;

		std::sort(std::execution::par_unseq, m_lines.begin(), m_lines.end(), [](const Line& lhs, const Line& rhs)
		{
			return lhs.rva < rhs.rva;
		});

// ---------------------------
// Build line symbol info
// ---------------------------

		if (infoStream.HasNamesStream())
		{
			if (!module.HasLineStream())
				continue;

			const PDB::NamesStream namesStream = infoStream.CreateNamesStream(rawPdbFile);

			const char* prevFilename = nullptr;

			for (const Section& section : m_sections)																	                            
			{
				Line& line = m_lines[section.lineIndex];

				const Filename& lineFilename = m_filenames[line.filenameIndex];
				const char* filename = namesStream.GetFilename(lineFilename.namesFilenameOffset);

				line.rva = imageSectionStream.ConvertSectionOffsetToRVA(section.index, section.offset);
			}
		}
	}

	m_filenames_map = std::move(filenames_map);

	std::sort(std::execution::par_unseq, m_functionSymbols.begin(), m_functionSymbols.end(), [](const FunctionSymbol& lhs, const FunctionSymbol& rhs)
	{
		return lhs.rva < rhs.rva;
	});

	return m_functionSymbols.size() != 0;
}

void PDBFile::getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame)
{
	RTM_ASSERT(_address < 0xffffffff, "");

	// clear struct
	rtm::strlCpy(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), "Unknown");
	rdebug::addressToString(_address, _frame.m_func);
	_frame.m_line = 0;

	rtm::strlCpy(_frame.m_moduleName, 256, m_sFileName.c_str(), (uint32_t)m_sFileName.size());

	// fill funciton name and file
	FunctionSymbol dummyFunc;
	dummyFunc.rva = (uint32_t)_address;
	auto lowFunc = std::lower_bound(m_functionSymbols.begin(), m_functionSymbols.end(), dummyFunc);
	if (lowFunc != m_functionSymbols.end())
	{
		rtm::strlCpy(_frame.m_func, 16384, lowFunc->name.c_str(), (uint32_t)lowFunc->name.size());
    	}

	// Try to find the line
	Line dummyLine;
	dummyLine.rva = (uint32_t)_address;
	auto low = std::lower_bound(m_lines.begin(), m_lines.end(), dummyLine);
	if (low != m_lines.end())
	{
		size_t fileIdx = low->filenameIndex;
		Filename& filename = m_filenames[fileIdx];
		auto it = m_filenames_map.find(filename.namesFilenameOffset);
		if (it != m_filenames_map.end())
		{
			rtm::strlCpy(_frame.m_file, 256, it->second.c_str(), (uint32_t)it->second.size());
		}

		_frame.m_line = low->lineNumber;
	}
}

uint64_t PDBFile::getSymbolID(uint64_t _address)
{
	RTM_ASSERT(_address < 0xffffffff, "");

	FunctionSymbol dummy;
	dummy.rva = (uint32_t)_address;
	auto low = std::lower_bound(m_functionSymbols.begin(), m_functionSymbols.end(), dummy);
	if (low != m_functionSymbols.end())
	{
		return low->rva;
	}

	return 0;
}

#endif // RTM_PLATFORM_WINDOWS
