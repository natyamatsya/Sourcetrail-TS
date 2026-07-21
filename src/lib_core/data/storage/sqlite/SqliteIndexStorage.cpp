// Consume sqlpp23 as a C++20 module (import) under SOURCETRAIL_SQLPP23_MODULES; the import must precede
// every declaration, so it leads the TU. The query DSL comes from the import; the raw connection wrapper
// (BorrowedSqliteConnection.h) stays a plain #include -- it derives from sqlpp23 internals
// (common_connection/connection_base) the module doesn't export. See DESIGN_STORAGE_MODULARIZATION.md §3.
#ifdef SRCTRL_SQLPP23_MODULE
import sqlpp23.core;
import sqlpp23.sqlite3;
#endif

#include "SqliteIndexStorage.h"

#include <utility>

#ifndef SRCTRL_SQLPP23_MODULE
#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#endif

#include "BorrowedSqliteConnection.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#endif
#include "IndexTables.h"
#ifndef SRCTRL_MODULE_BUILD
#include "LocationType.h"
#include "SourceLocationCollection.h"
#include "SourceLocationFile.h"
#include "TextAccess.h"
#endif
#include "logging.h"
#include "utilityFilePath.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.data;
import srctrl.file;
import srctrl.utility;
#endif


const size_t SqliteIndexStorage::s_storageVersion = 27;

