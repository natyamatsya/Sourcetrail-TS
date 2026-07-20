#ifndef CXX_COMPILATION_DATABASE_SINGLE_H
#define CXX_COMPILATION_DATABASE_SINGLE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Tooling/CompilationDatabase.h>
#endif

SRCTRL_EXPORT class CxxCompilationDatabaseSingle: public clang::tooling::CompilationDatabase
{
public:
	CxxCompilationDatabaseSingle(const clang::tooling::CompileCommand& command);

	std::vector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef FilePath) const override;
	std::vector<std::string> getAllFiles() const override;
	std::vector<clang::tooling::CompileCommand> getAllCompileCommands() const override;

private:
	clang::tooling::CompileCommand m_command;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxCompilationDatabaseSingle.inl"
#endif

#endif	  // CXX_COMPILATION_DATABASE_SINGLE_H
