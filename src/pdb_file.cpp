//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>
#include <rdebug/src/symbols_types.h>

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
const GUID CLSID_DiaSource = { 0X2735412E, 0X7F64, 0X5B0F, 0X8F, 0X00, 0X5D, 0X77, 0XAF, 0XBE, 0X26, 0X1E };
const GUID IID_IDiaDataSource = { 0X79F1BB5F, 0XB66E, 0X48E5, 0XB6, 0XA9, 0X15, 0X45, 0XC3, 0X23, 0XCA, 0X3D };
const GUID IID_IDiaLoadCallback2 = { 0x4688A074, 0x5A4D, 0x4486, 0xAE, 0xA8, 0x7B, 0x90, 0x71, 0x1D, 0x9F, 0x7C };
const GUID IID_IDiaLoadCallback = { 0xC32ADB82, 0x73F4, 0x421B, 0x95, 0xD5, 0xA4, 0x70, 0x6E, 0xDF, 0x5D, 0xBE };
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

namespace rdebug {

	class DiaLoadCallBack : public IDiaLoadCallback2
	{
	private:
		uint32_t	m_refCount;
		wchar_t* m_buffer;

	public:
		DiaLoadCallBack(wchar_t* inBuffer) : m_refCount(0), m_buffer(inBuffer) {}
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
			{
				delete this;
				return 0;
			}
			return m_refCount;
		}
		HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID rid, void** ppUnk)
		{
			if (ppUnk == 0)
				return E_INVALIDARG;

			if (rid == IID_IDiaLoadCallback2)	*ppUnk = (IDiaLoadCallback2*)this;
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
				OutputDebugStringW(L"PDB: Try open \"");
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
		if (FAILED(hr))	hr = NoRegCoCreate(L"msdia140.dll", __uuidof(DiaSource), __uuidof(IDiaDataSource), _ptr);
#endif // RTM_COMPILER_MSVC
		return hr;
	}

	/// Reads a little-endian uint16 from a file at the given offset.
	static inline uint16_t readU16(FILE* _file, uint32_t _pos)
	{
		fseek(_file, (long)_pos, SEEK_SET);
		uint8_t buf[2];
		if (fread(buf, 1, 2, _file) != 2) return 0;
		return (uint16_t)((uint32_t)buf[0] | (uint32_t)buf[1] << 8);
	}

	/// Reads a little-endian uint32 from a file at the given offset.
	static inline uint32_t readU32(FILE* _file, uint32_t _pos)
	{
		fseek(_file, (long)_pos, SEEK_SET);
		uint8_t buf[4];
		if (fread(buf, 1, 4, _file) != 4) return 0;
		return (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
	}

	/// Extracts the PDB file path from a PE executable's CodeView (RSDS) debug directory entry.
	/// Returns true and fills _outPdbPath if a valid RSDS record with a PDB path is found.
	static bool extractPdbPathFromExe(const wchar_t* _exePath, wchar_t* _outPdbPath, size_t _outSize)
	{
		_outPdbPath[0] = L'\0';

		rtm::WideToMulti exePathMulti(_exePath);
		FILE* file = fopen(exePathMulti, "rb");
		if (!file)
			return false;

		// Verify MZ signature
		if (readU16(file, 0) != 0x5A4D)
		{
			fclose(file); return false;
		}

		uint32_t peOffset = readU32(file, 0x3C);
		if (readU32(file, peOffset) != 0x00004550)
		{
			fclose(file); return false;
		}

		uint16_t numSections = readU16(file, peOffset + 6);
		uint16_t sizeOfOptHdr = readU16(file, peOffset + 20);
		uint32_t optHdrOffset = peOffset + 24;
		uint16_t optMagic = readU16(file, optHdrOffset);

		// Data directories begin at different offsets for PE32 vs PE32+
		uint32_t dataDirBase = optHdrOffset + (optMagic == 0x020B ? 112 : 96);

		// IMAGE_DIRECTORY_ENTRY_DEBUG is index 6
		uint32_t debugDirRVA = readU32(file, dataDirBase + 6 * 8);
		uint32_t debugDirSize = readU32(file, dataDirBase + 6 * 8 + 4);

		if (debugDirRVA == 0 || debugDirSize == 0)
		{
			fclose(file); return false;
		}

		// Convert debug directory RVA to file offset via section headers
		uint32_t sectionsBase = peOffset + 24 + sizeOfOptHdr;
		uint32_t debugDirFileOff = 0;
		for (uint16_t s = 0; s < numSections; ++s)
		{
			uint32_t sectBase = sectionsBase + (uint32_t)s * 40;
			uint32_t sectVA = readU32(file, sectBase + 12);
			uint32_t sectRawSize = readU32(file, sectBase + 16);
			uint32_t sectRawOffset = readU32(file, sectBase + 20);

			if (debugDirRVA >= sectVA && debugDirRVA < sectVA + sectRawSize)
			{
				debugDirFileOff = sectRawOffset + (debugDirRVA - sectVA);
				break;
			}
		}

		if (debugDirFileOff == 0)
		{
			fclose(file); return false;
		}

		// Walk IMAGE_DEBUG_DIRECTORY entries (28 bytes each); look for CodeView (Type == 2)
		uint32_t numEntries = debugDirSize / 28;
		for (uint32_t e = 0; e < numEntries; ++e)
		{
			uint32_t entryBase = debugDirFileOff + e * 28;
			uint32_t type = readU32(file, entryBase + 12);

			if (type != 2) // IMAGE_DEBUG_TYPE_CODEVIEW
				continue;

			uint32_t sizeOfData = readU32(file, entryBase + 16);
			uint32_t ptrRawData = readU32(file, entryBase + 24);
			if (ptrRawData == 0 || sizeOfData == 0)
				continue;

			uint32_t sig = readU32(file, ptrRawData);
			if (sig != 0x53445352) // "RSDS" — PDB 7.0
				continue;

			// RSDS layout: signature(4) + GUID(16) + age(4) + pdb_path(variable, null-terminated UTF-8)
			// Read GUID (16 bytes)
			uint8_t guidBytes[16];
			fseek(file, (long)(ptrRawData + 4), SEEK_SET);
			if (fread(guidBytes, 1, 16, file) != 16)
				continue;

			// Read Age (4 bytes)
			uint32_t age = readU32(file, ptrRawData + 20);

			// Read PDB path
			uint32_t pdbPathOffset = ptrRawData + 24;
			uint32_t pdbPathMaxLen = sizeOfData - 24;
			if (pdbPathMaxLen == 0 || pdbPathMaxLen > 4096)
				continue;

			char pdbPathUtf8[4096];
			fseek(file, (long)pdbPathOffset, SEEK_SET);
			size_t bytesRead = fread(pdbPathUtf8, 1, pdbPathMaxLen, file);
			if (bytesRead == 0)
				continue;

			// Ensure null termination
			pdbPathUtf8[bytesRead < sizeof(pdbPathUtf8) ? bytesRead : sizeof(pdbPathUtf8) - 1] = '\0';

			if (rtm::strLen(pdbPathUtf8) == 0)
				continue;

			// Convert UTF-8 PDB path to wide string
			size_t converted = mbstowcs(_outPdbPath, pdbPathUtf8, _outSize - 1);
			if (converted == (size_t)-1 || converted == 0)
				continue;

			_outPdbPath[converted] = L'\0';
			fclose(file);
			return true;
		}

		fclose(file);
		return false;
	}

	/// Builds a symbol server URL for downloading a PDB file based on the RSDS debug information.
	/// Format: http(s)://symbolserver/pdbname/GUIDAGE/pdbname
	/// Example: https://msdl.microsoft.com/download/symbols/ntdll.pdb/1234567890ABCDEF1/ntdll.pdb
	static bool buildPdbDownloadUrl(const wchar_t* _exePath, const char* _symbolServer, wchar_t* _outUrl, size_t _outSize)
	{
		_outUrl[0] = L'\0';

		if (!_symbolServer || rtm::strLen(_symbolServer) == 0)
			return false;

		rtm::WideToMulti exePathMulti(_exePath);
		FILE* file = fopen(exePathMulti, "rb");
		if (!file)
			return false;

		// Verify MZ signature
		if (readU16(file, 0) != 0x5A4D)
		{
			fclose(file); return false;
		}

		uint32_t peOffset = readU32(file, 0x3C);
		if (readU32(file, peOffset) != 0x00004550)
		{
			fclose(file); return false;
		}

		uint16_t numSections = readU16(file, peOffset + 6);
		uint16_t sizeOfOptHdr = readU16(file, peOffset + 20);
		uint32_t optHdrOffset = peOffset + 24;
		uint16_t optMagic = readU16(file, optHdrOffset);

		// Data directories begin at different offsets for PE32 vs PE32+
		uint32_t dataDirBase = optHdrOffset + (optMagic == 0x020B ? 112 : 96);

		// IMAGE_DIRECTORY_ENTRY_DEBUG is index 6
		uint32_t debugDirRVA = readU32(file, dataDirBase + 6 * 8);
		uint32_t debugDirSize = readU32(file, dataDirBase + 6 * 8 + 4);

		if (debugDirRVA == 0 || debugDirSize == 0)
		{
			fclose(file); return false;
		}

		// Convert debug directory RVA to file offset via section headers
		uint32_t sectionsBase = peOffset + 24 + sizeOfOptHdr;
		uint32_t debugDirFileOff = 0;
		for (uint16_t s = 0; s < numSections; ++s)
		{
			uint32_t sectBase = sectionsBase + (uint32_t)s * 40;
			uint32_t sectVA = readU32(file, sectBase + 12);
			uint32_t sectRawSize = readU32(file, sectBase + 16);
			uint32_t sectRawOffset = readU32(file, sectBase + 20);

			if (debugDirRVA >= sectVA && debugDirRVA < sectVA + sectRawSize)
			{
				debugDirFileOff = sectRawOffset + (debugDirRVA - sectVA);
				break;
			}
		}

		if (debugDirFileOff == 0)
		{
			fclose(file); return false;
		}

		// Walk IMAGE_DEBUG_DIRECTORY entries (28 bytes each); look for CodeView (Type == 2)
		uint32_t numEntries = debugDirSize / 28;
		for (uint32_t e = 0; e < numEntries; ++e)
		{
			uint32_t entryBase = debugDirFileOff + e * 28;
			uint32_t type = readU32(file, entryBase + 12);

			if (type != 2) // IMAGE_DEBUG_TYPE_CODEVIEW
				continue;

			uint32_t sizeOfData = readU32(file, entryBase + 16);
			uint32_t ptrRawData = readU32(file, entryBase + 24);
			if (ptrRawData == 0 || sizeOfData == 0)
				continue;

			uint32_t sig = readU32(file, ptrRawData);
			if (sig != 0x53445352) // "RSDS" — PDB 7.0
				continue;

			// RSDS layout: signature(4) + GUID(16) + age(4) + pdb_path(variable, null-terminated UTF-8)
			// Read GUID (16 bytes) - must be interpreted according to GUID structure
			uint8_t guidBytes[16];
			fseek(file, (long)(ptrRawData + 4), SEEK_SET);
			if (fread(guidBytes, 1, 16, file) != 16)
				continue;

			// Read Age (4 bytes)
			uint32_t age = readU32(file, ptrRawData + 20);

			// Read PDB path
			uint32_t pdbPathOffset = ptrRawData + 24;
			uint32_t pdbPathMaxLen = sizeOfData - 24;
			if (pdbPathMaxLen == 0 || pdbPathMaxLen > 4096)
				continue;

			char pdbPathUtf8[4096];
			fseek(file, (long)pdbPathOffset, SEEK_SET);
			size_t bytesRead = fread(pdbPathUtf8, 1, pdbPathMaxLen, file);
			if (bytesRead == 0)
				continue;

			// Ensure null termination
			pdbPathUtf8[bytesRead < sizeof(pdbPathUtf8) ? bytesRead : sizeof(pdbPathUtf8) - 1] = '\0';

			if (rtm::strLen(pdbPathUtf8) == 0)
				continue;

			// Extract PDB filename from path
			const char* pdbFileName = pdbPathUtf8;
			size_t pathLen = rtm::strLen(pdbPathUtf8);
			for (size_t i = pathLen; i > 0; --i)
			{
				if (pdbPathUtf8[i - 1] == '\\' || pdbPathUtf8[i - 1] == '/')
				{
					pdbFileName = &pdbPathUtf8[i];
					break;
				}
			}

			// Format GUID correctly for symbol server:
			// GUID structure: Data1(4 bytes LE) + Data2(2 bytes LE) + Data3(2 bytes LE) + Data4(8 bytes BE)
			// Symbol server format: Data1Data2Data3Data4 (uppercase hex, no dashes)
			// The bytes must be reordered to match the GUID structure, not just read sequentially
			char guidStr[33];
			snprintf(guidStr, sizeof(guidStr),
				"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
				// Data1 (4 bytes, reverse for little-endian)
				guidBytes[3], guidBytes[2], guidBytes[1], guidBytes[0],
				// Data2 (2 bytes, reverse for little-endian)
				guidBytes[5], guidBytes[4],
				// Data3 (2 bytes, reverse for little-endian)
				guidBytes[7], guidBytes[6],
				// Data4 (8 bytes, already in correct order/big-endian)
				guidBytes[8], guidBytes[9], guidBytes[10], guidBytes[11],
				guidBytes[12], guidBytes[13], guidBytes[14], guidBytes[15]);

			// Format Age as uppercase hex (1 to 8 hex digits, typically)
			char ageStr[9];
			snprintf(ageStr, sizeof(ageStr), "%X", age);

			// Build URL: server/pdbname/GUIDAGE/pdbname
			// Ensure server doesn't end with '/'
			char symbolServer[1024];
			rtm::strlCpy(symbolServer, sizeof(symbolServer), _symbolServer);
			size_t serverLen = rtm::strLen(symbolServer);
			if (serverLen > 0 && symbolServer[serverLen - 1] == '/')
				symbolServer[serverLen - 1] = '\0';

			char urlBuffer[4096];
			snprintf(urlBuffer, sizeof(urlBuffer), "%s/%s/%s%s/%s", symbolServer, pdbFileName, guidStr, ageStr, pdbFileName);

			// Convert to wide string
			size_t converted = mbstowcs(_outUrl, urlBuffer, _outSize - 1);
			if (converted == (size_t)-1 || converted == 0)
				continue;

			_outUrl[converted] = L'\0';
			fclose(file);
			return true;
		}

		fclose(file);
		return false;
	}

	extern char	 g_symStore[ResolveInfo::SYM_SERVER_BUFFER_SIZE];

	bool findSymbol(const char* _path, wchar_t _outSymbolPath[4096], const char* _symbolStore)
	{
		IDiaDataSource* pIDiaDataSource = nullptr;

		HRESULT hr = createDiaDataSource((void**)&pIDiaDataSource);

		if (FAILED(hr))
			return false;

		// The local file path must use '\\', and http(s) url must use '/'
		// Do not change the the slashes only if you detect each part in the _symbolStore.
		char symStoreBuffer[32 * 1024];
		rtm::strlCpy(symStoreBuffer, 32 * 1024, g_symStore);

		// The file path is not needed in the search path, loadDataForExe will find from src path automatically.
		// The semicolon is necessary between each path (or srv*).
		wchar_t moduleName[8 * 1024];

		if (!_path || (rtm::strLen(_path) == 0))
		{
			GetModuleFileNameW(nullptr, moduleName, (DWORD)(RTM_NUM_ELEMENTS(moduleName)));
		}
		else
		{
			rtm::MultiToWide widePath(_path);
			wcscpy(moduleName, widePath);
		}

		if (rtm::strLen(_symbolStore) > 1) // not ("" or null)
		{
			// Ensure semicolon separator before appending additional symbol store paths
			size_t existingLen = rtm::strLen(symStoreBuffer);
			if (existingLen > 0 && symStoreBuffer[existingLen - 1] != ';')
				rtm::strlCat(symStoreBuffer, 32*1024, ";");
			rtm::strlCat(symStoreBuffer, 32 * 1024, _symbolStore);
		}

		{
			size_t len = rtm::strLen(symStoreBuffer);
			if (len > 0 && symStoreBuffer[len - 1] != ';')
			{
				symStoreBuffer[len] = ';';
				symStoreBuffer[len + 1] = '\0';
				++len;
			}
			wchar_t envBuffer[32 * 1024];

			if (0 == GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", (LPWSTR)envBuffer, (DWORD)(RTM_NUM_ELEMENTS(envBuffer))))
				wcscpy(envBuffer, L"");

			rtm::WideToMulti mbEnvBuffer(envBuffer);

			rtm::strlCpy(&symStoreBuffer[len], uint32_t(32 * 1024) - uint32_t(len), mbEnvBuffer);
		}

		wchar_t outSymbolPath[32 * 1024];
		outSymbolPath[0] = L'\0';
		DiaLoadCallBack* callback = new DiaLoadCallBack(outSymbolPath);
		callback->AddRef();
		hr = pIDiaDataSource->loadDataForExe(moduleName, (LPOLESTR)rtm::MultiToWide(symStoreBuffer), callback);
		callback->Release();

		if (FAILED(hr))
		{
			// Strategy 1: Extract the PDB path from the executable's CodeView debug directory.
			// The RSDS record embeds the exact PDB filename the linker produced.
			wchar_t embeddedPdbPath[4096];
			if (extractPdbPathFromExe(moduleName, embeddedPdbPath, RTM_NUM_ELEMENTS(embeddedPdbPath)))
			{
				// If the embedded path is absolute and the file exists, use it directly
				if (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(embeddedPdbPath))
				{
					wcscpy(_outSymbolPath, embeddedPdbPath);
					pIDiaDataSource->Release();
					return true;
				}

				// Otherwise, try the PDB filename in the same directory as the executable
				const wchar_t* pdbFileName = embeddedPdbPath;
				size_t embeddedLen = wcslen(embeddedPdbPath);
				for (size_t i = embeddedLen; i > 0; --i)
				{
					if (embeddedPdbPath[i - 1] == L'\\' || embeddedPdbPath[i - 1] == L'/')
					{
						pdbFileName = &embeddedPdbPath[i];
						break;
					}
				}

				// Build path: exe directory + pdb filename
				wchar_t candidatePath[8 * 1024];
				wcscpy(candidatePath, moduleName);
				size_t modLen = wcslen(candidatePath);
				while (modLen > 0 && candidatePath[modLen - 1] != L'\\' && candidatePath[modLen - 1] != L'/')
					--modLen;
				candidatePath[modLen] = L'\0';
				wcscat(candidatePath, pdbFileName);

				if (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(candidatePath))
				{
					wcscpy(_outSymbolPath, candidatePath);
					pIDiaDataSource->Release();
					return true;
				}
			}

			// Strategy 2: Try building a symbol server download URL
			// Parse symbol server URLs from the store (typically starts with "srv*" or "cache*srv*")
			const char* symbolServerUrl = nullptr;
			if (rtm::strStr(symStoreBuffer, "http://") || rtm::strStr(symStoreBuffer, "https://"))
			{
				// Extract first http/https URL from symbol store string
				const char* httpStart = rtm::strStr(symStoreBuffer, "http");
				if (httpStart)
				{
					static char serverUrlBuffer[1024];
					const char* httpEnd = httpStart;
					while (*httpEnd && *httpEnd != ';' && *httpEnd != '*')
						++httpEnd;
					size_t urlLen = httpEnd - httpStart;
					if (urlLen > 0 && urlLen < sizeof(serverUrlBuffer) - 1)
					{
						rtm::memCopy(serverUrlBuffer, sizeof(serverUrlBuffer), httpStart, urlLen);
						serverUrlBuffer[urlLen] = '\0';
						symbolServerUrl = serverUrlBuffer;
					}
				}
			}

			if (symbolServerUrl)
			{
				wchar_t pdbDownloadUrl[4096];
				if (buildPdbDownloadUrl(moduleName, symbolServerUrl, pdbDownloadUrl, RTM_NUM_ELEMENTS(pdbDownloadUrl)))
				{
					// URL built successfully - this could be used for downloading
					// For now, just log or store it; actual download would require HTTP client
#if RTM_DEBUG
					OutputDebugStringW(L"PDB download URL: ");
					OutputDebugStringW(pdbDownloadUrl);
					OutputDebugStringW(L"\n");
#endif
					// TODO: Implement HTTP download and retry loadDataForExe with downloaded PDB
				}
			}

			// Strategy 3: Fallback — try replacing the executable extension with .pdb
			size_t len = wcslen(moduleName);
			if (len > 0)
			{
				--len;
				while ((len > 0) && (moduleName[len] != L'.'))
					--len;

				if (len > 0)
				{
					wcscpy(&moduleName[len], L".pdb");
					if (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(moduleName))
					{
						wcscpy(_outSymbolPath, moduleName);
						pIDiaDataSource->Release();
						return true;
					}
				}
			}

			pIDiaDataSource->Release();
			return false;
		}

		wcscpy(_outSymbolPath, outSymbolPath);
		pIDiaDataSource->Release();
		return true;
	}

} // namespace rdebug

const char* s_PDB_File_Extension = "pdb";

PDBFile::PDBFile() :
	m_pIDiaDataSource(nullptr),
	m_pIDiaSession(nullptr),
	m_pIDiaSymbol(nullptr)
{
	m_isStripped = false;
}

PDBFile::~PDBFile()
{
	close();
}

void PDBFile::close()
{
	if (m_pIDiaSymbol)
	{
		m_pIDiaSymbol->Release();
		m_pIDiaSymbol = nullptr;
	}
	if (m_pIDiaSession)
	{
		m_pIDiaSession->Release();
		m_pIDiaSession = nullptr;
	}
	if (m_pIDiaDataSource)
	{
		m_pIDiaDataSource->Release();
		m_pIDiaDataSource = nullptr;
	}
}

bool PDBFile::load(const wchar_t* _filename)
{
	if (!_filename) return false;
	if (wcslen(_filename) == 0) return false;

	if (m_pIDiaDataSource == nullptr)
	{
		HRESULT hr = rdebug::createDiaDataSource((void**)&m_pIDiaDataSource);

		if (FAILED(hr))
			return false;
	}

	bool bRet = false;

	if (INVALID_FILE_ATTRIBUTES != GetFileAttributesW(_filename))
	{
		if (loadSymbolsFileWithoutValidation(_filename))
		{
			bRet = m_pIDiaSession->get_globalScope(&m_pIDiaSymbol) == S_OK ? true : false;

			m_isStripped = false;
			if (m_pIDiaSymbol)
			{
				BOOL b = FALSE;
				HRESULT hr = m_pIDiaSymbol->get_isStripped(&b);
				if (hr == S_OK)
				{
					m_isStripped = b ? true : false;
				}
			}

			RTM_ASSERT(bRet, "");
		}
	}

	return bRet;
}

bool PDBFile::isLoaded() const
{
	return m_pIDiaSession != nullptr;
}

bool PDBFile::getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame)
{
	rtm::strlCpy(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), "Unknown");
	rdebug::addressToString(_address, _frame.m_func);
	_frame.m_line = 0;

	if (m_pIDiaSession)
	{
		IDiaSymbol* sym = nullptr;

		_address -= 1;	// get address of previous instruction

		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagFunction, &sym);
		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagPublicSymbol, &sym);

		if (sym)
		{
			BSTR SymName = nullptr;
			BSTR FileName = nullptr;
			DWORD LineNo = 0;

			if (FAILED(sym->get_undecoratedNameEx(UND_CODE, &SymName)))
			{
				sym->Release();
				return false;
			}

			if (SymName == nullptr)
				sym->get_name(&SymName);

			IDiaEnumLineNumbers* lineEnum = nullptr;
			if (FAILED(m_pIDiaSession->findLinesByVA(_address, 1, &lineEnum)))
			{
				SysFreeString(SymName);
				sym->Release();
				return false;
			}

			ULONG celt = 0;
			for (;;)
			{
				IDiaLineNumber* Line = nullptr;
				lineEnum->Next(1, &Line, &celt);
				if (!Line)
					celt = 1;			// hack, no file and line but has symbol name

				if (celt == 1)
				{
					IDiaSourceFile* SrcFile = nullptr;

					if (Line)
					{
						Line->get_sourceFile(&SrcFile);
						Line->get_lineNumber(&LineNo);
					}
					if (SrcFile)
						SrcFile->get_fileName(&FileName);

					_bstr_t a = SymName;
					const wchar_t* nameC = a.operator const wchar_t* ();
					rtm::WideToMulti name(nameC);
					rtm::strlCpy(_frame.m_func, RTM_NUM_ELEMENTS(_frame.m_func), name.m_ptr);

					if (FileName)
					{
						a = FileName;
						nameC = a.operator const wchar_t* ();
						rtm::WideToMulti file(nameC);
						rtm::strlCpy(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), file.m_ptr);
						_frame.m_line = LineNo;
					}
					else
						_frame.m_line = 0;

					SysFreeString(SymName);

					if (FileName)
						SysFreeString(FileName);

					if (Line)
						Line->Release();

					if (SrcFile)
						SrcFile->Release();

					lineEnum->Release();
					sym->Release();
					return true;
				}

				if (celt != 1)
				{
					if (Line)
						Line->Release();
					break;
				}
			}

			SysFreeString(SymName);
			lineEnum->Release();
			sym->Release();
		}
	}
	return false;
}

