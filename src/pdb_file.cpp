//--------------------------------------------------------------------------//
/// Copyright (c) 2010-2017 Milos Tosic. All Rights Reserved.              ///
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
#pragma warning (default: 4091)
#pragma comment(lib, "diaguids.lib")
#endif

#include <DIA/include/diacreate.h>

namespace rdebug {

class DiaLoadCallBack : public IDiaLoadCallback2
{
	private:
		uint32_t	m_refCount;
		wchar_t*	m_buffer;

	public:
		DiaLoadCallBack(wchar_t inBuffer[1024]) : m_refCount(0), m_buffer(inBuffer) {}

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

		if		(rid == IID_IDiaLoadCallback2)	*ppUnk = (IDiaLoadCallback2 *)this;
		else if (rid == IID_IDiaLoadCallback)	*ppUnk = (IDiaLoadCallback *)this;
		else if (rid == IID_IUnknown)			*ppUnk = (IUnknown *)this;
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

bool findSymbol(const char* _path, char _outSymbolPath[1024], const char* _symbolStore)
{
	IDiaDataSource* pIDiaDataSource = NULL;
	HRESULT hr = NoRegCoCreate(L"msdia140.dll", __uuidof(DiaSource), __uuidof(IDiaDataSource), (void**)&pIDiaDataSource);
	
	if (FAILED(hr))
		hr = CoCreateInstance(CLSID_DiaSource, NULL, CLSCTX_INPROC_SERVER, IID_IDiaDataSource, (void**)&pIDiaDataSource);

	if(FAILED(hr))
		return false;
	
	rtm::MultiToWide symbolStore(_symbolStore);

	wchar_t symStoreBuffer[2048];
	if (_symbolStore)
	{
		wcscpy(symStoreBuffer, symbolStore);
	}
	else
	{
		if (0 == GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", (LPWSTR)symStoreBuffer, sizeof(symStoreBuffer)))
			wcscpy(symStoreBuffer, L"");
	}

	rtm::MultiToWide path(_path);

	const wchar_t* scrPath = path;
	wchar_t moduleName[512];
	if (!_path || (wcslen(path) == 0))
	{
		GetModuleFileNameW(NULL, moduleName, sizeof(wchar_t)*512);
		scrPath = moduleName;
	}

	wchar_t outSymbolPath[1024];
	DiaLoadCallBack callback(outSymbolPath);
	callback.AddRef();
	hr = pIDiaDataSource->loadDataForExe((LPOLESTR)path, (LPOLESTR)symStoreBuffer, &callback);

	if (FAILED(hr))
	{
		pIDiaDataSource->Release();
		return false;
	}

	rtm::WideToMulti result(outSymbolPath);
	strcpy(_outSymbolPath, result);
	
	pIDiaDataSource->Release();
	return true;
}

} // namespace rdebug

const wchar_t*	TEXT_PDB_FILE_EXTENSION	= L".pdb";

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

bool PDBFile::load(const wchar_t* _filename)
{
	if (!_filename) return false;
	if (wcslen(_filename) == 0) return false;

	if (m_pIDiaDataSource == NULL)
	{
		HRESULT hr = NoRegCoCreate(L"msdia140.dll", __uuidof(DiaSource), __uuidof(IDiaDataSource), (void**)&m_pIDiaDataSource);

		if (FAILED(hr))
			hr = CoCreateInstance(	__uuidof(DiaSource), NULL, CLSCTX_ALL,// CLSCTX_INPROC_SERVER, 
									__uuidof(IDiaDataSource), (void**)&m_pIDiaDataSource);
		if (FAILED(hr))
			return false;
	}

	bool bRet = false;

	wchar_t wszExt[MAX_PATH];
	if (_wsplitpath_s( _filename, NULL, 0, NULL, 0, NULL, 0, wszExt, MAX_PATH)==0)
	{
		if (_wcsicmp( wszExt, TEXT_PDB_FILE_EXTENSION )==0)
		{
			if (loadSymbolsFileWithoutValidation(_filename))
			{
				bRet = m_pIDiaSession->get_globalScope( &m_pIDiaSymbol )==S_OK?true:false;

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
	
				_ASSERT( bRet );
			}
		}
	}

	if (bRet)
	{
		m_sFileName = _filename;

		IDiaEnumSymbols* compilands;
		HRESULT hr = m_pIDiaSymbol->findChildren(SymTagCompiland, NULL, NULL, &compilands);
		RTM_ASSERT(hr == S_OK, "Unable to find PDB's compilands.");
		if (hr != S_OK)
			return bRet;

		IDiaSymbol* currCompiland;
		DWORD numSymbolsFetched;
		for(HRESULT moreChildren = compilands->Next(1, &currCompiland, &numSymbolsFetched);
			moreChildren == S_OK; moreChildren = compilands->Next(1, &currCompiland, &numSymbolsFetched))
		{
			BSTR pName;
			hr = currCompiland->get_name(&pName);
			std::wstring compilandName;
			if(hr == S_OK)
			{
				compilandName = pName;
				SysFreeString(pName);
			}
			else
			{
				compilandName = L"Unknown";
			}

			IDiaEnumSymbols* functions;
			IDiaSymbol* currFunction;
			if (!m_isStripped)
				hr = currCompiland->findChildren(SymTagFunction, NULL, NULL, &functions);
			else
				hr = currCompiland->findChildren(SymTagPublicSymbol, NULL, NULL, &functions);

			if(hr != S_OK)
			{
				currCompiland->Release();
				continue;
			}
			
			for(HRESULT moreFuncs = functions->Next(1, &currFunction, &numSymbolsFetched);
				moreFuncs == S_OK; moreFuncs = functions->Next(1, &currFunction, &numSymbolsFetched))
			{
				rdebug::Symbol symbol;

				rtm::WideToMulti compilandNameMulti(compilandName.c_str());
				symbol.m_file = compilandNameMulti.m_ptr;
				hr = currFunction->get_name(&pName);
				if(hr == S_OK)
				{
					rtm::WideToMulti symbolNameMulti(pName);
					symbol.m_name = symbolNameMulti.m_ptr;
					SysFreeString(pName);
				}
				else
				{
					symbol.m_name = "Unknown";
				}

				DWORD address;
				hr = currFunction->get_relativeVirtualAddress(&address);
				symbol.m_offset = address;

				if(hr != S_OK)
				{
					currFunction->Release();
					continue;
				}
		
				unsigned long long	length;
				hr = currFunction->get_length(&length);
				if(hr != S_OK)
				{
					currFunction->Release();
					continue;
				}
				symbol.m_size = length;
			
				m_symMap.addSymbol(symbol);
				currFunction->Release();
			}
			
			functions->Release();
			currCompiland->Release();
		}
		compilands->Release();
	}

	m_symMap.sort();

	return bRet;
}

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

void PDBFile::getSymbolByAddress(uint64_t _address, rdebug::StackFrame& _frame)
{
	strcpy(_frame.m_file, "Unknown");
	strcpy(_frame.m_func, "Unknown");
	_frame.m_line = 0;

	if( m_pIDiaSession )
	{
		IDiaSymbol* sym = NULL;

		if (!m_isStripped)
			m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagFunction, &sym);
		else
			m_pIDiaSession->findSymbolByVA((ULONGLONG)_address, SymTagPublicSymbol, &sym);

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
				bool shouldBreak = false;
				IDiaLineNumber* Line = NULL;
				lineEnum->Next(1, &Line, &celt);
				if (!Line)
				{
					shouldBreak = true;
					celt = 1;			// hack, no file and line but has symbol name
				}

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
					strcpy(_frame.m_func, name.m_ptr);

					if (FileName)
					{
						a = FileName;
						nameC = a.operator const wchar_t*();
						rtm::WideToMulti file(nameC);
						strcpy(_frame.m_file, file.m_ptr);
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

uintptr_t PDBFile::getSymbolID(uint64_t _address)
{
	rdebug::Symbol* sym = m_symMap.findSymbol(_address);
	if (sym)
		return (uintptr_t)sym->m_offset;
	else
		return 0;
}

bool PDBFile::loadSymbolsFileWithoutValidation(const wchar_t* _PdbFileName)
{
	bool bRet = false;
	IDiaDataSource* pIDiaDataSource = m_pIDiaDataSource;

	if(pIDiaDataSource)
	{
		bool bContinue = false;

		HRESULT hr = pIDiaDataSource->loadDataFromPdb(_PdbFileName);
		
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
