--
-- Copyright (c) 2017 Milos Tosic. All rights reserved. 
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectLink_DIA()
	configuration {"windows", "x32", "not gmake" }
		libdirs { getProjectPath("DIA", ProjectPath.Root) .. "DIA/lib/x32/" }
		links {"diaguids"}
	configuration {"windows", "x64", "not gmake" }
		libdirs { getProjectPath("DIA", ProjectPath.Root) .. "DIA/lib/x64/" }
		links {"diaguids"}
	configuration {}		
end

function projectDependencies_rdebug()
	return { "DIA", "rbase" }
end


function rdebugDisableWarningsForMinGW()
	configuration { "gmake" }
		if "mingw-gcc" == _OPTIONS["gcc"] then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			buildoptions { "-Wno-unknown-pragmas" }
		end
	configuration {}
end

function projectAdd_rdebug()
	addProject_lib("rdebug", Lib.Tool, false, nil, rdebugDisableWarningsForMinGW)
end

