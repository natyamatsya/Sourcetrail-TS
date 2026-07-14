// Compiled only for the Turso concurrent-writer path; otherwise an empty TU
// (the lib globs its sources).
#ifdef SOURCETRAIL_TURSO_CONCURRENT

#include "TursoSqliteExport.h"

#include <string>
#include <variant>
#include <vector>

#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>

#include "BorrowedSqliteConnection.h"
#include "ConcurrentTursoWriter.h"
#include "IndexTables.h"
#include "StorageConnection.h"
#include "TimeStamp.h"
#include "logging.h"

namespace
{
// Table instances (stateless), same models as SqliteIndexStorage.
constexpr idx::Element elementTable;
constexpr idx::ElementComponent elementComponentTable;
constexpr idx::Edge edgeTable;
constexpr idx::Node nodeTable;
constexpr idx::Symbol symbolTable;
constexpr idx::File fileTable;
constexpr idx::Filecontent filecontentTable;
constexpr idx::LocalSymbol localSymbolTable;
constexpr idx::SourceLocation sourceLocationTable;
constexpr idx::Occurrence occurrenceTable;
constexpr idx::ComponentAccess componentAccessTable;
constexpr idx::Error errorTable;

// Mirrors SqliteIndexStorage's chunking: values are inlined and escaped by
// sqlpp23, so chunking only bounds the length of one serialized statement.
constexpr size_t kInsertChunkRows = 500;

using QueryValue = ConcurrentTursoWriter::QueryValue;
using QueryRow = std::vector<QueryValue>;

int64_t asInt(const QueryValue& value)
{
	if (const auto* i = std::get_if<long long>(&value))
	{
		return *i;
	}
	// Defensive: a numeric column delivered as text.
	const std::string& text = std::get<std::string>(value);
	return text.empty() ? 0 : std::stoll(text);
}

const std::string& asText(const QueryValue& value)
{
	static const std::string empty;
	if (const auto* s = std::get_if<std::string>(&value))
	{
		return *s;
	}
	return empty;
}

//! Stream one table from the Turso database into SQLite: buffer rows and
//! flush a multi-row INSERT every kInsertChunkRows.
template <typename MakeInsert, typename AddRow>
size_t copyTable(
	const ConcurrentTursoWriter& writer,
	sourcetrail::storage::BorrowedSqliteConnection& db,
	const char* selectSql,
	const MakeInsert& makeInsert,
	const AddRow& addRow)
{
	size_t total = 0;
	std::vector<QueryRow> rows;
	rows.reserve(kInsertChunkRows);

	auto flush = [&]() {
		if (rows.empty())
		{
			return;
		}
		auto insert = makeInsert();
		for (const QueryRow& row: rows)
		{
			addRow(insert, row);
		}
		db(insert);
		total += rows.size();
		rows.clear();
	};

	writer.query(selectSql, [&](const QueryRow& row) {
		rows.push_back(row);
		if (rows.size() >= kInsertChunkRows)
		{
			flush();
		}
	});
	flush();
	return total;
}
}  // namespace

