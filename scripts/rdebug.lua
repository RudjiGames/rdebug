--
-- Copyright 2025 Milos Tosic. All rights reserved. 
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectDependencies_rdebug()
	return { "rbase" }
end

function projectExtraConfigExecutable_rdebug()
	if getTargetOS() == "windows" then
		local DIAProjectPath = projectGetPath("DIA")
		local DIApath = path.join(DIAProjectPath, "../");

		configuration {"windows", "x32", "not gmake" }
			includedirs { DIApath }
			libdirs { DIAProjectPath .. "/lib/x32/" }
			links {"diaguids"}
		configuration {"windows", "x64", "not gmake" }
			includedirs { DIApath }
			libdirs { DIAProjectPath .. "/lib/x64/" }
			links {"diaguids"}
		configuration {}
	end
end

function projectExtraConfig_rdebug()
	configuration { "gmake" }
		if "mingw-gcc" == _OPTIONS["gcc"] then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			buildoptions { "-Wno-unknown-pragmas" }
		end
	configuration { "linux-*" }
		buildoptions { "-Wimplicit-fallthrough=0" }
	configuration {}
end

function projectAdd_rdebug()
	addProject_lib("rdebug", LibraryType.Tool)
end
