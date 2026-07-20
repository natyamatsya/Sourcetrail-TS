// Inline implementations for GeneratePCHAction.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/MultiplexConsumer.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Serialization/ASTWriter.h>
#include "clang_compat/ClangCompat.h"
#include "PreprocessorCallbacks.h"
#endif

inline GeneratePCHAction::GeneratePCHAction(
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache)
	: m_client(client), m_canonicalFilePathCache(canonicalFilePathCache)
{
}

inline bool GeneratePCHAction::shouldEraseOutputFiles()
{
	return false;
}

inline std::unique_ptr<clang::ASTConsumer> GeneratePCHAction::CreateASTConsumer(
	clang::CompilerInstance& CI, llvm::StringRef InFile)
{
	std::string Sysroot;
	if (!ComputeASTConsumerArguments(CI, /*ref*/ Sysroot))
		return nullptr;

	std::string OutputFile;
	std::unique_ptr<llvm::raw_pwrite_stream> OS = CreateOutputFile(CI, InFile, /*ref*/ OutputFile);
	if (!OS)
		return nullptr;

	if (!CI.getFrontendOpts().RelocatablePCH)
		Sysroot.clear();

	auto Buffer = std::make_shared<clang::PCHBuffer>();
	std::vector<std::unique_ptr<clang::ASTConsumer>> Consumers;
	Consumers.push_back(clang_compat::createPchGenerator(
		CI,
		OutputFile,
		Sysroot,
		Buffer,
		true));
	Consumers.push_back(CI.getPCHContainerWriter().CreatePCHContainerGenerator(
		CI, InFile.str(), OutputFile, std::move(OS), Buffer));

	return std::make_unique<clang::MultiplexConsumer>(std::move(Consumers));
}

inline bool GeneratePCHAction::BeginSourceFileAction(clang::CompilerInstance& compiler)
{
	clang::Preprocessor& preprocessor = compiler.getPreprocessor();
	preprocessor.addPPCallbacks(std::make_unique<PreprocessorCallbacks>(
		compiler.getSourceManager(), m_client, m_canonicalFilePathCache));
	return true;
}
