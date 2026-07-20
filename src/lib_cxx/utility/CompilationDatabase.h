#ifndef UTILITY_COMPILATION_DATABASE_H
#define UTILITY_COMPILATION_DATABASE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "FilePath.h"
#endif

namespace utility
{
SRCTRL_EXPORT class CompilationDatabase
{
public:
	CompilationDatabase(const FilePath& filePath);

	std::vector<FilePath> getAllHeaderPaths() const;
	std::vector<FilePath> getHeaderPaths() const;
	std::vector<FilePath> getSystemHeaderPaths() const;
	std::vector<FilePath> getFrameworkHeaderPaths() const;

private:
	void init();

	FilePath m_filePath;
	std::vector<FilePath> m_headers;
	std::vector<FilePath> m_systemHeaders;
	std::vector<FilePath> m_frameworkHeaders;
};

}	 // namespace utility

#include "CompilationDatabase.inl"

#endif	  // UTILITY_COMPILATION_DATABASE_H
