//--------------------------------------------------------------------------//
/// Copyright (c) 2023 by Milos Tosic. All Rights Reserved.                ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RTM_TOOLUTIL_TOOLUTIL_PCH_H
#define RTM_TOOLUTIL_TOOLUTIL_PCH_H

#define RBASE_NAMESPACE rdebug
#define RTM_DEFINE_STL_STRING

#include <rbase/inc/platform.h>
#include <rbase/inc/stringfn.h>
#include <rbase/inc/winchar.h>
#include <rbase/inc/libhandler.h>
#include <rbase/inc/path.h>

#include <rdebug/inc/rdebug.h>

#include <raw_pdb/src/PDB.h>
#include <raw_pdb/src/PDB_DBIStream.h>
#include <raw_pdb/src/PDB_InfoStream.h>
#include <raw_pdb/src/Foundation/PDB_PointerUtil.h>
#include <raw_pdb/src/PDB_RawFile.h>
#include <raw_pdb/src/PDB_TPIStream.h>
#include <raw_pdb/src/Examples/ExampleMemoryMappedFile.h>

#endif // RTM_TOOLUTIL_TOOLUTIL_PCH_H
