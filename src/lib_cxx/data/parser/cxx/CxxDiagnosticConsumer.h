#ifndef CXX_DIAGNOSTIC_CONSUMER
#define CXX_DIAGNOSTIC_CONSUMER

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#include <clang/Frontend/TextDiagnosticPrinter.h>

class CanonicalFilePathCache;
class ParserClient;
#endif

SRCTRL_EXPORT class CxxDiagnosticConsumer: public clang::TextDiagnosticPrinter
{
public:
	CxxDiagnosticConsumer(
		clang::raw_ostream& os,
		std::shared_ptr<clang::DiagnosticOptions> diags,
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		const FilePath& sourceFilePath,
		bool useLogging = true);

	void BeginSourceFile(
		const clang::LangOptions& langOptions, const clang::Preprocessor* preProcessor) override;
	void EndSourceFile() override;

	void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override;

private:
	std::shared_ptr<clang::DiagnosticOptions> m_diagnosticOptions;
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;

	const FilePath m_sourceFilePath;
	bool m_useLogging;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDiagnosticConsumer.inl"
#endif

#endif	  // CXX_DIAGNOSTIC_CONSUMER
