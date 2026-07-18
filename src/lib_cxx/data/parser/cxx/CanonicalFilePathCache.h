#ifndef CANONICAL_FILE_PATH_CACHE_H
#define CANONICAL_FILE_PATH_CACHE_H

#include <string>
#include <unordered_map>

#include <llvm/ADT/DenseMap.h>

#include <clang/AST/Decl.h>
#include <clang/Basic/SourceManager.h>

#include "FilePath.h"
#include "FileRegister.h"
#include "types.h"

class CanonicalFilePathCache
{
public:
	CanonicalFilePathCache(std::shared_ptr<FileRegister> fileRegister);

	std::shared_ptr<FileRegister> getFileRegister() const;

	FilePath getCanonicalFilePath(const clang::FileID& fileId, const clang::SourceManager& sourceManager);
	FilePath getCanonicalFilePath(const clang::FileEntryRef &entry);
	FilePath getCanonicalFilePath(const std::string& path);
	FilePath getCanonicalFilePath(const Id symbolId);

	void addFileSymbolId(const clang::FileID& fileId, const FilePath& path, Id symbolId);
	Id getFileSymbolId(const clang::FileID& fileId);
	Id getFileSymbolId(const clang::FileEntryRef &entry);
	Id getFileSymbolId(const std::string& path);

	FilePath getDeclarationFilePath(const clang::Decl* declaration);
	std::string getDeclarationFileName(const clang::Decl* declaration);

	bool isProjectFile(const clang::FileID& fileId, const clang::SourceManager& sourceManager);

private:
	std::shared_ptr<FileRegister> m_fileRegister;

	// clang::FileID keys are small trivial values: llvm::DenseMap (open-addressed, contiguous, no
	// per-node allocation, O(1)) is the idiomatic Clang choice and far more cache-friendly than a
	// node-based std::map. The Id-keyed reverse map uses std::unordered_map instead — Id is a
	// strong type with std::hash but no llvm::DenseMapInfo, and coupling it to LLVM would leak into
	// all of lib. String keys stay in hash maps.
	llvm::DenseMap<clang::FileID, FilePath> m_fileIdMap;
	std::unordered_map<std::string, FilePath> m_fileStringMap;

	llvm::DenseMap<clang::FileID, Id> m_fileIdSymbolIdMap;
	std::unordered_map<Id, clang::FileID> m_symbolIdFileIdMap;
	std::unordered_map<std::string, Id> m_fileStringSymbolIdMap;

	llvm::DenseMap<clang::FileID, bool> m_isProjectFileMap;
};

#endif	  // CANONICAL_FILE_PATH_CACHE_H
