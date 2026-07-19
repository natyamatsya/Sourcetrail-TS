#include "CxxParser.h"

#include "ASTAction.h"
#include "ApplicationSettings.h"
#include "CanonicalFilePathCache.h"
#include "ClangInvocationInfo.h"
#include "CxxCompilationDatabaseSingle.h"
#include "CxxDiagnosticConsumer.h"
#include "FilePath.h"
#include "FileRegister.h"
#include "IndexerCommandCxx.h"
#include "ParserClient.h"
#include "ResourcePaths.h"
#include "SingleFrontendActionFactory.h"
#include "TextAccess.h"
#include "ToolChain.h"
#include "logging.h"
#include "utility.h"
#include "utilityClang.h"
#include "utilityString.h"

#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Option/ArgList.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/VirtualFileSystem.h>

namespace
{
// clang derives its driver mode from argv[0]; a name ending in 'cl' selects CL
// mode, which cannot parse the GNU-style flags Sourcetrail assembles (MSVC
// flags are translated to clang spellings up front, see replaceMsvcArguments /
// translateMsvcCompilerFragments). Fall back to the neutral tool name for
// MSVC-style compilers; their resource dir would not resolve anyway.
bool isMsvcStyleCompiler(const std::string& compilerPath)
{
	const std::string fileName = utility::toLowerCase(FilePath(compilerPath).fileName());
	return fileName == "cl.exe" || fileName == "cl" || fileName.starts_with("clang-cl");
}

std::vector<std::string> prependSyntaxOnlyToolArgs(
	const std::string& compilerPath, const std::vector<std::string>& args)
{
	const std::string toolName = (compilerPath.empty() || isMsvcStyleCompiler(compilerPath))
		? std::string(ClangCompiler::TOOL_NAME)
		: compilerPath;
	return utility::concat(std::vector<std::string>({toolName, ClangCompiler::syntaxOnlyOption()}), args);
}

std::vector<std::string> appendFilePath(const std::vector<std::string>& args, llvm::StringRef filePath)
{
	return utility::concat(args, {filePath.str()});
}

// custom implementation of clang::runToolOnCodeWithArgs which also sets our custom DiagnosticConsumer
bool runToolOnCodeWithArgs(
	const std::string& compilerPath,
	clang::DiagnosticConsumer* DiagConsumer,
	std::unique_ptr<clang::FrontendAction> ToolAction,
	const llvm::Twine& Code,
	const std::vector<std::string>& Args,
	const llvm::Twine& FileName = "input.cc",
	const clang::tooling::FileContentMappings& /*VirtualMappedFiles*/ =
		clang::tooling::FileContentMappings())
{
	CxxParser::initializeLLVM();

	llvm::SmallString<16> FileNameStorage;
	llvm::StringRef FileNameRef = FileName.toNullTerminatedStringRef(FileNameStorage);

	llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> OverlayFileSystem(
		new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
	llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
		new llvm::vfs::InMemoryFileSystem);
	OverlayFileSystem->pushOverlay(InMemoryFileSystem);
	llvm::IntrusiveRefCntPtr<clang::FileManager> Files(
		new clang::FileManager(clang::FileSystemOptions(), OverlayFileSystem));

	clang::tooling::ToolInvocation Invocation(
		prependSyntaxOnlyToolArgs(compilerPath, appendFilePath(Args, FileNameRef)), std::move(ToolAction), Files.get());

	llvm::SmallString<1024> CodeStorage;
	llvm::StringRef CodeRef = Code.toNullTerminatedStringRef(CodeStorage);

	InMemoryFileSystem->addFile(FileNameRef, 0, llvm::MemoryBuffer::getMemBufferCopy(CodeRef));

	Invocation.setDiagnosticConsumer(DiagConsumer);

	return Invocation.run();
}
}	 // namespace