namespace
{
std::pair<std::string, std::string> splitLocalSymbolName(const std::string& name)
{
	size_t pos = name.find_last_of('<');
	if (pos == std::string::npos || name.back() != '>')
	{
		return std::make_pair("", "");
	}

	return std::make_pair(name.substr(0, pos), name.substr(pos + 1, name.size() - pos - 2));
}

// Table instances (stateless). Models generated from index.sql; see
// IndexTables.h for the [PK-is-FK] manual corrections.
constexpr idx::Element elementTable;
constexpr idx::ElementComponent elementComponentTable;
constexpr idx::Edge edgeTable;
constexpr idx::Node nodeTable;
constexpr idx::Symbol symbolTable;
constexpr idx::File fileTable;
constexpr idx::Filecontent filecontentTable;
constexpr idx::FileCommandHash fileCommandHashTable;
constexpr idx::LocalSymbol localSymbolTable;
constexpr idx::SourceLocation sourceLocationTable;
constexpr idx::Occurrence occurrenceTable;
constexpr idx::ComponentAccess componentAccessTable;
constexpr idx::NodeAttribute nodeAttributeTable;
constexpr idx::Error errorTable;

// Aggregate result aliases (SQLPP_CREATE_NAME_TAG must live at namespace scope).
SQLPP_CREATE_NAME_TAG(maxId);
SQLPP_CREATE_NAME_TAG(cnt);
SQLPP_CREATE_NAME_TAG(total);

// Read a nullable text column into std::string ("" when NULL) — matches the old
// getStringField(col, "") default.
template <typename Field>
std::string fieldText(const Field& field)
{
	return field.has_value() ? std::string(field.value()) : std::string();
}

std::vector<Id::type> toI64(const std::vector<Id>& ids)
{
	std::vector<Id::type> result;
	result.reserve(ids.size());
	for (const Id id: ids)
	{
		result.push_back(static_cast<Id::type>(id));
	}
	return result;
}

// Row -> storage-type mappers. Each returns nullopt for rows the raw layer's
// validity checks would have skipped, keeping filter semantics identical.
constexpr auto edgeFromRow = [](const auto& row) -> std::optional<StorageEdge> {
	const Id id = Id(row.id);
	const auto type = static_cast<int>(row.type);
	if (id != 0 && type != -1)
	{
		return StorageEdge(id, intToEnum<Edge::EdgeType>(type), Id(row.sourceNodeId), Id(row.targetNodeId));
	}
	return std::nullopt;
};

constexpr auto nodeFromRow = [](const auto& row) -> std::optional<StorageNode> {
	const Id id = Id(row.id);
	const auto type = static_cast<int>(row.type);
	if (id != 0 && type != -1)
	{
		return StorageNode(id, intToEnum<NodeKind>(type), fieldText(row.serializedName),
			static_cast<NodeModifierMask>(row.modifiers));
	}
	return std::nullopt;
};

constexpr auto symbolFromRow = [](const auto& row) -> std::optional<StorageSymbol> {
	const Id id = Id(row.id);
	if (id != 0)
	{
		return StorageSymbol(id, intToEnum<DefinitionKind>(static_cast<int>(row.definitionKind)));
	}
	return std::nullopt;
};

constexpr auto fileFromRow = [](const auto& row) -> std::optional<StorageFile> {
	const Id id = Id(row.id);
	if (id != 0)
	{
		return StorageFile(
			id,
			fieldText(row.path),
			fieldText(row.language),
			fieldText(row.modificationTime),
			row.indexed.value_or(0) != 0,
			row.complete.value_or(0) != 0);
	}
	return std::nullopt;
};

constexpr auto localSymbolFromRow = [](const auto& row) -> std::optional<StorageLocalSymbol> {
	const Id id = Id(row.id);
	if (id != 0)
	{
		return StorageLocalSymbol(id, fieldText(row.name));
	}
	return std::nullopt;
};

constexpr auto sourceLocationFromRow = [](const auto& row) -> std::optional<StorageSourceLocation> {
	const Id id = Id(row.id);
	const Id fileNodeId = Id(row.fileNodeId.value_or(0));
	const auto startLine = static_cast<int>(row.startLine.value_or(-1));
	const auto startCol = static_cast<int>(row.startColumn.value_or(-1));
	const auto endLine = static_cast<int>(row.endLine.value_or(-1));
	const auto endCol = static_cast<int>(row.endColumn.value_or(-1));
	const auto type = static_cast<int>(row.type.value_or(-1));

	if (id != 0 && fileNodeId != 0 && startLine != -1 && startCol != -1 && endLine != -1 &&
		endCol != -1 && type != -1)
	{
		return StorageSourceLocation(
			id, fileNodeId, startLine, startCol, endLine, endCol, intToEnum<LocationType>(type));
	}
	return std::nullopt;
};

constexpr auto occurrenceFromRow = [](const auto& row) -> std::optional<StorageOccurrence> {
	const Id elementId = Id(row.elementId);
	const Id sourceLocationId = Id(row.sourceLocationId);
	if (elementId != 0 && sourceLocationId != 0)
	{
		return StorageOccurrence(elementId, sourceLocationId);
	}
	return std::nullopt;
};

constexpr auto componentAccessFromRow = [](const auto& row) -> std::optional<StorageComponentAccess> {
	const Id nodeId = Id(row.nodeId);
	const auto type = static_cast<int>(row.type);
	if (nodeId != 0 && type != -1)
	{
		return StorageComponentAccess(nodeId, intToEnum<AccessKind>(type));
	}
	return std::nullopt;
};

constexpr auto nodeAttributeFromRow = [](const auto& row) -> std::optional<StorageNodeAttribute> {
	const Id nodeId = Id(row.nodeId);
	const auto key = static_cast<int>(row.key);
	if (nodeId != 0)
	{
		return StorageNodeAttribute(
			nodeId, intToEnum<NodeAttributeKind>(key), fieldText(row.value));
	}
	return std::nullopt;
};

constexpr auto elementComponentFromRow = [](const auto& row) -> std::optional<StorageElementComponent> {
	const Id elementId = Id(row.elementId.value_or(0));
	const auto type = static_cast<int>(row.type.value_or(-1));
	if (elementId != 0 && type != -1)
	{
		return StorageElementComponent(elementId, intToEnum<ElementComponentKind>(type), fieldText(row.data));
	}
	return std::nullopt;
};

constexpr auto errorFromRow = [](const auto& row) -> std::optional<StorageError> {
	const Id id = Id(row.id);
	if (id != 0)
	{
		return StorageError(
			id,
			fieldText(row.message),
			fieldText(row.translationUnit),
			row.fatal != 0,
			row.indexed != 0);
	}
	return std::nullopt;
};

// Typed full-table scan / filtered scan runners shared by the forEach family.
template <typename StorageType, typename Table, typename Mapper>
void forEachRowAll(
	sourcetrail::storage::BorrowedSqliteConnection& db,
	const Table& table,
	const Mapper& mapper,
	const std::function<void(StorageType&&)>& func)
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db(select(all_of(table)).from(table)))
		{
			if (std::optional<StorageType> element = mapper(row))
			{
				func(std::move(*element));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

template <typename StorageType, typename Table, typename Mapper, typename Condition>
void forEachRowWhere(
	sourcetrail::storage::BorrowedSqliteConnection& db,
	const Table& table,
	const Mapper& mapper,
	const Condition& condition,
	const std::function<void(StorageType&&)>& func)
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db(select(all_of(table)).from(table).where(condition)))
		{
			if (std::optional<StorageType> element = mapper(row))
			{
				func(std::move(*element));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

// Multi-row typed INSERTs execute directly (values are inlined and escaped by
// sqlpp23), so the old 999-bound-parameter batching is moot; chunking merely
// bounds the length of a single serialized statement.
constexpr size_t kInsertChunkRows = 500;

template <typename Item, typename MakeInsert, typename AddRow>
bool insertInChunks(
	sourcetrail::storage::BorrowedSqliteConnection& db,
	const std::vector<Item>& items,
	const MakeInsert& makeInsert,
	const AddRow& addRow)
{
	try
	{
		size_t i = 0;
		while (i < items.size())
		{
			auto insert = makeInsert();
			const size_t end = std::min(items.size(), i + kInsertChunkRows);
			for (; i < end; i++)
			{
				addRow(insert, items[i]);
			}
			db(insert);
		}
		return true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
		return false;
	}
}
}	 // namespace

size_t SqliteIndexStorage::getStorageVersion()
{
	return s_storageVersion;
}

SqliteIndexStorage::SqliteIndexStorage(StorageConnection& connection): SqliteStorage(connection)
{
}

size_t SqliteIndexStorage::getStaticVersion() const
{
	return s_storageVersion;
}

void SqliteIndexStorage::setMode(const StorageModeType mode)
{
	m_tempNodeNameIndex.clear();
	m_tempWNodeNameIndex.clear();
	m_tempNodeTypes.clear();
	m_tempNodeModifiers.clear();
	m_tempEdgeIndex.clear();
	m_tempLocalSymbolIndex.clear();
	m_tempSourceLocationIndices.clear();

	std::vector<std::pair<int, SqliteDatabaseIndex>> indices = getIndices();
	for (size_t i = 0; i < indices.size(); i++)
	{
		if (indices[i].first & std::to_underlying(mode))
		{
			indices[i].second.createOnDatabase(m_database);
		}
		else
		{
			indices[i].second.removeFromDatabase(m_database);
		}
	}
}

std::string SqliteIndexStorage::getProjectSettingsText() const
{
	return getMetaValue("project_settings");
}

void SqliteIndexStorage::setProjectSettingsText(std::string text)
{
	insertOrUpdateMetaValue("project_settings", text);
}

Id SqliteIndexStorage::insertElement()
{
	using namespace sqlpp;
#ifdef SOURCETRAIL_CLIENT_IDS
	// Phase 2: allocate the id in-process instead of reading it back from the DB.
	// Seeded from MAX(id) so the sequence reproduces SQLite's autoincrement
	// exactly — a full client-id index is byte-identical to an autoincrement one.
	if (m_nextElementId == 0)
	{
		m_nextElementId = 1;
		// MAX(id) is NULL on an empty table -> optional; fall back to 0 -> counter 1.
		for (const auto& row: db()(select(max(elementTable.id).as(maxId)).from(elementTable)))
		{
			if (row.maxId)
			{
				m_nextElementId = static_cast<Id::type>(row.maxId.value()) + 1;
			}
		}
	}
	const Id id = Id(m_nextElementId);
	++m_nextElementId;
	db()(insert_into(elementTable).set(elementTable.id = static_cast<Id::type>(id)));
	return id;
#else
	const auto result = db()(insert_into(elementTable).default_values());
	return Id(result.last_insert_id);
#endif
}

Id SqliteIndexStorage::addNode(const StorageNodeData& data)
{
	std::vector<Id> ids = addNodes({StorageNode(0, data)});
	return ids.size() ? ids[0] : 0;
}

std::vector<Id> SqliteIndexStorage::addNodes(const std::vector<StorageNode>& nodes)
{
	if (m_tempNodeNameIndex.empty() && m_tempWNodeNameIndex.empty())
	{
		forEach<StorageNode>([this](StorageNode&& node) {
			std::string name = node.serializedName;
			if (name.size() != node.serializedName.size())
			{
				m_tempWNodeNameIndex.add(node.serializedName, node.id);
			}
			else
			{
				m_tempNodeNameIndex.add(name, node.id);
			}

			m_tempNodeTypes.emplace(node.id, node.type);
			if (node.modifiers != 0)
			{
				m_tempNodeModifiers.emplace(node.id, node.modifiers);
			}
		});
	}

	std::vector<Id> nodeIds(nodes.size(), 0);
	std::vector<StorageNode> nodesToInsert;
	for (size_t i = 0; i < nodes.size(); i++)
	{
		const StorageNodeData& data = nodes[i];
		std::string name = data.serializedName;
		{
			Id nodeId;
			if (name.size() != data.serializedName.size())
			{
				nodeId = m_tempWNodeNameIndex.find(data.serializedName);
			}
			else
			{
				nodeId = m_tempNodeNameIndex.find(name);
			}

			if (nodeId)
			{
				auto it = m_tempNodeTypes.find(nodeId);
				if (it != m_tempNodeTypes.end() && it->second < data.type)
				{
					setNodeType(data.type, nodeId);
					m_tempNodeTypes[nodeId] = data.type;
				}

				// Merge modifiers by OR: the same symbol is recorded by many TUs, and only some
				// see the flag-bearing declaration (e.g. NODE_MODIFIER_EXPORTED exists only in
				// the module-wrapper parse; classic TUs never see `export`). Without the merge,
				// whichever TU inserted the node first silently wins and the flag is lost.
				if (data.modifiers != 0)
				{
					const int previous = m_tempNodeModifiers[nodeId];
					const int merged = previous | static_cast<int>(data.modifiers);
					if (merged != previous)
					{
						setNodeModifiers(merged, nodeId);
						m_tempNodeModifiers[nodeId] = merged;
					}
				}

				nodeIds[i] = nodeId;
			}
			else
			{
				const Id id = insertElement();

				nodesToInsert.emplace_back(id, data);
				nodeIds[i] = id;

				if (name.size() != data.serializedName.size())
				{
					m_tempWNodeNameIndex.add(data.serializedName, id);
				}
				else
				{
					m_tempNodeNameIndex.add(name, id);
				}
				m_tempNodeTypes.emplace(id, data.type);
				if (data.modifiers != 0)
				{
					m_tempNodeModifiers.emplace(id, static_cast<int>(data.modifiers));
				}
			}
		}
	}

	if (nodesToInsert.size())
	{
		using namespace sqlpp;
		insertInChunks(
			db(),
			nodesToInsert,
			[] { return insert_into(nodeTable).columns(nodeTable.id, nodeTable.type, nodeTable.serializedName, nodeTable.modifiers); },
			[](auto& insert, const StorageNode& node) {
				insert.add_values(
					nodeTable.id = static_cast<Id::type>(node.id),
					nodeTable.type = static_cast<int>(node.type),
					nodeTable.serializedName = node.serializedName,
					nodeTable.modifiers = static_cast<int>(node.modifiers));
			});
	}

	return nodeIds;
}

bool SqliteIndexStorage::addSymbol(const StorageSymbol& data)
{
	return addSymbols({data});
}

bool SqliteIndexStorage::addSymbols(const std::vector<StorageSymbol>& symbols)
{
	using namespace sqlpp;
	return insertInChunks(
		db(),
		symbols,
		[] {
			return ::sqlpp::sqlite3::insert_or_ignore().into(symbolTable).columns(
				symbolTable.id, symbolTable.definitionKind);
		},
		[](auto& insert, const StorageSymbol& symbol) {
			insert.add_values(
				symbolTable.id = static_cast<Id::type>(symbol.id),
				symbolTable.definitionKind = static_cast<int>(symbol.definitionKind));
		});
}

bool SqliteIndexStorage::addFile(const StorageFile& data)
{
	using namespace sqlpp;
#ifdef SOURCETRAIL_CLIENT_IDS
	// Read-back-free dedup: consult an in-process path set instead of a
	// SELECT-by-path. insert().second is false when the path is already present.
	if (!m_fileDedupSeeded)
	{
		try
		{
			for (const auto& row: db()(select(fileTable.path).from(fileTable)))
			{
				m_knownFilePaths.insert(fieldText(row.path));
			}
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
		}
		m_fileDedupSeeded = true;
	}
	if (!m_knownFilePaths.insert(data.filePath).second)
	{
		return false;
	}
#else
	if (getFileByPath(data.filePath).id != 0)
	{
		return false;
	}
#endif

	FilePath filePath(data.filePath);

	std::string modificationTime(data.modificationTime);
	if (modificationTime.empty())
	{
		modificationTime = FileSystem::getFileInfoForPath(filePath).lastWriteTime.toString();
	}

	std::shared_ptr<TextAccess> content;
	int lineCount = 0;
	if (data.indexed)
	{
		content = TextAccess::createFromFile(filePath);
		lineCount = content->getLineCount();
	}

	bool success = false;
	try
	{
		db()(insert_into(fileTable).set(
			fileTable.id = static_cast<Id::type>(data.id),
			fileTable.path = data.filePath,
			fileTable.language = data.languageIdentifier,
			fileTable.modificationTime = modificationTime,
			fileTable.indexed = static_cast<int>(data.indexed),
			fileTable.complete = static_cast<int>(data.complete),
			fileTable.lineCount = lineCount));
		success = true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}

	if (success && content)
	{
		try
		{
			db()(insert_into(filecontentTable).set(
				filecontentTable.id = static_cast<Id::type>(data.id),
				filecontentTable.content = content->getText()));
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
			success = false;
		}
	}

	return success;
}

Id SqliteIndexStorage::addEdge(const StorageEdgeData& data)
{
	std::vector<Id> ids = addEdges({StorageEdge(0, data)});
	return ids.size() ? ids[0] : 0;
}

std::vector<Id> SqliteIndexStorage::addEdges(const std::vector<StorageEdge>& edges)
{
	if (m_tempEdgeIndex.empty())
	{
		forEach<StorageEdge>([this](StorageEdge&& edge) {
			m_tempEdgeIndex.emplace(
				StorageEdgeData(edge.type, edge.sourceNodeId, edge.targetNodeId),
				edge.id);
		});
	}

	std::vector<Id> edgeIds(edges.size(), 0);
	std::vector<StorageEdge> edgesToInsert;
	for (size_t i = 0; i < edges.size(); i++)
	{
		const StorageEdge& data = edges[i];
		const auto it = m_tempEdgeIndex.find(data);
		if (it != m_tempEdgeIndex.end())
		{
			edgeIds[i] = it->second;
		}
		else
		{
			const Id id = insertElement();

			edgeIds[i] = id;
			edgesToInsert.emplace_back(id, data);

			m_tempEdgeIndex.emplace(data, id);
		}
	}

	if (edgesToInsert.size())
	{
		using namespace sqlpp;
		insertInChunks(
			db(),
			edgesToInsert,
			[] {
				return insert_into(edgeTable).columns(
					edgeTable.id, edgeTable.type, edgeTable.sourceNodeId, edgeTable.targetNodeId);
			},
			[](auto& insert, const StorageEdge& edge) {
				insert.add_values(
					edgeTable.id = static_cast<Id::type>(edge.id),
					edgeTable.type = static_cast<int>(edge.type),
					edgeTable.sourceNodeId = static_cast<Id::type>(edge.sourceNodeId),
					edgeTable.targetNodeId = static_cast<Id::type>(edge.targetNodeId));
			});
	}

	return edgeIds;
}

Id SqliteIndexStorage::addLocalSymbol(const StorageLocalSymbolData& data)
{
	std::vector<Id> ids = addLocalSymbols({StorageLocalSymbol(0, data)});
	return ids.size() ? ids[0] : 0;
}

std::vector<Id> SqliteIndexStorage::addLocalSymbols(const std::set<StorageLocalSymbol>& symbols)
{
	if (m_tempLocalSymbolIndex.empty())
	{
		forEach<StorageLocalSymbol>([this](StorageLocalSymbol&& localSymbol) {
			std::pair<std::string, std::string> name = splitLocalSymbolName(localSymbol.name);
			if (name.second.size())
			{
				m_tempLocalSymbolIndex[name.first].emplace(
					name.second, localSymbol.id);
			}
		});
	}

	std::vector<Id> symbolIds(symbols.size(), 0);
	std::vector<StorageLocalSymbol> symbolsToInsert;
	auto it = symbols.begin();
	for (size_t i = 0; i < symbols.size(); i++)
	{
		const StorageLocalSymbol& data = *it;
		std::pair<std::string, std::string> name = splitLocalSymbolName(data.name);
		if (name.second.size())
		{
			auto it = m_tempLocalSymbolIndex.find(name.first);
			if (it != m_tempLocalSymbolIndex.end())
			{
				auto it2 = it->second.find(name.second);
				if (it2 != it->second.end())
				{
					symbolIds[i] = it2->second;
				}
			}
		}

		if (!symbolIds[i])
		{
			const Id id = insertElement();

			symbolIds[i] = id;
			symbolsToInsert.emplace_back(id, data);
			if (name.second.size())
			{
				m_tempLocalSymbolIndex[name.first].emplace(name.second, id);
			}
		}

		it++;
	}

	if (symbolsToInsert.size())
	{
		using namespace sqlpp;
		insertInChunks(
			db(),
			symbolsToInsert,
			[] { return insert_into(localSymbolTable).columns(localSymbolTable.id, localSymbolTable.name); },
			[](auto& insert, const StorageLocalSymbol& symbol) {
				insert.add_values(
					localSymbolTable.id = static_cast<Id::type>(symbol.id),
					localSymbolTable.name = symbol.name);
			});
	}

	return symbolIds;
}

Id SqliteIndexStorage::addSourceLocation(const StorageSourceLocationData& data)
{
	std::vector<Id> ids = addSourceLocations({StorageSourceLocation(0, data)});
	return ids.size() ? ids[0] : 0;
}

std::vector<Id> SqliteIndexStorage::addSourceLocations(const std::vector<StorageSourceLocation>& locations)
{
	using namespace sqlpp;

	if (m_tempSourceLocationIndices.empty())
	{
		forEach<StorageSourceLocation>([this](StorageSourceLocation&& loc) {
			auto &index = m_tempSourceLocationIndices[loc.fileNodeId];
			index.emplace(
				TempSourceLocation(
					static_cast<uint32_t>(loc.startLine),
					static_cast<uint16_t>(loc.endLine - loc.startLine),
					static_cast<uint16_t>(loc.startCol),
					static_cast<uint16_t>(loc.endCol),
					static_cast<uint8_t>(loc.type)),
				loc.id);
		});
	}

	std::vector<Id> locationIds(locations.size(), 0);
	std::vector<StorageSourceLocationData> locationsToInsert;

	// source_location.id is a real auto rowid (id omitted below); the next ids are
	// derived from the current MAX(id) exactly like the raw MAX(rowid) read did.
	size_t lastRowId = 0;
	try
	{
		for (const auto& row: db()(select(max(sourceLocationTable.id).as(maxId)).from(sourceLocationTable)))
		{
			lastRowId = static_cast<size_t>(row.maxId.value_or(0));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}

	for (size_t i = 0; i < locations.size(); i++)
	{
		const StorageSourceLocation& data = locations[i];
		const TempSourceLocation tempLoc(
			static_cast<uint32_t>(data.startLine),
			static_cast<uint16_t>(data.endLine - data.startLine),
			static_cast<uint16_t>(data.startCol),
			static_cast<uint16_t>(data.endCol),
			static_cast<uint8_t>(data.type));

		auto &index = m_tempSourceLocationIndices[data.fileNodeId];
		const auto it = index.find(tempLoc);
		if (it != index.end())
		{
			locationIds[i] = it->second;
		}
		else
		{
			insertElement();  // mint the element id; source_location.id is derived below
			Id id = lastRowId + 1 + locationsToInsert.size();

			locationIds[i] = id;
			index.emplace(tempLoc, id);

			locationsToInsert.emplace_back(data);
		}
	}

	if (locationsToInsert.size())
	{
		insertInChunks(
			db(),
			locationsToInsert,
			[] {
				return insert_into(sourceLocationTable)
					.columns(
						sourceLocationTable.fileNodeId,
						sourceLocationTable.startLine,
						sourceLocationTable.startColumn,
						sourceLocationTable.endLine,
						sourceLocationTable.endColumn,
						sourceLocationTable.type);
			},
			[](auto& insert, const StorageSourceLocationData& location) {
				insert.add_values(
					sourceLocationTable.fileNodeId = static_cast<Id::type>(location.fileNodeId),
					sourceLocationTable.startLine = static_cast<int>(location.startLine),
					sourceLocationTable.startColumn = static_cast<int>(location.startCol),
					sourceLocationTable.endLine = static_cast<int>(location.endLine),
					sourceLocationTable.endColumn = static_cast<int>(location.endCol),
					sourceLocationTable.type = static_cast<int>(location.type));
			});
	}

	return locationIds;
}

bool SqliteIndexStorage::addOccurrence(const StorageOccurrence& data)
{
	return addOccurrences({data});
}

bool SqliteIndexStorage::addOccurrences(const std::vector<StorageOccurrence>& occurrences)
{
	using namespace sqlpp;
	// INSERT OR IGNORE: the composite PK (element_id, source_location_id) makes
	// re-inserting an existing pair a silent no-op (matches the raw layer).
	return insertInChunks(
		db(),
		occurrences,
		[] {
			return ::sqlpp::sqlite3::insert_or_ignore().into(occurrenceTable).columns(
				occurrenceTable.elementId, occurrenceTable.sourceLocationId);
		},
		[](auto& insert, const StorageOccurrence& occurrence) {
			insert.add_values(
				occurrenceTable.elementId = static_cast<Id::type>(occurrence.elementId),
				occurrenceTable.sourceLocationId = static_cast<Id::type>(occurrence.sourceLocationId));
		});
}

bool SqliteIndexStorage::addComponentAccess(const StorageComponentAccess& componentAccess)
{
	return addComponentAccesses({componentAccess});
}

bool SqliteIndexStorage::addComponentAccesses(const std::vector<StorageComponentAccess>& componentAccesses)
{
	using namespace sqlpp;
	return insertInChunks(
		db(),
		componentAccesses,
		[] {
			return ::sqlpp::sqlite3::insert_or_ignore().into(componentAccessTable).columns(
				componentAccessTable.nodeId, componentAccessTable.type);
		},
		[](auto& insert, const StorageComponentAccess& componentAccess) {
			insert.add_values(
				componentAccessTable.nodeId = static_cast<Id::type>(componentAccess.nodeId),
				componentAccessTable.type = static_cast<int>(componentAccess.type));
		});
}

bool SqliteIndexStorage::addNodeAttribute(const StorageNodeAttribute& nodeAttribute)
{
	return addNodeAttributes({nodeAttribute});
}

bool SqliteIndexStorage::addNodeAttributes(const std::vector<StorageNodeAttribute>& nodeAttributes)
{
	using namespace sqlpp;
	return insertInChunks(
		db(),
		nodeAttributes,
		[] {
			return ::sqlpp::sqlite3::insert_or_ignore().into(nodeAttributeTable).columns(
				nodeAttributeTable.nodeId, nodeAttributeTable.key, nodeAttributeTable.value);
		},
		[](auto& insert, const StorageNodeAttribute& nodeAttribute) {
			insert.add_values(
				nodeAttributeTable.nodeId = static_cast<Id::type>(nodeAttribute.nodeId),
				nodeAttributeTable.key = static_cast<int>(nodeAttribute.key),
				nodeAttributeTable.value = nodeAttribute.value);
		});
}

void SqliteIndexStorage::addElementComponent(const StorageElementComponent& component)
{
	using namespace sqlpp;
	try
	{
		// id is omitted -> SQLite assigns the rowid.
		db()(insert_into(elementComponentTable).set(
			elementComponentTable.elementId = static_cast<Id::type>(component.elementId),
			elementComponentTable.type = static_cast<int>(component.type),
			elementComponentTable.data = component.data));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::addElementComponents(const std::vector<StorageElementComponent>& components)
{
	for (const StorageElementComponent& component: components)
	{
		addElementComponent(component);
	}
}

StorageError SqliteIndexStorage::addError(const StorageErrorData& data)
{
	using namespace sqlpp;
	// NOTE: kept from the raw layer for byte-identical storage: messages are
	// stored (and deduplicated) with single quotes doubled.
	const std::string sanitizedMessage = utility::replace(data.message, "'", "''");

	Id id = 0;
#ifdef SOURCETRAIL_CLIENT_IDS
	// Read-back-free dedup: consult an in-process (message, fatal) -> id map.
	const std::pair<std::string, int> errorKey(sanitizedMessage, int(data.fatal));
	if (!m_errorDedupSeeded)
	{
		try
		{
			for (const auto& row: db()(select(errorTable.message, errorTable.fatal, errorTable.id)
										   .from(errorTable)))
			{
				m_errorDedup.emplace(
					std::make_pair(fieldText(row.message), static_cast<int>(row.fatal)),
					Id(row.id));
			}
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
		}
		m_errorDedupSeeded = true;
	}
	if (auto it = m_errorDedup.find(errorKey); it != m_errorDedup.end())
	{
		id = it->second;
	}
#else
	try
	{
		for (const auto& row: db()(select(errorTable.id)
									   .from(errorTable)
									   .where(errorTable.message == sanitizedMessage &&
											  errorTable.fatal == static_cast<int>(data.fatal))
									   .limit(1u)))
		{
			id = Id(row.id);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
#endif

	if (id == 0)
	{
		id = insertElement();

		try
		{
			db()(insert_into(errorTable).set(
				errorTable.id = static_cast<Id::type>(id),
				errorTable.message = sanitizedMessage,
				errorTable.fatal = static_cast<int>(data.fatal),
				errorTable.indexed = static_cast<int>(data.indexed),
				errorTable.translationUnit = data.translationUnit));
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
		}
#ifdef SOURCETRAIL_CLIENT_IDS
		m_errorDedup.emplace(errorKey, id);
#endif
	}

	return StorageError(id, data);
}

void SqliteIndexStorage::removeElement(Id id)
{
	std::vector<Id> ids;
	ids.push_back(id);
	removeElements(ids);
}

void SqliteIndexStorage::removeElements(const std::vector<Id>& ids)
{
	using namespace sqlpp;
	if (ids.empty())
	{
		return;
	}
	try
	{
		db()(delete_from(elementTable).where(elementTable.id.in(toI64(ids))));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::removeOccurrence(const StorageOccurrence& occurrence)
{
	using namespace sqlpp;
	try
	{
		db()(delete_from(occurrenceTable)
				 .where(occurrenceTable.elementId == static_cast<Id::type>(occurrence.elementId) &&
						occurrenceTable.sourceLocationId ==
							static_cast<Id::type>(occurrence.sourceLocationId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::removeOccurrences(const std::vector<StorageOccurrence>& occurrences)
{
	for (const StorageOccurrence& occurrence: occurrences)
	{
		removeOccurrence(occurrence);
	}
}

void SqliteIndexStorage::removeElementsWithoutOccurrences(const std::vector<Id>& elementIds)
{
	using namespace sqlpp;
	if (elementIds.empty())
	{
		return;
	}
	try
	{
		db()(delete_from(elementTable)
				 .where(elementTable.id.in(toI64(elementIds)) &&
						elementTable.id.not_in(
							select(occurrenceTable.elementId).from(occurrenceTable))));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::removeElementsWithLocationInFiles(
	const std::vector<Id>& fileIds, std::function<void(int)> updateStatusCallback)
{
	// Bulk maintenance around a temporary work table; stays on the raw view of the
	// shared handle (the interpolated values are internal ids, and CREATE/DROP has
	// no typed equivalent).
	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(1);
	}

	// preparing
	executeStatement("DROP TABLE IF EXISTS main.element_id_to_clear;");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(2);
	}

	executeStatement(
		"CREATE TABLE IF NOT EXISTS element_id_to_clear("
		"id INTEGER NOT NULL, "
		"PRIMARY KEY(id));");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(3);
	}

	// store ids of all elements located in fileIds into element_id_to_clear
	executeStatement(
		"INSERT INTO element_id_to_clear "
		"	SELECT occurrence.element_id "
		"	FROM occurrence "
		"	INNER JOIN source_location ON ("
		"		occurrence.source_location_id = source_location.id"
		"	) "
		"	WHERE source_location.file_node_id IN (" +
		utility::join(utility::toStrings(fileIds), ',') +
		")"
		"	GROUP BY (occurrence.element_id)");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(4);
	}

	// delete all edges in element_id_to_clear
	executeStatement(
		"DELETE FROM element WHERE element.id IN "
		"	(SELECT element_id_to_clear.id FROM element_id_to_clear INNER JOIN edge ON "
		"(element_id_to_clear.id = edge.id))");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(30);
	}

	// delete all edges originating from element_id_to_clear
	executeStatement(
		"DELETE FROM element WHERE element.id IN (SELECT id FROM edge WHERE source_node_id IN "
		"(SELECT id FROM element_id_to_clear))");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(31);
	}

	// remove all non existing ids from element_id_to_clear (they have been cleared by now and we
	// can disregard them)
	executeStatement(
		"DELETE FROM element_id_to_clear WHERE id NOT IN ("
		"	SELECT id FROM element"
		")");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(33);
	}

	// remove all files from element_id_to_clear (they will be cleared later)
	executeStatement(
		"DELETE FROM element_id_to_clear WHERE id IN ("
		"	SELECT id FROM file"
		")");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(34);
	}

	// delete source locations from fileIds (this also deletes the respective occurrences)
	executeStatement(
		"DELETE FROM source_location WHERE file_node_id IN (" +
		utility::join(utility::toStrings(fileIds), ',') + ");");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(45);
	}

	// remove all ids from element_id_to_clear that still have occurrences
	executeStatement(
		"DELETE FROM element_id_to_clear WHERE id IN ("
		"	SELECT element_id_to_clear.id FROM element_id_to_clear INNER JOIN occurrence ON "
		"		element_id_to_clear.id = occurrence.element_id"
		")");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(59);
	}

	// remove all ids from element_id_to_clear that still have an edge pointing to them
	executeStatement(
		"DELETE FROM element_id_to_clear WHERE id IN ("
		"	SELECT target_node_id FROM edge"
		")");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(74);
	}

	// delete all elements that are still listed in element_id_to_clear
	executeStatement(
		"DELETE FROM element WHERE EXISTS ("
		"	SELECT * FROM element_id_to_clear WHERE element.id = element_id_to_clear.id"
		")");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(87);
	}

	// cleaning up
	executeStatement("DROP TABLE IF EXISTS main.element_id_to_clear;");

	if (updateStatusCallback != nullptr)
	{
		updateStatusCallback(89);
	}
}

void SqliteIndexStorage::removeAllErrors()
{
	using namespace sqlpp;
	try
	{
		db()(delete_from(errorTable));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

bool SqliteIndexStorage::isEdge(Id elementId) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(edgeTable.id).as(cnt))
									   .from(edgeTable)
									   .where(edgeTable.id == static_cast<Id::type>(elementId))))
		{
			return row.cnt > 0;
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return false;
}

bool SqliteIndexStorage::isNode(Id elementId) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(nodeTable.id).as(cnt))
									   .from(nodeTable)
									   .where(nodeTable.id == static_cast<Id::type>(elementId))))
		{
			return row.cnt > 0;
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return false;
}

