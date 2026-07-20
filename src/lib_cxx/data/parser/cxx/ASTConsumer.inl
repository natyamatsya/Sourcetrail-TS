// Inline implementations for ASTConsumer.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "ApplicationSettings.h"
#include "CxxAstVisitor.h"
#endif

inline ASTConsumer::ASTConsumer(
	clang::ASTContext* context,
	clang::Preprocessor* preprocessor,
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	std::shared_ptr<IndexerStateInfo> indexerStateInfo)
{
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();

	const bool isVerbose = appSettings->getLoggingEnabled() &&
		appSettings->getVerboseIndexerLoggingEnabled();

	m_visitor = std::make_shared<CxxAstVisitor>(
		context, preprocessor, client, canonicalFilePathCache, indexerStateInfo, isVerbose);
}

inline void ASTConsumer::HandleTranslationUnit(clang::ASTContext& context)
{
	m_visitor->indexDecl(context.getTranslationUnitDecl());
}
