#include "ASTConsumer.h"

#include "ApplicationSettings.h"
#include "CxxAstVisitor.h"

ASTConsumer::ASTConsumer(
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

void ASTConsumer::HandleTranslationUnit(clang::ASTContext& context)
{
	m_visitor->indexDecl(context.getTranslationUnitDecl());
}
