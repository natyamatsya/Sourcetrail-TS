// Inline implementations for CommentHandler.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "CanonicalFilePathCache.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "utilityClang.h"
#endif

inline CommentHandler::CommentHandler(
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache)
	: m_client(client), m_canonicalFilePathCache(canonicalFilePathCache)
{
}

inline bool CommentHandler::HandleComment(clang::Preprocessor& preprocessor, clang::SourceRange sourceRange)
{
	const clang::SourceManager& sourceManager = preprocessor.getSourceManager();

	const clang::FileID fileId = sourceManager.getFileID(sourceRange.getBegin());
	Id fileSymbolId = m_canonicalFilePathCache.getFileSymbolId(fileId);

	if (fileSymbolId && m_canonicalFilePathCache.isProjectFile(fileId, sourceManager))
	{
		const clang::PresumedLoc& presumedBegin = sourceManager.getPresumedLoc(
			sourceRange.getBegin(), false);
		const clang::PresumedLoc& presumedEnd = sourceManager.getPresumedLoc(
			sourceRange.getEnd(), false);

		m_client.recordComment(ParseLocation(
			fileSymbolId,
			presumedBegin.getLine(),
			presumedBegin.getColumn(),
			presumedEnd.getLine(),
			presumedEnd.getColumn()));
	}

	return false;
}