bool exportConcurrentTursoToSqlite(
	const ConcurrentTursoWriter& writer, StorageConnection& connection)
{
	using namespace sqlpp;
	sourcetrail::storage::BorrowedSqliteConnection& db = connection.typed();

	const TimeStamp exportStart = TimeStamp::now();

	try
	{
		db.start_transaction();

		const size_t elements = copyTable(
			writer, db, "SELECT id FROM element",
			[] { return insert_into(elementTable).columns(elementTable.id); },
			[](auto& insert, const QueryRow& row) {
				insert.add_values(elementTable.id = asInt(row[0]));
			});

		const size_t nodes = copyTable(
			writer, db, "SELECT id, type, serialized_name FROM node",
			[] {
				return insert_into(nodeTable).columns(
					nodeTable.id, nodeTable.type, nodeTable.serializedName);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					nodeTable.id = asInt(row[0]),
					nodeTable.type = asInt(row[1]),
					nodeTable.serializedName = asText(row[2]));
			});

		const size_t edges = copyTable(
			writer, db, "SELECT id, type, source_node_id, target_node_id FROM edge",
			[] {
				return insert_into(edgeTable).columns(
					edgeTable.id, edgeTable.type, edgeTable.sourceNodeId, edgeTable.targetNodeId);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					edgeTable.id = asInt(row[0]),
					edgeTable.type = asInt(row[1]),
					edgeTable.sourceNodeId = asInt(row[2]),
					edgeTable.targetNodeId = asInt(row[3]));
			});

		const size_t localSymbols = copyTable(
			writer, db, "SELECT id, name FROM local_symbol",
			[] {
				return insert_into(localSymbolTable).columns(
					localSymbolTable.id, localSymbolTable.name);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					localSymbolTable.id = asInt(row[0]), localSymbolTable.name = asText(row[1]));
			});

		const size_t symbols = copyTable(
			writer, db, "SELECT id, definition_kind FROM symbol",
			[] {
				return insert_into(symbolTable).columns(
					symbolTable.id, symbolTable.definitionKind);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					symbolTable.id = asInt(row[0]), symbolTable.definitionKind = asInt(row[1]));
			});

		const size_t files = copyTable(
			writer, db,
			"SELECT id, path, language, modification_time, indexed, complete, line_count FROM file",
			[] {
				return insert_into(fileTable).columns(
					fileTable.id,
					fileTable.path,
					fileTable.language,
					fileTable.modificationTime,
					fileTable.indexed,
					fileTable.complete,
					fileTable.lineCount);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					fileTable.id = asInt(row[0]),
					fileTable.path = asText(row[1]),
					fileTable.language = asText(row[2]),
					fileTable.modificationTime = asText(row[3]),
					fileTable.indexed = asInt(row[4]),
					fileTable.complete = asInt(row[5]),
					fileTable.lineCount = asInt(row[6]));
			});

		const size_t fileContents = copyTable(
			writer, db, "SELECT id, content FROM filecontent",
			[] {
				return insert_into(filecontentTable)
					.columns(filecontentTable.id, filecontentTable.content);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					filecontentTable.id = asInt(row[0]),
					filecontentTable.content = asText(row[1]));
			});

		const size_t sourceLocations = copyTable(
			writer, db,
			"SELECT id, file_node_id, start_line, start_column, end_line, end_column, type FROM source_location",
			[] {
				return insert_into(sourceLocationTable)
					.columns(
						sourceLocationTable.id,
						sourceLocationTable.fileNodeId,
						sourceLocationTable.startLine,
						sourceLocationTable.startColumn,
						sourceLocationTable.endLine,
						sourceLocationTable.endColumn,
						sourceLocationTable.type);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					sourceLocationTable.id = asInt(row[0]),
					sourceLocationTable.fileNodeId = asInt(row[1]),
					sourceLocationTable.startLine = asInt(row[2]),
					sourceLocationTable.startColumn = asInt(row[3]),
					sourceLocationTable.endLine = asInt(row[4]),
					sourceLocationTable.endColumn = asInt(row[5]),
					sourceLocationTable.type = asInt(row[6]));
			});

		const size_t occurrences = copyTable(
			writer, db, "SELECT element_id, source_location_id FROM occurrence",
			[] {
				return insert_into(occurrenceTable)
					.columns(occurrenceTable.elementId, occurrenceTable.sourceLocationId);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					occurrenceTable.elementId = asInt(row[0]),
					occurrenceTable.sourceLocationId = asInt(row[1]));
			});

		const size_t componentAccesses = copyTable(
			writer, db, "SELECT node_id, type FROM component_access",
			[] {
				return insert_into(componentAccessTable)
					.columns(componentAccessTable.nodeId, componentAccessTable.type);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					componentAccessTable.nodeId = asInt(row[0]),
					componentAccessTable.type = asInt(row[1]));
			});

		const size_t elementComponents = copyTable(
			writer, db, "SELECT id, element_id, type, data FROM element_component",
			[] {
				return insert_into(elementComponentTable)
					.columns(
						elementComponentTable.id,
						elementComponentTable.elementId,
						elementComponentTable.type,
						elementComponentTable.data);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					elementComponentTable.id = asInt(row[0]),
					elementComponentTable.elementId = asInt(row[1]),
					elementComponentTable.type = asInt(row[2]),
					elementComponentTable.data = asText(row[3]));
			});

		const size_t errors = copyTable(
			writer, db, "SELECT id, message, fatal, indexed, translation_unit FROM error",
			[] {
				return insert_into(errorTable).columns(
					errorTable.id,
					errorTable.message,
					errorTable.fatal,
					errorTable.indexed,
					errorTable.translationUnit);
			},
			[](auto& insert, const QueryRow& row) {
				insert.add_values(
					errorTable.id = asInt(row[0]),
					errorTable.message = asText(row[1]),
					errorTable.fatal = asInt(row[2]),
					errorTable.indexed = asInt(row[3]),
					errorTable.translationUnit = asText(row[4]));
			});

		db.commit_transaction();

		// The export is the sole-writer mode's serialization cost — report its
		// duration separately so run timings can split ingest from export.
		LOG_INFO(
			"Turso->SQLite export took " +
			std::to_string(static_cast<long long>(TimeStamp::now().deltaMS(exportStart))) + " ms");
		LOG_INFO(
			"Turso->SQLite export: element=" + std::to_string(elements) +
			" node=" + std::to_string(nodes) + " edge=" + std::to_string(edges) +
			" local_symbol=" + std::to_string(localSymbols) + " symbol=" + std::to_string(symbols) +
			" file=" + std::to_string(files) + " filecontent=" + std::to_string(fileContents) +
			" source_location=" + std::to_string(sourceLocations) +
			" occurrence=" + std::to_string(occurrences) +
			" component_access=" + std::to_string(componentAccesses) +
			" element_component=" + std::to_string(elementComponents) +
			" error=" + std::to_string(errors));
		return true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::string("Turso->SQLite export failed: ") + e.what());
		try
		{
			db.rollback_transaction();
		}
		catch (...)
		{
			// no transaction open or rollback failed — nothing more to do
		}
		return false;
	}
}

#endif	  // SOURCETRAIL_TURSO_CONCURRENT
