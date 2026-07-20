// Inline implementations for ASTAction.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Frontend/CompilerInstance.h>
#include "ASTConsumer.h"
#include "PreprocessorCallbacks.h"
#endif

inline ASTAction::ASTAction(
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	std::shared_ptr<IndexerStateInfo> indexerStateInfo)
	: m_client(client)
	, m_canonicalFilePathCache(canonicalFilePathCache)
	, m_indexerStateInfo(indexerStateInfo)
	, m_commentHandler(client, canonicalFilePathCache)
{
}

inline std::unique_ptr<clang::ASTConsumer> ASTAction::CreateASTConsumer(
	clang::CompilerInstance& compiler, llvm::StringRef  /*inFile*/)
{
	return std::unique_ptr<clang::ASTConsumer>(new ASTConsumer(
		&compiler.getASTContext(),
		&compiler.getPreprocessor(),
		m_client,
		m_canonicalFilePathCache,
		m_indexerStateInfo));
}

inline bool ASTAction::BeginSourceFileAction(clang::CompilerInstance& compiler)
{
	clang::Preprocessor& preprocessor = compiler.getPreprocessor();
	preprocessor.addPPCallbacks(std::make_unique<PreprocessorCallbacks>(
		compiler.getSourceManager(), m_client, m_canonicalFilePathCache));
	preprocessor.addCommentHandler(&m_commentHandler);
	return true;
}
