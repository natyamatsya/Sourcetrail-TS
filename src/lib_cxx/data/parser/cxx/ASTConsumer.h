#ifndef AST_CONSUMER_H
#define AST_CONSUMER_H

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>

class CanonicalFilePathCache;
class CxxAstVisitor;
class ParserClient;
struct IndexerStateInfo;

class ASTConsumer: public clang::ASTConsumer
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

#endif	  // AST_CONSUMER_H
