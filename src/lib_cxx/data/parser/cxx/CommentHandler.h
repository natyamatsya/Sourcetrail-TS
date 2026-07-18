#ifndef COMMENT_HANDLER_H
#define COMMENT_HANDLER_H

#include <clang/Lex/Preprocessor.h>

class CanonicalFilePathCache;
class ParserClient;

class CommentHandler: public clang::CommentHandler
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

#endif	  // COMMENT_HANDLER_H
