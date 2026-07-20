#ifndef COMMENT_HANDLER_H
#define COMMENT_HANDLER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Lex/Preprocessor.h>

class CanonicalFilePathCache;
class ParserClient;
#endif

SRCTRL_EXPORT class CommentHandler: public clang::CommentHandler
{
public:
	CommentHandler(
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache);

	~CommentHandler() override = default;

	bool HandleComment(clang::Preprocessor& preprocessor, clang::SourceRange sourceRange) override;

private:
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "CommentHandler.inl"
#endif

#endif	  // COMMENT_HANDLER_H
