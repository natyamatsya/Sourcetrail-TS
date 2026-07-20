#ifndef AST_ACTION_H
#define AST_ACTION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>

#include <clang/Frontend/FrontendAction.h>

#include "CommentHandler.h"

class ParserClient;
class CanonicalFilePathCache;
struct IndexerStateInfo;
#endif

SRCTRL_EXPORT class ASTAction: public clang::ASTFrontendAction
{
public:
	explicit ASTAction(
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		std::shared_ptr<IndexerStateInfo> indexerStateInfo);

protected:
	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
		clang::CompilerInstance& compiler, llvm::StringRef inFile) override;
	bool BeginSourceFileAction(clang::CompilerInstance& compiler) override;

private:
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;
	std::shared_ptr<IndexerStateInfo> m_indexerStateInfo;
	CommentHandler m_commentHandler;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "ASTAction.inl"
#endif

#endif	  // AST_ACTION_H
