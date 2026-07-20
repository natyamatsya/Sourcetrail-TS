// Inline implementations for CanonicalFilePathCache.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cctype>

#include "utilityClang.h"
#endif

namespace canonical_file_path_cache_detail
{
// ASCII lowercasing for path-key comparison. Replaces utility::toLowerCase -- a Qt/locale-based
// helper deliberately excluded from srctrl.utility:string, which this .inl cannot reach from the
// srctrl.cxx:parser purview. Mirrors FilePath::getLowerCase().
inline std::string toLowerCaseAscii(std::string in)
{
	for (char& c: in)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return in;
}
}	 // namespace canonical_file_path_cache_detail

#include <clang/AST/ASTContext.h>
#include <clang/Basic/FileManager.h>


inline CanonicalFilePathCache::CanonicalFilePathCache(std::shared_ptr<FileRegister> fileRegister)
	: m_fileRegister(fileRegister)
{
}

inline std::shared_ptr<FileRegister> CanonicalFilePathCache::getFileRegister() const
{
	return m_fileRegister;
}

inline FilePath CanonicalFilePathCache::getCanonicalFilePath(
	const clang::FileID& fileId, const clang::SourceManager& sourceManager)
{
	if (!fileId.isValid())
	{
		return FilePath();
	}

	auto it = m_fileIdMap.find(fileId);
	if (it != m_fileIdMap.end())
	{
		return it->second;
	}

	FilePath filePath;

	const clang::OptionalFileEntryRef fileEntry = sourceManager.getFileEntryRefForID(fileId);
	if (fileEntry)
	{
		filePath = getCanonicalFilePath(*fileEntry);
		m_fileIdMap.try_emplace(fileId, filePath);
	}

	return filePath;
}

inline FilePath CanonicalFilePathCache::getCanonicalFilePath(const clang::FileEntryRef &entry)
{
	return getCanonicalFilePath(utility::getFileNameOfFileEntry(entry));
}

inline FilePath CanonicalFilePathCache::getCanonicalFilePath(const std::string& path)
{
	const std::string lowercasePath = canonical_file_path_cache_detail::toLowerCaseAscii(path);

	auto it = m_fileStringMap.find(lowercasePath);
	if (it != m_fileStringMap.end())
	{
		return it->second;
	}

	const FilePath canonicalPath = FilePath(path).makeCanonical();
	const std::string lowercaseCanonicalPath = canonical_file_path_cache_detail::toLowerCaseAscii(canonicalPath.str());

	m_fileStringMap.emplace(std::move(lowercasePath), canonicalPath);
	m_fileStringMap.emplace(std::move(lowercaseCanonicalPath), canonicalPath);

	return canonicalPath;
}

inline FilePath CanonicalFilePathCache::getCanonicalFilePath(const Id symbolId)
{
	auto it = m_symbolIdFileIdMap.find(symbolId);
	if (it != m_symbolIdFileIdMap.end())
	{
		auto it2 = m_fileIdMap.find(it->second);
		if (it2 != m_fileIdMap.end())
		{
			return it2->second;
		}
	}

	return FilePath();
}

inline void CanonicalFilePathCache::addFileSymbolId(const clang::FileID& fileId, const FilePath& path, Id symbolId)
{
	m_fileIdSymbolIdMap.try_emplace(fileId, symbolId);
	m_symbolIdFileIdMap.try_emplace(symbolId, fileId);
	m_fileStringSymbolIdMap.emplace(canonical_file_path_cache_detail::toLowerCaseAscii(path.str()), symbolId);
}

inline Id CanonicalFilePathCache::getFileSymbolId(const clang::FileID& fileId)
{
	if (!fileId.isValid())
	{
		return 0;
	}

	auto it = m_fileIdSymbolIdMap.find(fileId);
	if (it != m_fileIdSymbolIdMap.end())
	{
		return it->second;
	}

	return 0;
}

inline Id CanonicalFilePathCache::getFileSymbolId(const clang::FileEntryRef &entry)
{
	return getFileSymbolId(utility::getFileNameOfFileEntry(entry));
}

inline Id CanonicalFilePathCache::getFileSymbolId(const std::string& path)
{
	std::string canonicalPath = canonical_file_path_cache_detail::toLowerCaseAscii(getCanonicalFilePath(path).str());

	auto it = m_fileStringSymbolIdMap.find(canonicalPath);
	if (it != m_fileStringSymbolIdMap.end())
	{
		return it->second;
	}

	return 0;
}

inline FilePath CanonicalFilePathCache::getDeclarationFilePath(const clang::Decl* declaration)
{
	const clang::SourceManager& sourceManager = declaration->getASTContext().getSourceManager();
	const clang::FileID fileId = sourceManager.getFileID(declaration->getBeginLoc());
	const clang::FileEntry* fileEntry = sourceManager.getFileEntryForID(fileId);
	if (fileEntry != nullptr)
	{
		return getCanonicalFilePath(fileId, sourceManager);
	}
	return getCanonicalFilePath(sourceManager.getPresumedLoc(declaration->getBeginLoc()).getFilename());
}

inline std::string CanonicalFilePathCache::getDeclarationFileName(const clang::Decl* declaration)
{
	return getDeclarationFilePath(declaration).fileName();
}

inline bool CanonicalFilePathCache::isProjectFile(
	const clang::FileID& fileId, const clang::SourceManager& sourceManager)
{
	if (!fileId.isValid())
	{
		return false;
	}

	auto it = m_isProjectFileMap.find(fileId);
	if (it != m_isProjectFileMap.end())
	{
		return it->second;
	}

	bool ret = m_fileRegister->hasFilePath(getCanonicalFilePath(fileId, sourceManager));
	m_isProjectFileMap.try_emplace(fileId, ret);
	return ret;
}
