#ifndef CXX_PARSER_H
#define CXX_PARSER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "Parser.h"

class CanonicalFilePathCache;
class CxxDiagnosticConsumer;
class FilePath;
class FileRegister;
class IndexerCommandCxx;
class TextAccess;


namespace clang::tooling
{
class CompilationDatabase;
class FixedCompilationDatabase;
} // namespace clang::tooling


struct IndexerStateInfo;
#endif

SRCTRL_EXPORT class CxxParser: public Parser
{
public:
	static std::vector<std::string> getCommandlineArgumentsEssential(
		const std::vector<std::string>& compilerFlags);
	static void initializeLLVM();

	CxxParser(
		ParserClient& client,
		std::shared_ptr<FileRegister> fileRegister,
		std::shared_ptr<IndexerStateInfo> indexerStateInfo);

	void buildIndex(std::shared_ptr<const IndexerCommandCxx> indexerCommand);
	void buildIndex(
		const std::string& fileName,
		std::shared_ptr<TextAccess> fileContent,
		std::vector<std::string> compilerFlags = {});

	static std::vector<std::string> getCommandlineArgumentsEssential(
		const std::string& compilerPath, const std::vector<std::string>& compilerFlags);

private:
	void runTool(
		clang::tooling::CompilationDatabase* compilationDatabase,
		const FilePath& sourceFilePath,
		const std::string& compilerPath = {});

	std::shared_ptr<CxxDiagnosticConsumer> getDiagnostics(
		const FilePath& sourceFilePath,
		CanonicalFilePathCache& canonicalFilePathCache,
		bool logErrors) const;

	std::shared_ptr<FileRegister> m_fileRegister;
	std::shared_ptr<IndexerStateInfo> m_indexerStateInfo;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxParser.inl"
#endif

#endif	  // CXX_PARSER_H
