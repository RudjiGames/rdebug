--
-- Copyright 2025 Milos Tosic. All rights reserved. 
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectDependencies_rdebug()
	return { "rbase" }
end

DIAErrorPrinted = false

function projectExtraConfigExecutable_rdebug()
	if getTargetOS() == "windows" then
		local DIApath = getProjectPath("DIA", ProjectPath.Root);
		if DIApath == nil then
			if DIAErrorPrinted == false then
				print("DIA dependency is missing, please clone from https://github.com/RudjiGames/DIA!")
			end
			DIAErrorPrinted = true
			return
		end

		configuration {"windows", "x32", "not gmake" }
			includedirs { getProjectPath("DIA", ProjectPath.Root) }
			libdirs { getProjectPath("DIA", ProjectPath.Dir) .. "/lib/x32/" }
			links {"diaguids"}
		configuration {"windows", "x64", "not gmake" }
			includedirs { getProjectPath("DIA", ProjectPath.Root) }
			libdirs { getProjectPath("DIA", ProjectPath.Dir) .. "/lib/x64/" }
			links {"diaguids"}
		configuration {}
	end
end

function projectExtraConfig_rdebug()
	configuration { "gmake" }
		if "mingw-gcc" == _OPTIONS["gcc"] then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			buildoptions { "-Wno-unknown-pragmas" }
		end
end

function projectAdd_rdebug()
	addProject_lib("rdebug", Lib.Tool)
end

