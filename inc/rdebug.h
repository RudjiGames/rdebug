//--------------------------------------------------------------------------//
/// Copyright (c) 2023 by Milos Tosic. All Rights Reserved.                ///
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
			GCC,		// nm, addr2line, c++filt
			PS4,		// 
			PS3SNC		// ps3bin
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
		char			m_modulePath[1024];
		Toolchain		m_toolchain;

		inline bool checkAddress(uint64_t _address) const
		{
			return (_address - m_baseAddress <= m_size);
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

	/// Creates debug symbol resolver based on 
	///
	/// @param _moduleInfos
	/// @param _numInfos
	/// @param _tc
	/// @param _executable
	///
	uintptr_t	symbolResolverCreate(ModuleInfo* _moduleInfos, uint32_t _numInfos, const char* _executable, module_load_cb _callback = 0, void* _data = 0);

	/// Creates debug symbol resolver based on 
	///
	uintptr_t	symbolResolverCreateForCurrentProcess();

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	///
	void		symbolResolverDelete(uintptr_t _resolver);

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	/// @param _address
	/// @param _frame
	///
	void		symbolResolverGetFrame(uintptr_t _resolver, uint64_t _address, StackFrame* _frame);

	/// Creates debug symbol resolver based on 
	///
	/// @param _resolver
	/// @param _address
	/// @param _skipCount
	///
	uint64_t	symbolResolverGetAddressID(uintptr_t _resolver, uint64_t _address);

	/// Returns true if binary at the given path is 64bit
	///
	/// @param _path
	///
	bool		processIs64bitBinary(const char* _path);

	/// Creates and runs a new process with injected DLL
	///
	/// @param _executablePath
	/// @param _DLLPath
	/// @param _cmdLine
	/// @param _workingDir
	///
	bool		processInjectDLL(const char* _executablePath, const char* _DLLPath, const char* _cmdLine, const char* _workingDir, uint32_t* _pid = 0);

	/// Create and run a new process given the command line
	///
	/// @param _cmdLine
	///
	bool		processRun(const char* _cmdLine, bool _hideWindow = false, uint32_t* _exitCode = 0);

	/// Run a new process and return the console output
	///
	/// @param _cmdLine
	///
	char*		processGetOutputOf(const char* _cmdLine, bool _redirectIO = false);

	/// Release memory previously allocated by processGetOutputOf
	///
	/// @param _cmdLine
	///
	void		processReleaseOutput(const char* _output);

} // namespace rdebug

#endif // RTM_DEBUG_H
