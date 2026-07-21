#include "TestFileRegister.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePathFilter.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

TestFileRegister::TestFileRegister()
	: FileRegister(FilePath(), std::set<FilePath>(), {FilePathFilter("")})
{
}

TestFileRegister::~TestFileRegister() = default;

bool TestFileRegister::hasFilePath(const FilePath&  /*filePath*/) const
{
	return true;
}
