#ifndef SQLITE_INDEX_STORAGE_H
#define SQLITE_INDEX_STORAGE_H

#include <memory>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ErrorInfo.h"
#include "LocationType.h"
#include "LowMemoryStringMap.h"
#include "SqliteDatabaseIndex.h"
#include "SqliteStorage.h"
#include "StorageComponentAccess.h"
#include "StorageEdge.h"
#include "StorageElementComponent.h"
#include "StorageError.h"
#include "StorageFile.h"
#include "StorageLocalSymbol.h"
#include "StorageNode.h"
#include "StorageOccurrence.h"
#include "StorageSourceLocation.h"
#include "StorageSymbol.h"
#include "utility.h"
#include "utilityString.h"

class TextAccess;
class Version;
class SourceLocationCollection;
class SourceLocationFile;

class SqliteIndexStorage: public SqliteStorage
{
public:
	static size_t getStorageVersion();

	enum class StorageModeType
	{
		STORAGE_MODE_READ = 1,
		STORAGE_MODE_WRITE = 2,
		STORAGE_MODE_CLEAR = 4
	};

	explicit SqliteIndexStorage(StorageConnection& connection);

	size_t getStaticVersion() const override;

	void setMode(const StorageModeType mode);

	std::string getProjectSettingsText() const;
	void setProjectSettingsText(std::string text);

	Id addNode(const StorageNodeData& data);
	std::vector<Id> addNodes(const std::vector<StorageNode>& nodes);
	bool addSymbol(const StorageSymbol& data);
	bool addSymbols(const std::vector<StorageSymbol>& symbols);
	bool addFile(const StorageFile& data);
	Id addEdge(const StorageEdgeData& data);
	std::vector<Id> addEdges(const std::vector<StorageEdge>& edges);
	Id addLocalSymbol(const StorageLocalSymbolData& data);
	std::vector<Id> addLocalSymbols(const std::set<StorageLocalSymbol>& symbols);
	Id addSourceLocation(const StorageSourceLocationData& data);
	std::vector<Id> addSourceLocations(const std::vector<StorageSourceLocation>& locations);
	bool addOccurrence(const StorageOccurrence& data);
	bool addOccurrences(const std::vector<StorageOccurrence>& occurrences);
	bool addComponentAccess(const StorageComponentAccess& componentAccess);
	bool addComponentAccesses(const std::vector<StorageComponentAccess>& componentAccesses);
	void addElementComponent(const StorageElementComponent& component);
	void addElementComponents(const std::vector<StorageElementComponent>& components);
	StorageError addError(const StorageErrorData& data);

	void removeElement(Id id);
	void removeElements(const std::vector<Id>& ids);
	void removeOccurrence(const StorageOccurrence& occurrence);
	void removeOccurrences(const std::vector<StorageOccurrence>& occurrences);
	void removeElementsWithoutOccurrences(const std::vector<Id>& elementIds);
	void removeElementsWithLocationInFiles(
		const std::vector<Id>& fileIds, std::function<void(int)> updateStatusCallback);

	void removeAllErrors();

	bool isEdge(Id elementId) const;
	bool isNode(Id elementId) const;
	bool isFile(Id elementId) const;

	StorageEdge getEdgeById(Id edgeId) const;
	StorageEdge getEdgeBySourceTargetType(Id sourceId, Id targetId, int type) const;

	std::vector<StorageEdge> getEdgesBySourceId(Id sourceId) const;
	std::vector<StorageEdge> getEdgesBySourceIds(const std::vector<Id>& sourceIds) const;
	std::vector<StorageEdge> getEdgesByTargetId(Id targetId) const;
	std::vector<StorageEdge> getEdgesByTargetIds(const std::vector<Id>& targetIds) const;
	std::vector<StorageEdge> getEdgesBySourceOrTargetId(Id id) const;

	std::vector<StorageEdge> getEdgesByType(int type) const;
	std::vector<StorageEdge> getEdgesBySourceType(Id sourceId, int type) const;
	std::vector<StorageEdge> getEdgesBySourcesType(const std::vector<Id>& sourceIds, int type) const;
	std::vector<StorageEdge> getEdgesByTargetType(Id targetId, int type) const;
	std::vector<StorageEdge> getEdgesByTargetsType(const std::vector<Id>& targetIds, int type) const;

	StorageNode getNodeById(Id id) const;
	StorageNode getNodeBySerializedName(const std::string& serializedName) const;
	
