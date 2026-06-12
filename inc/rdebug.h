//--------------------------------------------------------------------------//
/// Copyright 2026 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_DEBUG_H
#define RTM_DEBUG_H

#include <stdint.h>

typedef struct _rtmLibInterface rtmLibInterface;

namespace rdebug {

	/// Toolchain description structure
	struct Toolchain
	{
		enum Type
		{
			Unknown,	// 
			MSVC,		// pdb
			GCC			// nm, addr2line, c++filt
		};

		Toolchain::Type	m_type;
		char			m_toolchainPath[2048];	// or symbol store path list for MSVC
		char			m_toolchainPrefix[64];

		Toolchain();
	};

	/// Module information structure
	struct ModuleInfo
	{
		uint64_t		m_baseAddress;
		uint64_t		m_size;
		uint64_t		m_loadTime;
		uint64_t		m_unloadTime;
		char			m_modulePath[1024];
		Toolchain		m_toolchain;

		inline bool checkAddress(uint64_t _address) const
		{
			// Module occupies [m_baseAddress, m_baseAddress + m_size); the first
			// byte past the end belongs to the next module, so use '<' not '<='.
			return (_address - m_baseAddress < m_size);
		}

		inline bool checkAddressAndTime(uint64_t _address, uint64_t _operationTime) const
		{
			return ((_operationTime >= m_loadTime)		&&
					(_operationTime <= m_unloadTime)	&&
					(_address - m_baseAddress < m_size));
		}
	};

	/// Single entry (frame) in stack trace
	struct StackFrame
	{
		char		m_moduleName[256];
		char		m_file[1024];
		char		m_func[16384];			// very big in order to support dumb code (templates, lambdas and such)
		uint32_t	m_line;
	};

	/// Module loading callback, called per module
	typedef void (*module_load_cb)(const char* _name, void* _customData);

	/// Initialize rdebug library
	///
	/// @param _libInterface
	///
	bool init(rtmLibInterface* _libInterface = 0);

	/// Shut down rdebug library and release internal resources
	///
	void shutDown(); 

	/// Sets symbol server path
	///
	/// @param _symStore
	///
	void symbolSetServerSource(const char* _symStore);

	/// Symbol-resolution status callback - receives human-readable progress messages such as
	/// symbol-server downloads. Optional; when unset, no status is reported.
	typedef void (*symbol_status_cb)(const char* _message, void* _userData);

	/// Registers a callback for symbol-resolution status messages (downloads, failures, ...).
	/// Pass (0, 0) to clear. Safe to leave unset.
	void symbolResolverSetStatusCallback(symbol_status_cb _callback, void* _userData);