bool SqliteIndexStorage::isFile(Id elementId) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(fileTable.id).as(cnt))
									   .from(fileTable)
									   .where(fileTable.id == static_cast<Id::type>(elementId))))
		{
			return row.cnt > 0;
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return false;
}

StorageEdge SqliteIndexStorage::getEdgeById(Id edgeId) const
{
	return getFirstById<StorageEdge>(edgeId);
}

StorageEdge SqliteIndexStorage::getEdgeBySourceTargetType(Id sourceId, Id targetId, int type) const
{
	StorageEdge result;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.sourceNodeId == static_cast<Id::type>(sourceId) &&
			edgeTable.targetNodeId == static_cast<Id::type>(targetId) && edgeTable.type == type,
		[&result](StorageEdge&& edge) {
			if (result.id == 0)
			{
				result = std::move(edge);
			}
		});
	return result;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesBySourceId(Id sourceId) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.sourceNodeId == static_cast<Id::type>(sourceId),
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesBySourceIds(const std::vector<Id>& sourceIds) const
{
	std::vector<StorageEdge> edges;
	if (sourceIds.size())
	{
		forEachRowWhere<StorageEdge>(
			db(),
			edgeTable,
			edgeFromRow,
			edgeTable.sourceNodeId.in(toI64(sourceIds)),
			[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	}
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesByTargetId(Id targetId) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.targetNodeId == static_cast<Id::type>(targetId),
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesByTargetIds(const std::vector<Id>& targetIds) const
{
	std::vector<StorageEdge> edges;
	if (targetIds.size())
	{
		forEachRowWhere<StorageEdge>(
			db(),
			edgeTable,
			edgeFromRow,
			edgeTable.targetNodeId.in(toI64(targetIds)),
			[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	}
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesBySourceOrTargetId(Id id) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.sourceNodeId == static_cast<Id::type>(id) ||
			edgeTable.targetNodeId == static_cast<Id::type>(id),
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesByType(int type) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.type == type,
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesBySourceType(Id sourceId, int type) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.sourceNodeId == static_cast<Id::type>(sourceId) && edgeTable.type == type,
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesBySourcesType(
	const std::vector<Id>& sourceIds, int type) const
{
	std::vector<StorageEdge> edges;
	if (sourceIds.size())
	{
		forEachRowWhere<StorageEdge>(
			db(),
			edgeTable,
			edgeFromRow,
			edgeTable.sourceNodeId.in(toI64(sourceIds)) && edgeTable.type == type,
			[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	}
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesByTargetType(Id targetId, int type) const
{
	std::vector<StorageEdge> edges;
	forEachRowWhere<StorageEdge>(
		db(),
		edgeTable,
		edgeFromRow,
		edgeTable.targetNodeId == static_cast<Id::type>(targetId) && edgeTable.type == type,
		[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	return edges;
}

std::vector<StorageEdge> SqliteIndexStorage::getEdgesByTargetsType(
	const std::vector<Id>& targetIds, int type) const
{
	std::vector<StorageEdge> edges;
	if (targetIds.size())
	{
		forEachRowWhere<StorageEdge>(
			db(),
			edgeTable,
			edgeFromRow,
			edgeTable.targetNodeId.in(toI64(targetIds)) && edgeTable.type == type,
			[&edges](StorageEdge&& edge) { edges.emplace_back(std::move(edge)); });
	}
	return edges;
}

StorageNode SqliteIndexStorage::getNodeById(Id id) const
{
	return getFirstById<StorageNode>(id);
}

StorageNode SqliteIndexStorage::getNodeBySerializedName(const std::string& serializedName) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(all_of(nodeTable))
									   .from(nodeTable)
									   .where(nodeTable.serializedName == serializedName)
									   .limit(1u)))
		{
			if (auto node = nodeFromRow(row))
			{
				return std::move(*node);
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageNode();
}

std::vector<NodeKind> SqliteIndexStorage::getAvailableNodeTypes() const
{
	using namespace sqlpp;
	std::vector<NodeKind> types;
	try
	{
		for (const auto& row: db()(select(distinct, nodeTable.type).from(nodeTable)))
		{
			types.push_back(intToEnum<NodeKind>(static_cast<int>(row.type)));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return types;
}

std::vector<Edge::EdgeType> SqliteIndexStorage::getAvailableEdgeTypes() const
{
	using namespace sqlpp;
	std::vector<Edge::EdgeType> types;
	try
	{
		for (const auto& row: db()(select(distinct, edgeTable.type).from(edgeTable)))
		{
			types.push_back(intToEnum<Edge::EdgeType>(static_cast<int>(row.type)));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return types;
}

StorageFile SqliteIndexStorage::getFileByPath(const std::string& filePath) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(all_of(fileTable))
									   .from(fileTable)
									   .where(fileTable.path == filePath)
									   .limit(1u)))
		{
			if (auto file = fileFromRow(row))
			{
				return std::move(*file);
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageFile();
}

std::vector<StorageFile> SqliteIndexStorage::getFilesByPaths(const std::vector<FilePath>& filePaths) const
{
	std::vector<StorageFile> files;
	if (filePaths.size())
	{
		forEachRowWhere<StorageFile>(
			db(),
			fileTable,
			fileFromRow,
			fileTable.path.in(utility::toStrings(filePaths)),
			[&files](StorageFile&& file) { files.emplace_back(std::move(file)); });
	}
	return files;
}

std::shared_ptr<TextAccess> SqliteIndexStorage::getFileContentById(Id fileId) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(filecontentTable.content)
									   .from(filecontentTable)
									   .where(filecontentTable.id == static_cast<Id::type>(fileId))))
		{
			return TextAccess::createFromString(fieldText(row.content));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return TextAccess::createFromString("");
}

std::shared_ptr<TextAccess> SqliteIndexStorage::getFileContentByPath(const std::string& filePath) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row:
			 db()(select(filecontentTable.content)
					  .from(filecontentTable.join(fileTable).on(filecontentTable.id == fileTable.id))
					  .where(fileTable.path == filePath)))
		{
			return TextAccess::createFromString(fieldText(row.content));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return TextAccess::createFromString("");
}

void SqliteIndexStorage::setFileIndexed(Id fileId, bool indexed)
{
	using namespace sqlpp;
	try
	{
		db()(update(fileTable)
				 .set(fileTable.indexed = static_cast<int>(indexed))
				 .where(fileTable.id == static_cast<Id::type>(fileId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::setFileCommandHash(const std::string& filePath, const std::string& hash)
{
	using namespace sqlpp;
	try
	{
		db()(::sqlpp::sqlite3::insert_or_replace().into(fileCommandHashTable).set(
			fileCommandHashTable.path = filePath, fileCommandHashTable.hash = hash));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::removeFileCommandHash(const std::string& filePath)
{
	using namespace sqlpp;
	try
	{
		db()(delete_from(fileCommandHashTable).where(fileCommandHashTable.path == filePath));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

std::unordered_map<std::string, std::string> SqliteIndexStorage::getFileCommandHashes() const
{
	using namespace sqlpp;
	std::unordered_map<std::string, std::string> hashes;
	if (!hasTable("file_command_hash"))
	{
		return hashes;
	}
	try
	{
		for (const auto& row: db()(select(all_of(fileCommandHashTable)).from(fileCommandHashTable)))
		{
			hashes.emplace(std::string(row.path), fieldText(row.hash));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return hashes;
}

void SqliteIndexStorage::setFileCompleteIfNoError(Id fileId, const std::string&  /*filePath*/, bool complete)
{
	using namespace sqlpp;
	bool fileHasErrors = false;
	try
	{
		for (const auto& row:
			 db()(select(sourceLocationTable.id)
					  .from(sourceLocationTable)
					  .where(sourceLocationTable.fileNodeId == static_cast<Id::type>(fileId) &&
							 sourceLocationTable.type == static_cast<int>(LocationType::ERROR))
					  .limit(1u)))
		{
			fileHasErrors = Id(row.id) != 0;
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}

	if (fileHasErrors != complete)
	{
		try
		{
			db()(update(fileTable)
					 .set(fileTable.complete = static_cast<int>(complete))
					 .where(fileTable.id == static_cast<Id::type>(fileId)));
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
		}
	}
}

void SqliteIndexStorage::setNodeType(int type, Id nodeId)
{
	using namespace sqlpp;
	try
	{
		db()(update(nodeTable)
				 .set(nodeTable.type = type)
				 .where(nodeTable.id == static_cast<Id::type>(nodeId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteIndexStorage::setNodeModifiers(int modifiers, Id nodeId)
{
	using namespace sqlpp;
	try
	{
		db()(update(nodeTable)
				 .set(nodeTable.modifiers = modifiers)
				 .where(nodeTable.id == static_cast<Id::type>(nodeId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

namespace
{
// Shared implementation of the three source-location-per-file getters; the
// extra condition narrows by line range or location type.
template <typename ExtraCondition>
std::shared_ptr<SourceLocationFile> sourceLocationsForFile(
	const SqliteIndexStorage& storage,
	sourcetrail::storage::BorrowedSqliteConnection& db,
	const FilePath& filePath,
	const ExtraCondition& extraCondition)
{
	std::shared_ptr<SourceLocationFile> ret = std::make_shared<SourceLocationFile>(
		filePath, "", true, false, false);

	const StorageFile file = storage.getFileByPath(filePath.str());
	if (file.id == 0)	 // early out
	{
		return ret;
	}

	ret->setLanguage(file.languageIdentifier);
	ret->setIsComplete(file.complete);
	ret->setIsIndexed(file.indexed);

	std::vector<StorageSourceLocation> sourceLocations;
	forEachRowWhere<StorageSourceLocation>(
		db,
		sourceLocationTable,
		sourceLocationFromRow,
		sourceLocationTable.fileNodeId == static_cast<Id::type>(file.id) && extraCondition,
		[&sourceLocations](StorageSourceLocation&& location) {
			sourceLocations.emplace_back(std::move(location));
		});

	std::vector<Id> sourceLocationIds;
	sourceLocationIds.reserve(sourceLocations.size());
	for (const StorageSourceLocation& storageLocation: sourceLocations)
	{
		sourceLocationIds.push_back(storageLocation.id);
	}

	std::map<Id, std::vector<Id>> sourceLocationIdToElementIds;
	for (const StorageOccurrence& occurrence: storage.getOccurrencesForLocationIds(sourceLocationIds))
	{
		sourceLocationIdToElementIds[occurrence.sourceLocationId].push_back(occurrence.elementId);
	}

	for (const StorageSourceLocation& location: sourceLocations)
	{
		auto it = sourceLocationIdToElementIds.find(location.id);

		ret->addSourceLocation(
			location.type,
			location.id,
			it != sourceLocationIdToElementIds.end() ? it->second : std::vector<Id>(),
			location.startLine,
			location.startCol,
			location.endLine,
			location.endCol);
	}

	return ret;
}
}	 // namespace

std::shared_ptr<SourceLocationFile> SqliteIndexStorage::getSourceLocationsForFile(
	const FilePath& filePath) const
{
	return sourceLocationsForFile(*this, db(), filePath, sqlpp::value(true));
}

std::shared_ptr<SourceLocationFile> SqliteIndexStorage::getSourceLocationsForLinesInFile(
	const FilePath& filePath, size_t startLine, size_t endLine) const
{
	return sourceLocationsForFile(
		*this,
		db(),
		filePath,
		sourceLocationTable.startLine <= static_cast<Id::type>(endLine) &&
			sourceLocationTable.endLine >= static_cast<Id::type>(startLine));
}

std::shared_ptr<SourceLocationFile> SqliteIndexStorage::getSourceLocationsOfTypeInFile(
	const FilePath& filePath, LocationType type) const
{
	return sourceLocationsForFile(
		*this, db(), filePath, sourceLocationTable.type == static_cast<int>(type));
}

std::shared_ptr<SourceLocationCollection> SqliteIndexStorage::getSourceLocationsForElementIds(
	const std::vector<Id>& elementIds) const
{
	using namespace sqlpp;

	std::vector<Id> sourceLocationIds;
	std::map<Id, std::vector<Id>> sourceLocationIdToElementIds;
	for (const StorageOccurrence& occurrence: getOccurrencesForElementIds(elementIds))
	{
		sourceLocationIds.push_back(occurrence.sourceLocationId);
		sourceLocationIdToElementIds[occurrence.sourceLocationId].push_back(occurrence.elementId);
	}

	std::shared_ptr<SourceLocationCollection> ret = std::make_shared<SourceLocationCollection>();

	if (sourceLocationIds.empty())
	{
		return ret;
	}

	try
	{
		for (const auto& row:
			 db()(select(
					  sourceLocationTable.id,
					  fileTable.path,
					  sourceLocationTable.startLine,
					  sourceLocationTable.startColumn,
					  sourceLocationTable.endLine,
					  sourceLocationTable.endColumn,
					  sourceLocationTable.type)
					  .from(sourceLocationTable.join(fileTable).on(
						  fileTable.id == sourceLocationTable.fileNodeId))
					  .where(sourceLocationTable.id.in(toI64(sourceLocationIds)))))
		{
			const Id id = Id(row.id);
			const std::string filePath = fieldText(row.path);
			const auto startLineNumber = static_cast<int>(row.startLine.value_or(-1));
			const auto startColNumber = static_cast<int>(row.startColumn.value_or(-1));
			const auto endLineNumber = static_cast<int>(row.endLine.value_or(-1));
			const auto endColNumber = static_cast<int>(row.endColumn.value_or(-1));
			const auto type = static_cast<int>(row.type.value_or(-1));

			if (id != 0 && filePath.size() && startLineNumber != -1 && startColNumber != -1 &&
				endLineNumber != -1 && endColNumber != -1 && type != -1)
			{
				ret->addSourceLocation(
					intToEnum<LocationType>(type),
					id,
					sourceLocationIdToElementIds[id],
					FilePath(filePath),
					startLineNumber,
					startColNumber,
					endLineNumber,
					endColNumber);
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}

	return ret;
}

std::vector<StorageOccurrence> SqliteIndexStorage::getOccurrencesForLocationId(Id locationId) const
{
	std::vector<Id> locationIds {locationId};
	return getOccurrencesForLocationIds(locationIds);
}

std::vector<StorageOccurrence> SqliteIndexStorage::getOccurrencesForLocationIds(
	const std::vector<Id>& locationIds) const
{
	std::vector<StorageOccurrence> occurrences;
	if (locationIds.size())
	{
		forEachRowWhere<StorageOccurrence>(
			db(),
			occurrenceTable,
			occurrenceFromRow,
			occurrenceTable.sourceLocationId.in(toI64(locationIds)),
			[&occurrences](StorageOccurrence&& occurrence) {
				occurrences.emplace_back(std::move(occurrence));
			});
	}
	return occurrences;
}

std::vector<StorageOccurrence> SqliteIndexStorage::getOccurrencesForElementIds(
	const std::vector<Id>& elementIds) const
{
	std::vector<StorageOccurrence> occurrences;
	if (elementIds.size())
	{
		forEachRowWhere<StorageOccurrence>(
			db(),
			occurrenceTable,
			occurrenceFromRow,
			occurrenceTable.elementId.in(toI64(elementIds)),
			[&occurrences](StorageOccurrence&& occurrence) {
				occurrences.emplace_back(std::move(occurrence));
			});
	}
	return occurrences;
}

StorageComponentAccess SqliteIndexStorage::getComponentAccessByNodeId(Id nodeId) const
{
	StorageComponentAccess result;
	forEachRowWhere<StorageComponentAccess>(
		db(),
		componentAccessTable,
		componentAccessFromRow,
		componentAccessTable.nodeId == static_cast<Id::type>(nodeId),
		[&result](StorageComponentAccess&& access) {
			if (result.nodeId == 0)
			{
				result = std::move(access);
			}
		});
	return result;
}

std::vector<StorageComponentAccess> SqliteIndexStorage::getComponentAccessesByNodeIds(
	const std::vector<Id>& nodeIds) const
{
	std::vector<StorageComponentAccess> accesses;
	if (nodeIds.size())
	{
		forEachRowWhere<StorageComponentAccess>(
			db(),
			componentAccessTable,
			componentAccessFromRow,
			componentAccessTable.nodeId.in(toI64(nodeIds)),
			[&accesses](StorageComponentAccess&& access) { accesses.emplace_back(std::move(access)); });
	}
	return accesses;
}

std::vector<StorageNodeAttribute> SqliteIndexStorage::getNodeAttributesByNodeIds(
	const std::vector<Id>& nodeIds) const
{
	std::vector<StorageNodeAttribute> attributes;
	if (nodeIds.size())
	{
		forEachRowWhere<StorageNodeAttribute>(
			db(),
			nodeAttributeTable,
			nodeAttributeFromRow,
			nodeAttributeTable.nodeId.in(toI64(nodeIds)),
			[&attributes](StorageNodeAttribute&& attribute) {
				attributes.emplace_back(std::move(attribute));
			});
	}
	return attributes;
}

std::vector<StorageElementComponent> SqliteIndexStorage::getElementComponentsByElementIds(
	const std::vector<Id>& elementIds) const
{
	std::vector<StorageElementComponent> components;
	if (elementIds.size())
	{
		forEachRowWhere<StorageElementComponent>(
			db(),
			elementComponentTable,
			elementComponentFromRow,
			elementComponentTable.elementId.in(toI64(elementIds)),
			[&components](StorageElementComponent&& component) {
				components.emplace_back(std::move(component));
			});
	}
	return components;
}

std::vector<ErrorInfo> SqliteIndexStorage::getAllErrorInfos() const
{
	using namespace sqlpp;
	std::vector<ErrorInfo> errorInfos;

	std::map<Id, size_t> errorIdCount;

	try
	{
		for (const auto& row:
			 db()(select(
					  errorTable.id,
					  errorTable.message,
					  errorTable.fatal,
					  errorTable.indexed,
					  errorTable.translationUnit,
					  fileTable.path,
					  sourceLocationTable.startLine,
					  sourceLocationTable.startColumn)
					  .from(occurrenceTable
								.join(errorTable)
								.on(errorTable.id == occurrenceTable.elementId)
								.join(sourceLocationTable)
								.on(sourceLocationTable.id == occurrenceTable.sourceLocationId)
								.join(fileTable)
								.on(fileTable.id == sourceLocationTable.fileNodeId))))
		{
			const Id id = Id(row.id);
			const std::string message = fieldText(row.message);
			const bool fatal = row.fatal != 0;
			const bool indexed = row.indexed != 0;
			const std::string translationUnit = fieldText(row.translationUnit);
			const std::string filePath = fieldText(row.path);
			const auto lineNumber = static_cast<int>(row.startLine.value_or(-1));
			const auto columnNumber = static_cast<int>(row.startColumn.value_or(-1));

			if (id != 0)
			{
				// There can be multiple errors with the same id, so a count is added to the id
				Id errorId = static_cast<Id::type>(id) * 10000;
				auto it = errorIdCount.find(id);
				if (it != errorIdCount.end())
				{
					errorId += it->second;
					it->second = it->second + 1;
				}
				else
				{
					errorIdCount.emplace(id, 1);
				}

				errorInfos.push_back(ErrorInfo(
					errorId,
					message,
					filePath,
					lineNumber,
					columnNumber,
					translationUnit,
					fatal,
					indexed));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}

	return errorInfos;
}

int SqliteIndexStorage::getNodeCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(nodeTable.id).as(cnt)).from(nodeTable)))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getEdgeCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(edgeTable.id).as(cnt)).from(edgeTable)))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getFileCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(fileTable.id).as(cnt))
									   .from(fileTable)
									   .where(fileTable.indexed == 1)))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getCompletedFileCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(fileTable.id).as(cnt))
									   .from(fileTable)
									   .where(fileTable.indexed == 1 && fileTable.complete == 1)))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getFileLineSum() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(sum(fileTable.lineCount).as(total)).from(fileTable)))
		{
			return static_cast<int>(row.total.value_or(0));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getSourceLocationCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row:
			 db()(select(count(sourceLocationTable.id).as(cnt)).from(sourceLocationTable)))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

int SqliteIndexStorage::getErrorCount() const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(count(errorTable.id).as(cnt))
									   .from(errorTable.join(occurrenceTable)
												 .on(errorTable.id == occurrenceTable.elementId))))
		{
			return static_cast<int>(row.cnt);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return 0;
}

std::vector<std::pair<int, SqliteDatabaseIndex>> SqliteIndexStorage::getIndices()
{
	std::vector<std::pair<int, SqliteDatabaseIndex>> indices;
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("edge_source_node_id_index", "edge(source_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("edge_target_node_id_index", "edge(target_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_READ) | std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("node_serialized_name_index", "node(serialized_name)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_READ) | std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("source_location_file_node_id_index", "source_location(file_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_WRITE), SqliteDatabaseIndex("error_all_data_index", "error(message, fatal)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_WRITE), SqliteDatabaseIndex("file_path_index", "file(path)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_READ) | std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("occurrence_element_id_index", "occurrence(element_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_READ) | std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex( "occurrence_source_location_id_index", "occurrence(source_location_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex( "element_component_foreign_key_index", "element_component(element_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("edge_source_foreign_key_index", "edge(source_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("edge_target_foreign_key_index", "edge(target_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("source_location_foreign_key_index", "source_location(file_node_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex("occurrence_element_foreign_key_index", "occurrence(element_id)")});
	indices.push_back({std::to_underlying(StorageModeType::STORAGE_MODE_CLEAR), SqliteDatabaseIndex( "occurrence_source_location_foreign_key_index", "occurrence(source_location_id)")});

	return indices;
}

void SqliteIndexStorage::clearTables()
{
	try
	{
		m_database.execDML("DROP TABLE IF EXISTS main.error;");
		m_database.execDML("DROP TABLE IF EXISTS main.node_attribute;");
		m_database.execDML("DROP TABLE IF EXISTS main.component_access;");
		m_database.execDML("DROP TABLE IF EXISTS main.occurrence;");
		m_database.execDML("DROP TABLE IF EXISTS main.source_location;");
		m_database.execDML("DROP TABLE IF EXISTS main.local_symbol;");
		m_database.execDML("DROP TABLE IF EXISTS main.filecontent;");
		m_database.execDML("DROP TABLE IF EXISTS main.file;");
		m_database.execDML("DROP TABLE IF EXISTS main.symbol;");
		m_database.execDML("DROP TABLE IF EXISTS main.node;");
		m_database.execDML("DROP TABLE IF EXISTS main.edge;");
		m_database.execDML("DROP TABLE IF EXISTS main.element_component;");
		m_database.execDML("DROP TABLE IF EXISTS main.element;");
		m_database.execDML("DROP TABLE IF EXISTS main.meta;");

		// Force the client-id allocator and in-memory dedup to re-seed from the
		// now-empty tables.
		m_nextElementId = 0;
		m_knownFilePaths.clear();
		m_fileDedupSeeded = false;
		m_errorDedup.clear();
		m_errorDedupSeeded = false;
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

void SqliteIndexStorage::setupTables()
{
	// Keep this DDL in sync with index.sql (the source for IndexTables.h).
	try
	{
		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS element("
			"id INTEGER, "
			"PRIMARY KEY(id));");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS element_component("
			"	id INTEGER, "
			"	element_id INTEGER, "
			"	type INTEGER, "
			"	data TEXT, "
			"	PRIMARY KEY(id), "
			"	FOREIGN KEY(element_id) REFERENCES element(id) ON DELETE CASCADE"
			");");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS edge("
			"id INTEGER NOT NULL, "
			"type INTEGER NOT NULL, "
			"source_node_id INTEGER NOT NULL, "
			"target_node_id INTEGER NOT NULL, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE, "
			"FOREIGN KEY(source_node_id) REFERENCES node(id) ON DELETE CASCADE, "
			"FOREIGN KEY(target_node_id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS node("
			"id INTEGER NOT NULL, "
			"type INTEGER NOT NULL, "
			"serialized_name TEXT, "
			"modifiers INTEGER NOT NULL DEFAULT 0, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS symbol("
			"id INTEGER NOT NULL, "
			"definition_kind INTEGER NOT NULL, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS file("
			"id INTEGER NOT NULL, "
			"path TEXT, "
			"language TEXT, "
			"modification_time TEXT, "
			"indexed INTEGER, "
			"complete INTEGER, "
			"line_count INTEGER, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS filecontent("
			"id INTEGER, "
			"content TEXT, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES file(id)"
			"ON DELETE CASCADE "
			"ON UPDATE CASCADE);");

		// Per-source-file hash of the effective compile command, for flag-aware
		// incremental refresh. Keyed by path (not file id) so the refresh generator
		// can compare without a node-id join. Added via CREATE IF NOT EXISTS so old
		// databases upgrade seamlessly: an absent hash means "unknown", which the
		// refresh treats as "no flag-change signal" rather than forcing a re-index.
		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS file_command_hash("
			"path TEXT NOT NULL, "
			"hash TEXT, "
			"PRIMARY KEY(path));");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS local_symbol("
			"id INTEGER NOT NULL, "
			"name TEXT, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS source_location("
			"id INTEGER NOT NULL, "
			"file_node_id INTEGER, "
			"start_line INTEGER, "
			"start_column INTEGER, "
			"end_line INTEGER, "
			"end_column INTEGER, "
			"type INTEGER, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(file_node_id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS occurrence("
			"element_id INTEGER NOT NULL, "
			"source_location_id INTEGER NOT NULL, "
			"PRIMARY KEY(element_id, source_location_id), "
			"FOREIGN KEY(element_id) REFERENCES element(id) ON DELETE CASCADE, "
			"FOREIGN KEY(source_location_id) REFERENCES source_location(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS component_access("
			"node_id INTEGER NOT NULL, "
			"type INTEGER NOT NULL, "
			"PRIMARY KEY(node_id), "
			"FOREIGN KEY(node_id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS node_attribute("
			"node_id INTEGER NOT NULL, "
			"key INTEGER NOT NULL, "
			"value TEXT, "
			"PRIMARY KEY(node_id, key, value), "
			"FOREIGN KEY(node_id) REFERENCES node(id) ON DELETE CASCADE);");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS error("
			"id INTEGER NOT NULL, "
			"message TEXT, "
			"fatal INTEGER NOT NULL, "
			"indexed INTEGER NOT NULL, "
			"translation_unit TEXT, "
			"PRIMARY KEY(id), "
			"FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE);");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());

		throw(std::exception());
	}
}

void SqliteIndexStorage::setupPrecompiledStatements()
{
	// Typed statements are built per call (multi-row inserts inline their values),
	// so there is nothing to precompile anymore.
}

template <>
void SqliteIndexStorage::forEach<StorageEdge>(std::function<void(StorageEdge&&)> func) const
{
	forEachRowAll<StorageEdge>(db(), edgeTable, edgeFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageNode>(std::function<void(StorageNode&&)> func) const
{
	forEachRowAll<StorageNode>(db(), nodeTable, nodeFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageSymbol>(std::function<void(StorageSymbol&&)> func) const
{
	forEachRowAll<StorageSymbol>(db(), symbolTable, symbolFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageFile>(std::function<void(StorageFile&&)> func) const
{
	forEachRowAll<StorageFile>(db(), fileTable, fileFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageLocalSymbol>(
	std::function<void(StorageLocalSymbol&&)> func) const
{
	forEachRowAll<StorageLocalSymbol>(db(), localSymbolTable, localSymbolFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageSourceLocation>(
	std::function<void(StorageSourceLocation&&)> func) const
{
	forEachRowAll<StorageSourceLocation>(db(), sourceLocationTable, sourceLocationFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageOccurrence>(
	std::function<void(StorageOccurrence&&)> func) const
{
	forEachRowAll<StorageOccurrence>(db(), occurrenceTable, occurrenceFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageComponentAccess>(
	std::function<void(StorageComponentAccess&&)> func) const
{
	forEachRowAll<StorageComponentAccess>(db(), componentAccessTable, componentAccessFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageNodeAttribute>(
	std::function<void(StorageNodeAttribute&&)> func) const
{
	forEachRowAll<StorageNodeAttribute>(db(), nodeAttributeTable, nodeAttributeFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageElementComponent>(
	std::function<void(StorageElementComponent&&)> func) const
{
	forEachRowAll<StorageElementComponent>(db(), elementComponentTable, elementComponentFromRow, func);
}

template <>
void SqliteIndexStorage::forEach<StorageError>(std::function<void(StorageError&&)> func) const
{
	forEachRowAll<StorageError>(db(), errorTable, errorFromRow, func);
}

template <>
void SqliteIndexStorage::forEachByIds<StorageEdge>(
	const std::vector<Id> ids, std::function<void(StorageEdge&&)> func) const
{
	if (ids.size())
	{
		forEachRowWhere<StorageEdge>(db(), edgeTable, edgeFromRow, edgeTable.id.in(toI64(ids)), func);
	}
}

template <>
void SqliteIndexStorage::forEachByIds<StorageNode>(
	const std::vector<Id> ids, std::function<void(StorageNode&&)> func) const
{
	if (ids.size())
	{
		forEachRowWhere<StorageNode>(db(), nodeTable, nodeFromRow, nodeTable.id.in(toI64(ids)), func);
	}
}

template <>
void SqliteIndexStorage::forEachByIds<StorageSymbol>(
	const std::vector<Id> ids, std::function<void(StorageSymbol&&)> func) const
{
	if (ids.size())
	{
		forEachRowWhere<StorageSymbol>(
			db(), symbolTable, symbolFromRow, symbolTable.id.in(toI64(ids)), func);
	}
}

template <>
void SqliteIndexStorage::forEachByIds<StorageFile>(
	const std::vector<Id> ids, std::function<void(StorageFile&&)> func) const
{
	if (ids.size())
	{
		forEachRowWhere<StorageFile>(db(), fileTable, fileFromRow, fileTable.id.in(toI64(ids)), func);
	}
}

template <>
void SqliteIndexStorage::forEachByIds<StorageSourceLocation>(
	const std::vector<Id> ids, std::function<void(StorageSourceLocation&&)> func) const
{
	if (ids.size())
	{
		forEachRowWhere<StorageSourceLocation>(
			db(), sourceLocationTable, sourceLocationFromRow, sourceLocationTable.id.in(toI64(ids)), func);
	}
}

template <>
void SqliteIndexStorage::forEachOfTypeImpl<StorageEdge>(
	int type, std::function<void(StorageEdge&&)> func) const
{
	forEachRowWhere<StorageEdge>(db(), edgeTable, edgeFromRow, edgeTable.type == type, func);
}