std::vector<std::string> CxxParser::getCommandlineArgumentsEssential(
	const std::string& compilerPath, const std::vector<std::string>& compilerFlags)
{
	std::vector<std::string> args;

	// When the compiler has a usable resource directory on disk, inject it
	// explicitly so the Clang driver finds the correct builtin headers
	// (stdarg.h, stdint.h, etc.).  Otherwise fall back to the bundled
	// builtin headers shipped with Sourcetrail.
	std::optional<std::filesystem::path> resourceDir = utility::resolveCompilerResourceDir(compilerPath);
	if (resourceDir)
	{
		args.push_back("-resource-dir");
		args.push_back(resourceDir->string());
	}
	else
	{
		args.push_back(ClangCompiler::systemIncludeOption());
		args.push_back(ResourcePaths::getCxxCompilerHeaderDirectoryPath().str());
	}

	args.push_back(ClangCompiler::noDelayedTemplateParsingOption());
	args.push_back(ClangCompiler::exceptionsOption());
	args.push_back(ClangCompiler::compileOption());
	args.push_back(ClangCompiler::noWarningsOption());
	args.push_back(ClangCompiler::errorLimitOption(0));

	for (const std::string& compilerFlag: compilerFlags)
	{
		args.push_back(compilerFlag);
	}

	return args;
}

void CxxParser::initializeLLVM()
{
	static bool initialized = false;
	if (!initialized)
	{
		llvm::InitializeAllTargets();
		llvm::InitializeAllTargetMCs();
		llvm::InitializeAllAsmPrinters();
		llvm::InitializeAllAsmParsers();
		initialized = true;
	}
}

CxxParser::CxxParser(
	ParserClient& client,
	std::shared_ptr<FileRegister> fileRegister,
	std::shared_ptr<IndexerStateInfo> indexerStateInfo)
	: Parser(client), m_fileRegister(fileRegister), m_indexerStateInfo(indexerStateInfo)
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmParser();
}

void CxxParser::buildIndex(std::shared_ptr<const IndexerCommandCxx> indexerCommand)
{
	clang::tooling::CompileCommand compileCommand;
	compileCommand.Filename = indexerCommand->getSourceFilePath().str();
	compileCommand.Directory = indexerCommand->getWorkingDirectory().str();
	std::vector<std::string> args = indexerCommand->getCompilerFlags();
	if (!args.empty() && !utility::isPrefix("-", args.front()))
	{
		// CDB-style command: drop the compiler executable; the input file is
		// already among the flags (as written in the compilation database).
		args.erase(args.begin());
	}
	else if (const std::string sourcePath = indexerCommand->getSourceFilePath().str();
			 std::find(args.begin(), args.end(), sourcePath) == args.end())
	{
		// Bare-flags command (empty C++ source group): the flags carry no input
		// file — append it, like the pre-merge Empty overload's appendFilePath
		// did. Without this every TU fails with "no input files". (CMake File
		// API commands already push the file themselves; the find() guards them.)
		args.push_back(sourcePath);
	}
	compileCommand.CommandLine = getCommandlineArgumentsEssential(indexerCommand->getCompilerPath(), args);
	compileCommand.CommandLine = prependSyntaxOnlyToolArgs(indexerCommand->getCompilerPath(), compileCommand.CommandLine);

	LOG_INFO(
		"buildIndex: " + indexerCommand->getSourceFilePath().fileName() +
		" compilerPath='" + indexerCommand->getCompilerPath() +
		"' flags=" + std::to_string(compileCommand.CommandLine.size()));

	CxxCompilationDatabaseSingle compilationDatabase(compileCommand);
	runTool(&compilationDatabase, indexerCommand->getSourceFilePath(), indexerCommand->getCompilerPath());
}

