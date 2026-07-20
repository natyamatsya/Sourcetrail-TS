#ifndef AST_CONSUMER_H
#define AST_CONSUMER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>

class CanonicalFilePathCache;
class CxxAstVisitor;
class ParserClient;
struct IndexerStateInfo;
#endif

SRCTRL_EXPORT class ASTConsumer: public clang::ASTConsumer
{
public:
	explicit ASTConsumer(
		clang::ASTContext* context,
		clang::Preprocessor* preprocessor,
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		std::shared_ptr<IndexerStateInfo> indexerStateInfo);

	~ASTConsumer() override = default;

	void HandleTranslationUnit(clang::ASTContext& context) override;

private:
	std::shared_ptr<CxxAstVisitor> m_visitor;
	std::shared_ptr<IndexerStateInfo> m_indexerStateInfo;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "ASTConsumer.inl"
#endif

#endif	  // AST_CONSUMER_H
