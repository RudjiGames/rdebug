--
-- Copyright 2025 Milos Tosic. All rights reserved. 
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectDependencies_rdebug()
	return { "rbase", "DIA" }
end

function projectExtraConfig_rdebug()
	configuration { "gmake" }
		if "mingw-gcc" == _OPTIONS["gcc"] then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			buildoptions { "-Wno-unknown-pragmas" }
		end
	configuration { "linux-*" }
		buildoptions { "-Wimplicit-fallthrough=0" }
	configuration {}
	includedirs	{ path.join(projectGetPath("rdebug"), "../") }
end

function projectExtraConfigExecutable_rdebug() 
	includedirs	{ path.join(projectGetPath("rdebug"), "../") }
end

function projectAdd_rdebug()
	addProject_lib("rdebug", LibraryType.Tool)
end
