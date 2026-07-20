// Inline implementations for CxxCompilationDatabaseSingle.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

inline CxxCompilationDatabaseSingle::CxxCompilationDatabaseSingle(const clang::tooling::CompileCommand& command)
	: m_command(command)
{
}

inline std::vector<clang::tooling::CompileCommand> CxxCompilationDatabaseSingle::getCompileCommands(
	llvm::StringRef  /*FilePath*/) const
{
	return getAllCompileCommands();
}

inline std::vector<std::string> CxxCompilationDatabaseSingle::getAllFiles() const
{
	return std::vector<std::string>(1, m_command.Filename);
}

inline std::vector<clang::tooling::CompileCommand> CxxCompilationDatabaseSingle::getAllCompileCommands() const
{
	return std::vector<clang::tooling::CompileCommand>(1, m_command);
}
