// Inline implementations for CxxDiagnosticConsumer.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Basic/SourceManager.h>
#include <clang/Tooling/Tooling.h>
#include "CanonicalFilePathCache.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "utilityClang.h"
#include "utilityString.h"
#endif

inline CxxDiagnosticConsumer::CxxDiagnosticConsumer(
	clang::raw_ostream& os,
	std::shared_ptr<clang::DiagnosticOptions> diags,
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	const FilePath& sourceFilePath,
	bool useLogging)
	: clang::TextDiagnosticPrinter(os, *diags)
	, m_diagnosticOptions(diags)
	, m_client(client)
	, m_canonicalFilePathCache(canonicalFilePathCache)
	, m_sourceFilePath(sourceFilePath)
	, m_useLogging(useLogging)
{
}

inline void CxxDiagnosticConsumer::BeginSourceFile(
	const clang::LangOptions& langOptions, const clang::Preprocessor* preProcessor)
{
	if (m_useLogging)
	{
		clang::TextDiagnosticPrinter::BeginSourceFile(langOptions, preProcessor);
	}
}

inline void CxxDiagnosticConsumer::EndSourceFile()
{
	if (m_useLogging)
	{
		clang::TextDiagnosticPrinter::EndSourceFile();
	}
}

inline void CxxDiagnosticConsumer::HandleDiagnostic(
	clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info)
{
	if (m_useLogging)
	{
		clang::TextDiagnosticPrinter::HandleDiagnostic(level, info);
	}

	if (level >= clang::DiagnosticsEngine::Error)
	{
		llvm::SmallString<100> messageStr;
		info.FormatDiagnostic(messageStr);
		std::string message = messageStr.str().str();

		if (message ==
			"MS-style inline assembly is not available: Unable to find target for this triple (no "
			"targets are registered)")
		{
			return;
		}
		if (utility::isPrefix("unknown argument:", message))
		{
			return;
		}

		Id fileId = 0;
		FilePath filePath;
		size_t lineNumber = 0;
		size_t columnNumber = 0;
		if (info.getLocation().isValid() && info.hasSourceManager())
		{
			const clang::SourceManager& sourceManager = info.getSourceManager();

			clang::SourceLocation loc = sourceManager.getExpansionLoc(info.getLocation());
			if (loc.isInvalid())
			{
				loc = info.getLocation();
			}

			clang::FileID clangFileId = sourceManager.getFileID(loc);
			if (sourceManager.getFileEntryForID(clangFileId) != nullptr)
			{
				ParseLocation location = utility::getParseLocation(
					loc, sourceManager, nullptr, m_canonicalFilePathCache);
				fileId = location.fileId;
				filePath = m_canonicalFilePathCache.getCanonicalFilePath(fileId);
				lineNumber = location.startLineNumber;
				columnNumber = location.startColumnNumber;
			}
			else
			{
				const clang::OptionalFileEntryRef fileEntry = sourceManager.getFileEntryRefForID(sourceManager.getMainFileID());
				if (fileEntry)
				{
					filePath = m_canonicalFilePathCache.getCanonicalFilePath(*fileEntry);
					fileId = m_client.recordFile(
						filePath, false /*keeps the "indexed" state if the file already exists*/);
					lineNumber = 1;
					columnNumber = 1;
				}
			}
		}
		else
		{
			filePath = m_sourceFilePath;
			fileId = m_client.recordFile(
				filePath, false /*keeps the "indexed" state if the file already exists*/);
			lineNumber = 1;
			columnNumber = 1;
		}

		if (fileId != 0)
		{
			m_client.recordError(
				message,
				level == clang::DiagnosticsEngine::Fatal,
				m_canonicalFilePathCache.getFileRegister()->hasFilePath(filePath),
				m_sourceFilePath,
				ParseLocation(fileId, lineNumber, columnNumber));
		}
	}
}