	/// Creates debug symbol resolver based on 
	///
	/// @param _moduleInfos
	/// @param _numInfos
	/// @param _tc
	/// @param _executable
	///
	uintptr_t symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, const char* _executable, module_load_cb _callback = 0, void* _data = 0);

	/// Creates debug symbol resolver based on 
	///
	uintptr_t symbolResolverCreateForCurrentProcess();

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	///
	void symbolResolverDelete(uintptr_t _resolver);

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	/// @param _address
	/// @param _frame
	///
	void symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame* _frame);

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	/// @param _address
	/// @param _skipCount
	///
	uint64_t symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address);

	/// Progress callback for symbolResolverGetAddressIDs: _pct in [0,100]. Invoked only from the
	/// calling thread (safe to touch host UI), never from worker threads.
	typedef void (*SymbolIDProgress)(void* _data, float _pct);

	/// Batch form of symbolResolverGetAddressID: resolves _count addresses into _outIDs. Internally
	/// groups addresses by module and resolves modules in parallel (each module's symbol session is
	/// driven by a single worker), which is far faster than serial per-address resolution on large
	/// captures. _outIDs must have room for _count entries. _progress (optional) reports completion.
	void symbolResolverGetAddressIDs(uintptr_t _resolver, const uint64_t* _addresses, uint64_t* _outIDs, uint32_t _count, SymbolIDProgress _progress = 0, void* _progressData = 0);

	/// Returns true if binary at the given path is 64bit
	///
	/// @param _path
	///
	bool processIs64bitBinary(const char* _path);

	/// Creates and runs a new process with injected DLL
	///
	/// @param _executablePath
	/// @param _DLLPath
	/// @param _cmdLine
	/// @param _workingDir
	///
	bool processInjectDLL(const char* _executablePath, const char* _DLLPath, const char* _cmdLine, const char* _workingDir, uint32_t* _pid = 0);

	/// Create and run a new process given the command line
	///
	/// @param _cmdLine
	///
	bool processRun(const char* _cmdLine, bool _hideWindow = false, uint32_t* _exitCode = 0);

	/// Run a new process and return the console output
	///
	/// @param _cmdLine
	///
	char* processGetOutputOf(const char* _cmdLine, bool _redirectIO = false);

	/// Release memory previously allocated by processGetOutputOf
	///
	/// @param _cmdLine
	///
	void processReleaseOutput(const char* _output);

	///
	void addressToString(uint64_t _address, char* _buffer);

	//----------------------------------------------------------------------//
	/// Type (struct/class/union) layout from PDB debug info. Used by the Types analytics view to show
	/// member layout + padding. Windows/MSVC (DIA) only; other toolchains return false.
	//----------------------------------------------------------------------//

	/// One row in a type's layout: a data member, a base-class subobject, or a synthesized padding gap.
	struct TypeMember
	{
		enum Kind { Field, Base, Bitfield, Padding };

		char		m_name[1024];		///< member name; "" for padding rows
		char		m_typeName[1024];	///< member/base type spelled out; "" for padding rows
		uint32_t	m_offset;			///< byte offset within the enclosing type
		uint32_t	m_size;			///< size in bytes (for a bitfield: the storage-unit size)
		uint8_t		m_kind;			///< TypeMember::Kind
		uint8_t		m_bitOffset;		///< first bit (bitfields only)
		uint8_t		m_bitWidth;		///< width in bits (bitfields only)
	};

	/// Summary of a type's layout. Members (incl. synthesized padding rows) arrive via the callback.
	struct TypeLayout
	{
		char		m_name[1024];
		uint32_t	m_size;			///< sizeof, from the PDB
		uint32_t	m_align;			///< best-effort alignment (largest member alignment)
		uint32_t	m_paddingTotal;		///< total padding bytes (0 for unions)
		uint8_t		m_udtKind;			///< 0=struct, 1=class, 2=union, 3=interface (DIA UdtKind)
	};

	/// Per-member visitor for pdbGetTypeLayout - fired in offset order with padding rows interleaved.
	typedef void (*type_member_cb)(const TypeMember* _member, void* _userData);

	/// Resolve a type's layout (members, base subobjects, padding) from a module's PDB. _moduleName is
	/// matched against the module file name (case-insensitive substring). Loads the module's PDB on
	/// demand and is self-contained re: COM (safe to call on any thread). Returns false if the module,
	/// its PDB, or the type can't be found, or on non-MSVC toolchains.
	bool pdbGetTypeLayout(uintptr_t _resolver, const char* _moduleName, const char* _typeName,
						  TypeLayout* _outLayout, type_member_cb _cb, void* _userData);

	/// One entry in a module's type index: just enough to list + search types cheaply (full layout is
	/// fetched on demand with pdbGetTypeLayout).
	struct TypeBrief
	{
		char		m_name[1024];
		uint32_t	m_size;			///< sizeof
		uint32_t	m_paddingTotal;		///< total padding bytes in the layout (0 for unions)
		uint8_t		m_udtKind;			///< 0=struct, 1=class, 2=union, 3=interface
	};

	typedef void (*type_brief_cb)(const TypeBrief* _type, void* _userData);

	/// Enumerate the named UDTs (struct/class/union) defined in a module's PDB - name + size only, for a
	/// searchable type list. De-duplicated by name; zero-sized / anonymous types are skipped. Potentially
	/// slow on large PDBs (call off the UI thread). Loads the PDB on demand, self-contained re: COM.
	/// Returns the number of types emitted (0 on failure / non-MSVC).
	uint32_t pdbEnumerateTypes(uintptr_t _resolver, const char* _moduleName, type_brief_cb _cb, void* _userData);

} // namespace rdebug

#endif // RTM_DEBUG_H