	std::vector<NodeKind> getAvailableNodeTypes() const;
	std::vector<Edge::EdgeType> getAvailableEdgeTypes() const;

	StorageFile getFileByPath(const std::string& filePath) const;

	std::vector<StorageFile> getFilesByPaths(const std::vector<FilePath>& filePaths) const;
	std::shared_ptr<TextAccess> getFileContentByPath(const std::string& filePath) const;
	std::shared_ptr<TextAccess> getFileContentById(Id fileId) const;

	void setFileIndexed(Id fileId, bool indexed);

	// Per-source-file compile-command hash (flag-aware incremental refresh).
	void setFileCommandHash(const std::string& filePath, const std::string& hash);
	void removeFileCommandHash(const std::string& filePath);
	std::unordered_map<std::string, std::string> getFileCommandHashes() const;

	// generic meta k/v access (shard manifests etc.)
	using SqliteStorage::getMetaValue;
	using SqliteStorage::insertOrUpdateMetaValue;
	void setFileCompleteIfNoError(Id fileId, const std::string& filePath, bool complete);
	void setNodeType(int type, Id nodeId);

	std::shared_ptr<SourceLocationFile> getSourceLocationsForFile(const FilePath& filePath) const;
	std::shared_ptr<SourceLocationFile> getSourceLocationsForLinesInFile(
		const FilePath& filePath, size_t startLine, size_t endLine) const;
	std::shared_ptr<SourceLocationFile> getSourceLocationsOfTypeInFile(
		const FilePath& filePath, LocationType type) const;

	std::shared_ptr<SourceLocationCollection> getSourceLocationsForElementIds(
		const std::vector<Id>& elementIds) const;

	std::vector<StorageOccurrence> getOccurrencesForLocationId(Id locationId) const;
	std::vector<StorageOccurrence> getOccurrencesForLocationIds(const std::vector<Id>& locationIds) const;
	std::vector<StorageOccurrence> getOccurrencesForElementIds(const std::vector<Id>& elementIds) const;

	StorageComponentAccess getComponentAccessByNodeId(Id nodeId) const;
	std::vector<StorageComponentAccess> getComponentAccessesByNodeIds(
		const std::vector<Id>& nodeIds) const;

	std::vector<StorageElementComponent> getElementComponentsByElementIds(
		const std::vector<Id>& elementIds) const;

	std::vector<ErrorInfo> getAllErrorInfos() const;

	template <typename ResultType>
	std::vector<ResultType> getAll() const
	{
		std::vector<ResultType> elements;
		forEach<ResultType>(
			[&elements](ResultType&& element) { elements.emplace_back(std::move(element)); });
		return elements;
	}

	template <typename ResultType>
	ResultType getFirstById(const Id id) const
	{
		if (id != 0)
		{
			std::vector<ResultType> results = getAllByIds<ResultType>({id});
			if (results.size() > 0)
			{
				return results[0];
			}
		}
		return ResultType();
	}

	template <typename ResultType>
	std::vector<ResultType> getAllByIds(const std::vector<Id>& ids) const
	{
		std::vector<ResultType> elements;
		forEachByIds<ResultType>(
			ids, [&elements](ResultType&& element) { elements.emplace_back(std::move(element)); });
		return elements;
	}

	//! Typed full-table scan; specialized per storage type in the .cpp.
	template <typename StorageType>
	void forEach(std::function<void(StorageType&&)> func) const;

	template <typename StorageType, typename T>
	void forEachOfType(T type, std::function<void(StorageType&&)> func) const
	{
		forEachOfTypeImpl<StorageType>(static_cast<int>(type), func);
	}

	//! Typed `WHERE id IN (...)`; no-op on an empty id list. Specialized in the .cpp.
	template <typename StorageType>
	void forEachByIds(const std::vector<Id> ids, std::function<void(StorageType&&)> func) const;

	int getNodeCount() const;
	int getEdgeCount() const;
	int getFileCount() const;
	int getCompletedFileCount() const;
	int getFileLineSum() const;
	int getSourceLocationCount() const;
	int getErrorCount() const;

private:
	static const size_t s_storageVersion;

	struct TempSourceLocation
	{
		TempSourceLocation(
			uint32_t startLine, uint16_t lineDiff, uint16_t startCol, uint16_t endCol, uint8_t type)
			: startLine(startLine), lineDiff(lineDiff), startCol(startCol), endCol(endCol), type(type)
		{
		}