uint64_t PDBFile::getSymbolID(uint64_t _address)
{
	DWORD ID = 0;
	if (m_pIDiaSession)
	{
		IDiaSymbol* sym = nullptr;

		_address -= 1;	// get address of previous instruction

		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagFunction, &sym);
		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagPublicSymbol, &sym);

		if (sym)
		{
			sym->get_symIndexId(&ID);
			sym->Release();
		}
	}

	return ID;
}

bool PDBFile::loadSymbolsFileWithoutValidation(const wchar_t* _PdbFileName)
{
	bool bRet = false;
	IDiaDataSource* pIDiaDataSource = m_pIDiaDataSource;

	if (pIDiaDataSource)
	{
		bool bContinue = false;

		HRESULT hr = pIDiaDataSource->loadDataFromPdb(_PdbFileName);

		if (SUCCEEDED(hr))
			bContinue = true;
		else
		{
			switch (hr)
			{
			case E_PDB_NOT_FOUND:
			case E_PDB_FORMAT:
			case E_INVALIDARG:
				break;
			case E_UNEXPECTED:
				bContinue = true;
				break;
			default:
				break;
			}
		}

		if (bContinue)
			bRet = pIDiaDataSource->openSession(&m_pIDiaSession) == S_OK ? true : false;
	}
	return bRet;
}

#endif // RTM_PLATFORM_WINDOWS
