#ifndef CLANG_INVOCATION_INFO_H
#define CLANG_INVOCATION_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>


namespace clang::tooling
{
class CompilationDatabase;
}
#endif


SRCTRL_EXPORT struct ClangInvocationInfo
{
	static ClangInvocationInfo getClangInvocationString(
		const clang::tooling::CompilationDatabase* compilationDatabase);

	std::string invocation;
	std::string errors;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "ClangInvocationInfo.inl"
#endif

#endif	  // CLANG_INVOCATION_INFO_H