		bool operator<(const TempSourceLocation& other) const
		{
			if (startLine != other.startLine)
			{
				return startLine < other.startLine;
			}
			else if (lineDiff != other.lineDiff)
			{
				return lineDiff < other.lineDiff;
			}
			else if (startCol != other.startCol)
			{
				return startCol < other.startCol;
			}
			else if (endCol != other.endCol)
			{
				return endCol < other.endCol;
			}
			else
			{
				return type < other.type;
			}
		}

		uint32_t startLine;
		uint16_t lineDiff;
		uint16_t startCol;
		uint16_t endCol;
		uint8_t type;
	};

	static std::vector<std::pair<int, SqliteDatabaseIndex>> getIndices();

	void clearTables() override;
	void setupTables() override;
	void setupPrecompiledStatements() override;

	// Mint a fresh element id. With SOURCETRAIL_CLIENT_IDS it comes from an
	// in-process counter (seeded from MAX(id), so the sequence matches SQLite's
	// autoincrement) — no INSERT+lastRowId() round-trip; otherwise it inserts a
	// NULL id and reads it back as before. This is the seam that removes the
	// read-after-write on the hot write path (Phase 2, non-blocking storage).
	Id insertElement();

	//! Typed `WHERE type == ?`; specialized in the .cpp for the types that have one.
	template <typename StorageType>
	void forEachOfTypeImpl(int type, std::function<void(StorageType&&)> func) const;

	LowMemoryStringMap<std::string, Id> m_tempNodeNameIndex;
	LowMemoryStringMap<std::string, Id> m_tempWNodeNameIndex;
	std::map<Id, int> m_tempNodeTypes;
	std::map<StorageEdgeData, Id> m_tempEdgeIndex;
	std::map<std::string, std::map<std::string, Id>> m_tempLocalSymbolIndex;
	std::map<Id, std::map<TempSourceLocation, Id>> m_tempSourceLocationIndices;

	Id::type m_nextElementId = 0;	 // in-process element-id allocator (0 = unseeded)

	// In-memory dedup for the read-back-free write path (SOURCETRAIL_CLIENT_IDS):
	// replaces the SELECT-before-INSERT in addFile/addError. Seeded lazily from
	// the DB on first use, then maintained in-process. Always present (no ABI
	// impact); only consulted under the flag.
	std::unordered_set<std::string> m_knownFilePaths;
	bool m_fileDedupSeeded = false;
	std::map<std::pair<std::string, int>, Id> m_errorDedup;
	bool m_errorDedupSeeded = false;
};

// Full-table scans — one per persisted storage type.
template <>
void SqliteIndexStorage::forEach<StorageEdge>(std::function<void(StorageEdge&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageNode>(std::function<void(StorageNode&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageSymbol>(std::function<void(StorageSymbol&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageFile>(std::function<void(StorageFile&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageLocalSymbol>(
	std::function<void(StorageLocalSymbol&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageSourceLocation>(
	std::function<void(StorageSourceLocation&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageOccurrence>(
	std::function<void(StorageOccurrence&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageComponentAccess>(
	std::function<void(StorageComponentAccess&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageElementComponent>(
	std::function<void(StorageElementComponent&&)> func) const;
template <>
void SqliteIndexStorage::forEach<StorageError>(std::function<void(StorageError&&)> func) const;

// `WHERE id IN (...)` — for the types addressed by element id.
template <>
void SqliteIndexStorage::forEachByIds<StorageEdge>(
	const std::vector<Id> ids, std::function<void(StorageEdge&&)> func) const;
template <>
void SqliteIndexStorage::forEachByIds<StorageNode>(
	const std::vector<Id> ids, std::function<void(StorageNode&&)> func) const;
template <>
void SqliteIndexStorage::forEachByIds<StorageSymbol>(
	const std::vector<Id> ids, std::function<void(StorageSymbol&&)> func) const;
template <>
void SqliteIndexStorage::forEachByIds<StorageFile>(
	const std::vector<Id> ids, std::function<void(StorageFile&&)> func) const;
template <>
void SqliteIndexStorage::forEachByIds<StorageSourceLocation>(
	const std::vector<Id> ids, std::function<void(StorageSourceLocation&&)> func) const;

// `WHERE type == ?` — only edges are filtered by type today.
template <>
void SqliteIndexStorage::forEachOfTypeImpl<StorageEdge>(
	int type, std::function<void(StorageEdge&&)> func) const;

#endif	  // SQLITE_INDEX_STORAGE_H