void CxxParser::buildIndex(
	const std::string& fileName,
	std::shared_ptr<TextAccess> fileContent,
	std::vector<std::string> compilerFlags)
{
	std::shared_ptr<CanonicalFilePathCache> canonicalFilePathCache =
		std::make_shared<CanonicalFilePathCache>(m_fileRegister);

	std::shared_ptr<CxxDiagnosticConsumer> diagnostics = getDiagnostics(
		FilePath(), *canonicalFilePathCache, false);
	std::unique_ptr<clang::ASTFrontendAction> action = std::make_unique<ASTAction>(
		m_client, *canonicalFilePathCache, m_indexerStateInfo);

	std::vector<std::string> args = getCommandlineArgumentsEssential("", compilerFlags);

	runToolOnCodeWithArgs("", diagnostics.get(), std::move(action), fileContent->getText(), args, fileName);
}

void CxxParser::runTool(
	clang::tooling::CompilationDatabase* compilationDatabase,
	const FilePath& sourceFilePath,
	const std::string& compilerPath)
{
	initializeLLVM();

	clang::tooling::ClangTool tool(*compilationDatabase, std::vector<std::string>(1, sourceFilePath.str()));

	std::shared_ptr<CanonicalFilePathCache> canonicalFilePathCache =
		std::make_shared<CanonicalFilePathCache>(m_fileRegister);

	std::shared_ptr<CxxDiagnosticConsumer> diagnostics = getDiagnostics(
		sourceFilePath, *canonicalFilePathCache, true);

	tool.setDiagnosticConsumer(diagnostics.get());
	tool.clearArgumentsAdjusters();

	// Only inject the bundled builtin headers when the compiler doesn't have
	// a usable resource directory on disk.  When it does, -resource-dir was
	// already injected in getCommandlineArgumentsEssential and the driver
	// finds its own builtin headers.  Injecting the bundled dir on top
	// breaks the stdint.h __has_include_next chain on macOS SDK 26+.
	if (!utility::resolveCompilerResourceDir(compilerPath))
	{
		tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
			ResourcePaths::getCxxCompilerHeaderDirectoryPath().str().c_str(),
			clang::tooling::ArgumentInsertPosition::BEGIN));
		tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
			ClangCompiler::systemIncludeOption().c_str(),
			clang::tooling::ArgumentInsertPosition::BEGIN));
	}

	ClangInvocationInfo info;
	if (LogManager::getInstance()->getLoggingEnabled())
	{
		info = ClangInvocationInfo::getClangInvocationString(compilationDatabase);
		LOG_INFO(
			"Clang Invocation: " +
			info.invocation.substr(
				0,
				ApplicationSettings::getInstance()->getVerboseIndexerLoggingEnabled() ? std::string::npos
																					  : 20000));

		if (!info.errors.empty())
		{
			LOG_INFO("Clang Invocation errors: " + info.errors);
		}
	}

	clang::ASTFrontendAction* action = new ASTAction(
		m_client, *canonicalFilePathCache, m_indexerStateInfo);
	tool.run(new SingleFrontendActionFactory(action));

	if (!m_client.hasContent())
	{
		if (info.invocation.empty())
		{
			info = ClangInvocationInfo::getClangInvocationString(compilationDatabase);
		}

		if (!info.errors.empty())
		{
			Id fileId = m_client.recordFile(sourceFilePath, true);
			m_client.recordError(
				"Clang Invocation errors: " + info.errors,
				true,
				true,
				sourceFilePath,
				ParseLocation(fileId, 1, 1));
		}
	}
}

std::shared_ptr<CxxDiagnosticConsumer> CxxParser::getDiagnostics(
	const FilePath& sourceFilePath,
	CanonicalFilePathCache& canonicalFilePathCache,
	bool logErrors) const
{
	auto options = std::make_shared<clang::DiagnosticOptions>();
	options->ShowCarets = false;
	options->ShowFixits = false;
	options->ShowSourceRanges = false;
	options->SnippetLineLimit = 0;
	return std::make_shared<CxxDiagnosticConsumer>(
		llvm::errs(), options, m_client, canonicalFilePathCache, sourceFilePath, logErrors);
}
