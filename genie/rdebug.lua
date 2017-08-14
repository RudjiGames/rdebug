--
-- Copyright (c) 2017 Milos Tosic. All rights reserved. 
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectDependencies_rdebug()
	return { "DIA", "rbase" }
end

function projectAdd_rdebug()
	addProject_lib("rdebug", Lib.Tool)
end
