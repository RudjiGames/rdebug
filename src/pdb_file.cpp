//--------------------------------------------------------------------------//
/// Copyright (c) 2023 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include <rdebug_pch.h>
#include <rdebug/src/pdb_file.h>

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

PDBFile::PDBFile() :
	m_pIDiaDataSource( NULL ),
	m_pIDiaSession( NULL ), 
	m_pIDiaSymbol( NULL )	
{
	m_isStripped = false;
}

PDBFile::~PDBFile()
{
	close();
}

void PDBFile::close()
{
	if ( m_pIDiaSymbol ) 
	{
		m_pIDiaSymbol->Release();
		m_pIDiaSymbol = NULL;
	}
	if ( m_pIDiaSession ) 
	{
		m_pIDiaSession->Release();
		m_pIDiaSession = NULL;
	}
	if (m_pIDiaDataSource)
	{
		m_pIDiaDataSource->Release();
		m_pIDiaDataSource = NULL;
	}
}

bool PDBFile::load(const char* _filename)
{
	if (!_filename) return false;
	if (rtm::strLen(_filename) == 0) return false;

	if (m_pIDiaDataSource == NULL)
	{
		HRESULT hr = rdebug::createDiaDataSource((void**)&m_pIDiaDataSource);

		if (FAILED(hr))
			return false;
	}

	bool bRet = false;

	const char* ext = rtm::pathGetExt(_filename);
	if (ext && (rtm::strCmp(ext, s_PDB_File_Extension)==0))
	{
		if (loadSymbolsFileWithoutValidation(_filename))
		{
			bRet = m_pIDiaSession->get_globalScope( &m_pIDiaSymbol ) == S_OK ? true : false;

			m_isStripped = false;
			if( m_pIDiaSymbol )
			{
				BOOL b = FALSE;
				HRESULT hr = m_pIDiaSymbol->get_isStripped(&b);
				if(hr==S_OK)
				{
					m_isStripped = b?true:false;
				}
			}
	
			RTM_ASSERT(bRet, "");
		}
	}

	return bRet;
}

void PDBFile::getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame)
{
	rtm::strlCpy(_frame.m_file, RTM_NUM_ELEMENTS(_frame.m_file), "Unknown");
	rtm::strlCpy(_frame.m_func, RTM_NUM_ELEMENTS(_frame.m_func), "Unknown");
	_frame.m_line = 0;

	if( m_pIDiaSession )
	{
		IDiaSymbol* sym = NULL;

		_address -= 1;	// get address of previous instruction

		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagFunction, &sym);
		if (!sym)	m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagPublicSymbol, &sym);

		if (sym)
		{
			BSTR SymName = NULL;
			BSTR FileName = NULL;
			DWORD LineNo = 0;

			if (FAILED(sym->get_undecoratedNameEx(UND_CODE, &SymName)))
			{
				sym->Release();
				return;
			}

			if (SymName == NULL)
				sym->get_name(&SymName);
	
			IDiaEnumLineNumbers* lineEnum = NULL;
			if (FAILED(m_pIDiaSession->findLinesByVA(_address,1,&lineEnum)))
				return;

			ULONG celt = 0;
			for (;;)
			{
				IDiaLineNumber* Line = NULL;
				lineEnum->Next(1, &Line, &celt);
				if (!Line)
					celt = 1;			// hack, no file and line but has symbol name

				if (celt==1)
				{
					IDiaSourceFile* SrcFile = NULL;
					
					if (Line)
					{
						Line->get_sourceFile(&SrcFile);
						Line->get_lineNumber(&LineNo);
					}
					if (SrcFile)
						SrcFile->get_fileName(&FileName);

					_bstr_t a = SymName;
					const wchar_t* nameC = a.operator const wchar_t*();
					rtm::WideToMulti name(nameC);
					rtm::strlCpy(_frame.m_func, RTM_NUM_ELEMENTS(_frame.m_func), name.m_ptr);

					if (FileName)
					{
						a = FileName;
						nameC = a.operator const wchar_t*();
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
					return;
				}

				if (celt != 1)
					break;
			}
		
			SysFreeString(SymName);
			lineEnum->Release();
			sym->Release();
		}
	}
}

uint64_t PDBFile::getSymbolID(uint64_t _address)
{
	DWORD ID = 0;
	if (m_pIDiaSession)
	{
		IDiaSymbol* sym = NULL;

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

bool PDBFile::loadSymbolsFileWithoutValidation(const char* _PdbFileName)
{
	bool bRet = false;
	IDiaDataSource* pIDiaDataSource = m_pIDiaDataSource;

	if(pIDiaDataSource)
	{
		bool bContinue = false;

		HRESULT hr = pIDiaDataSource->loadDataFromPdb(rtm::MultiToWide(_PdbFileName));
		
		if(SUCCEEDED(hr))
			bContinue = true;
		else
		{
			switch(hr)
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
		
		if(bContinue)
			bRet = pIDiaDataSource->openSession( &m_pIDiaSession )==S_OK?true:false;
	}
	return bRet;
}

#endif // RTM_PLATFORM_WINDOWS
