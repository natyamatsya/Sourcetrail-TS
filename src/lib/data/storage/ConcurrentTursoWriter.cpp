// Compiled only for the Turso-only concurrent-writer path; otherwise an empty TU
// (the lib globs its sources). Pulls in Turso/stdexec headers only here.
#ifdef SOURCETRAIL_TURSO_CONCURRENT

#include "ConcurrentTursoWriter.h"

#include "StdexecPrelude.h"

#include "ConcurrentStorageIndex.h"
#include "FilePath.h"
#include "Id.h"
#include "TextAccess.h"
#include "logging.h"
#include "turso_shim.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ex = stdexec;
using storage::ConcurrentStorageIndex;
using storage::ElementId;

namespace
{
std::string esc(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (char c: s)
	{
		if (c == '\'')
		{
			out += "''";
		}
		else
		{
			out += c;
		}
	}
	return out;
}

//! Bounded-work-free MPSC-ish queue with an end-of-stream flag; the writers'
//! stand-in for StorageProvider. pop() blocks until an item is available or the
//! stream is finished and drained.
class BatchQueue
{
public:
	void push(ConcurrentTursoWriter::Batch b)
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			m_items.push_back(std::move(b));
		}
		m_cv.notify_one();
	}

	void setDone()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			m_done = true;
		}
		m_cv.notify_all();
	}

	std::optional<ConcurrentTursoWriter::Batch> pop()
	{
		std::unique_lock<std::mutex> lock(m_mtx);
		m_cv.wait(lock, [&] { return !m_items.empty() || m_done; });
		if (m_items.empty())
		{
			return std::nullopt;  // finished and drained
		}
		ConcurrentTursoWriter::Batch b = std::move(m_items.front());
		m_items.pop_front();
		return b;
	}

private:
	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::deque<ConcurrentTursoWriter::Batch> m_items;
	bool m_done = false;
};
}  // namespace

struct ConcurrentTursoWriter::Impl
{
	explicit Impl(int numWriters): pool(static_cast<unsigned>(numWriters > 0 ? numWriters : 1)) {}

	//! Commit attempts per batch before it counts as failed.
	static constexpr int kMaxCommitTries = 8;

	TsqDb* db = nullptr;
	ConcurrentStorageIndex index;
	BatchQueue queue;
	std::atomic<long long> failed{0};
	std::atomic<long long> retried{0};
	bool finished = false;

	exec::static_thread_pool pool;
	exec::async_scope scope;
	std::vector<TsqConn*> conns;

	// Resolve ids/dedup in-process, then write this batch in one BEGIN CONCURRENT
	// transaction on the given connection.
	void process(TsqConn* conn, const Batch& batch)
	{
		// Two per-batch remap tables: LOCAL element id -> DB element id (nodes,
		// edges, local symbols, errors) and LOCAL source-location id -> DB id.
		std::unordered_map<Id::type, ElementId> elem;
		std::unordered_map<Id::type, ElementId> sloc;
		elem.reserve(batch.nodes.size() + batch.edges.size());

		std::string sql;
		auto s = [](long long v) { return std::to_string(v); };

		// errors (element-minting; dedup by message+fatal)
		for (const StorageError& e: batch.errors)
		{
			const auto r = index.internError(e.message, e.fatal);
			elem[static_cast<Id::type>(e.id)] = r.id;
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element(id) VALUES(" + s(r.id) + ");";
				sql += "INSERT OR IGNORE INTO error(id,message,fatal,indexed,translation_unit) VALUES(" + s(r.id) +
					",'" + esc(e.message) + "'," + s(e.fatal ? 1 : 0) + "," + s(e.indexed ? 1 : 0) + ",'" +
					esc(e.translationUnit) + "');";
			}
		}

		// nodes (element-minting; dedup by serialized name, tracking max type)
		for (const StorageNode& n: batch.nodes)
		{
			const auto r = index.internNode(n.serializedName, static_cast<int>(n.type));
			elem[static_cast<Id::type>(n.id)] = r.id;
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element(id) VALUES(" + s(r.id) + ");";
				sql += "INSERT OR IGNORE INTO node(id,type,serialized_name) VALUES(" + s(r.id) + "," +
					s(static_cast<int>(n.type)) + ",'" + esc(n.serializedName) + "');";
			}
		}

		// files (id = node id; dedup by path). Read content + line count like
		// SqliteIndexStorage::addFile; content is bound (below) not inlined.
		std::vector<std::pair<ElementId, std::string>> fileContents;
		for (const StorageFile& f: batch.files)
		{
			const auto it = elem.find(static_cast<Id::type>(f.id));
			if (it == elem.end() || !index.markFile(f.filePath))
			{
				continue;
			}
			int lineCount = 0;
			if (f.indexed)
			{
				const std::shared_ptr<TextAccess> content = TextAccess::createFromFile(FilePath(f.filePath));
				lineCount = content->getLineCount();
				fileContents.emplace_back(it->second, content->getText());
			}
			sql += "INSERT OR IGNORE INTO file(id,path,language,modification_time,indexed,complete,line_count) VALUES(" +
				s(it->second) + ",'" + esc(f.filePath) + "','" + esc(f.languageIdentifier) + "','" +
				esc(f.modificationTime) + "'," + s(f.indexed ? 1 : 0) + "," + s(f.complete ? 1 : 0) + "," +
				s(lineCount) + ");";
		}

		// symbols (id = node id; dedup by node id)
		for (const StorageSymbol& sym: batch.symbols)
		{
			const auto it = elem.find(static_cast<Id::type>(sym.id));
			if (it != elem.end() && index.markSymbol(it->second))
			{
				sql += "INSERT OR IGNORE INTO symbol(id,definition_kind) VALUES(" + s(it->second) + "," +
					s(static_cast<int>(sym.definitionKind)) + ");";
			}
		}

		// edges (remap src/tgt via element map; element-minting)
		for (const StorageEdge& e: batch.edges)
		{
			const auto sIt = elem.find(static_cast<Id::type>(e.sourceNodeId));
			const auto tIt = elem.find(static_cast<Id::type>(e.targetNodeId));
			if (sIt == elem.end() || tIt == elem.end())
			{
				continue;  // reference outside this batch (shouldn't happen)
			}
			const auto r = index.internEdge(static_cast<int>(e.type), sIt->second, tIt->second);
			elem[static_cast<Id::type>(e.id)] = r.id;
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element(id) VALUES(" + s(r.id) + ");";
				sql += "INSERT OR IGNORE INTO edge(id,type,source_node_id,target_node_id) VALUES(" + s(r.id) + "," +
					s(static_cast<int>(e.type)) + "," + s(sIt->second) + "," + s(tIt->second) + ");";
			}
		}

		// local symbols (element-minting; dedup by name)
		for (const StorageLocalSymbol& ls: batch.localSymbols)
		{
			const auto r = index.internLocalSymbol(ls.name);
			elem[static_cast<Id::type>(ls.id)] = r.id;
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element(id) VALUES(" + s(r.id) + ");";
				sql += "INSERT OR IGNORE INTO local_symbol(id,name) VALUES(" + s(r.id) + ",'" + esc(ls.name) + "');";
			}
		}

		// source locations (remap file node id; element-minting)
		for (const StorageSourceLocation& l: batch.sourceLocations)
		{
			const auto fIt = elem.find(static_cast<Id::type>(l.fileNodeId));
			const ElementId fileDbId = (fIt != elem.end()) ? fIt->second : 0;
			const storage::SourceLocationKey key{fileDbId, static_cast<int>(l.startLine),
				static_cast<int>(l.startCol), static_cast<int>(l.endLine), static_cast<int>(l.endCol),
				static_cast<int>(l.type)};
			const auto r = index.internSourceLocation(key);
			sloc[static_cast<Id::type>(l.id)] = r.id;
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element(id) VALUES(" + s(r.id) + ");";
				sql += "INSERT OR IGNORE INTO source_location(id,file_node_id,start_line,start_column,end_line,end_column,type) VALUES(" +
					s(r.id) + "," + s(fileDbId) + "," + s(static_cast<long long>(l.startLine)) + "," +
					s(static_cast<long long>(l.startCol)) + "," + s(static_cast<long long>(l.endLine)) + "," +
					s(static_cast<long long>(l.endCol)) + "," + s(static_cast<int>(l.type)) + ");";
			}
		}

		// occurrences (remap element + source location; dedup by the pair)
		for (const StorageOccurrence& o: batch.occurrences)
		{
			const auto eIt = elem.find(static_cast<Id::type>(o.elementId));
			const auto lIt = sloc.find(static_cast<Id::type>(o.sourceLocationId));
			if (eIt != elem.end() && lIt != sloc.end() && index.markOccurrence(eIt->second, lIt->second))
			{
				sql += "INSERT OR IGNORE INTO occurrence(element_id,source_location_id) VALUES(" + s(eIt->second) +
					"," + s(lIt->second) + ");";
			}
		}

		// component accesses (remap node id; dedup by node id)
		for (const StorageComponentAccess& c: batch.componentAccesses)
		{
			const auto it = elem.find(static_cast<Id::type>(c.nodeId));
			if (it != elem.end() && index.markComponentAccess(it->second))
			{
				sql += "INSERT OR IGNORE INTO component_access(node_id,type) VALUES(" + s(it->second) + "," +
					s(static_cast<int>(c.type)) + ");";
			}
		}

		// element components (remap element id; id-minting; dedup by element+type+data)
		for (const StorageElementComponent& c: batch.elementComponents)
		{
			const auto it = elem.find(static_cast<Id::type>(c.elementId));
			if (it == elem.end())
			{
				continue;
			}
			const storage::ElementComponentKey key{it->second, static_cast<int>(c.type), c.data};
			const auto r = index.internElementComponent(key);
			if (r.created)
			{
				sql += "INSERT OR IGNORE INTO element_component(id,element_id,type,data) VALUES(" + s(r.id) + "," +
					s(it->second) + "," + s(static_cast<int>(c.type)) + ",'" + esc(c.data) + "');";
			}
		}

		commitWithRetry(conn, sql, fileContents);
	}

	struct CommitError
	{
		int code;
		std::string message;
	};

	// One transaction attempt: BEGIN CONCURRENT, the batch SQL, the bound
	// filecontent inserts, COMMIT. On failure, captures (code, message) before
	// the best-effort ROLLBACK (which would overwrite the thread-local error).
	std::optional<CommitError> tryCommit(TsqConn* conn, const std::string& sql,
		const std::vector<std::pair<ElementId, std::string>>& fileContents)
	{
		auto fail = [conn](const char* stage) -> CommitError
		{
			CommitError e{tsq_last_error_code(), std::string(stage) + " failed: " + tsq_last_error()};
			tsq_conn_exec(conn, "ROLLBACK");
			return e;
		};

		if (tsq_conn_exec(conn, "BEGIN CONCURRENT") != TSQ_OK)
		{
			return CommitError{
				tsq_last_error_code(), std::string("BEGIN failed: ") + tsq_last_error()};
		}
		if (!sql.empty() && tsq_conn_exec(conn, sql.c_str()) != TSQ_OK)
		{
			return fail("INSERT");
		}
		// filecontent: bound inserts (arbitrary/large text) in the same transaction.
		if (!fileContents.empty())
		{
			TsqStmt* stmt = tsq_conn_prepare(conn, "INSERT OR IGNORE INTO filecontent(id, content) VALUES(?, ?)");
			if (stmt == nullptr)
			{
				return fail("prepare filecontent");
			}
			for (const auto& [fileId, text]: fileContents)
			{
				tsq_bind_int(stmt, 1, fileId);
				tsq_bind_text(stmt, 2, text.data(), static_cast<int>(text.size()));
				if (tsq_step(stmt) != TSQ_DONE)
				{
					tsq_finalize(stmt);
					return fail("INSERT filecontent");
				}
				tsq_reset(stmt);
			}
			tsq_finalize(stmt);
		}
		if (tsq_conn_exec(conn, "COMMIT") != TSQ_OK)
		{
			return fail("COMMIT");
		}
		return std::nullopt;
	}

	// Commit with bounded retry on transient MVCC conflicts (TSQ_BUSY). Retrying
	// the identical transaction is safe: a conflicted transaction commits no
	// rows, ids were pre-assigned by the in-process index (never read from the
	// DB), and every INSERT is OR IGNORE, so even a partially applied attempt
	// cannot duplicate rows or drift ids.
	void commitWithRetry(TsqConn* conn, const std::string& sql,
		const std::vector<std::pair<ElementId, std::string>>& fileContents)
	{
		for (int tries = 1;; ++tries)
		{
			const std::optional<CommitError> err = tryCommit(conn, sql, fileContents);
			if (!err)
			{
				if (tries > 1)
				{
					retried.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
			if (err->code != TSQ_BUSY || tries >= kMaxCommitTries)
			{
				if (tries > 1)
				{
					retried.fetch_add(1, std::memory_order_relaxed);
				}
				failed.fetch_add(1, std::memory_order_relaxed);
				LOG_ERROR("concurrent writer batch failed (try " + std::to_string(tries) + "/" +
					std::to_string(kMaxCommitTries) + "): " + err->message);
				return;
			}
			// Exponential backoff (1..32 ms) before re-running the transaction.
			std::this_thread::sleep_for(std::chrono::milliseconds(1LL << std::min(tries - 1, 5)));
		}
	}

	void workerLoop(TsqConn* conn)
	{
		// Runs inside a noexcept sender: a throwing batch (e.g. a file read or
		// allocation failure) must be contained, not terminate the indexer.
		for (;;)
		{
			auto batch = queue.pop();
			if (!batch)
			{
				break;
			}
			try
			{
				process(conn, *batch);
			}
			catch (const std::exception& e)
			{
				failed.fetch_add(1, std::memory_order_relaxed);
				LOG_ERROR(std::string("concurrent writer batch threw: ") + e.what());
			}
			catch (...)
			{
				failed.fetch_add(1, std::memory_order_relaxed);
				LOG_ERROR("concurrent writer batch threw unknown exception");
			}
		}
	}

	// NOTE: node types use the first-seen type (whichever concurrent writer creates
	// the node). SqliteIndexStorage upgrades to the numeric max across re-adds; a
	// finish-time UPDATE-to-max pass was tried (index.forEachNode tracks the max)
	// but over-corrected + read non-deterministically under turso 0.7.0-pre.18
	// MVCC, so it is left out. Net effect: a ~0.01-0.03% node.type histogram
	// difference for forward-decl-then-definition nodes. The max is still tracked
	// (ConcurrentNodeIndex) for a future, correct reconciliation.
};

ConcurrentTursoWriter::ConcurrentTursoWriter(const std::string& dbPath, int numWriters)
	: m_impl(std::make_unique<Impl>(numWriters))
{
	m_impl->db = tsq_open(dbPath.c_str());
	if (m_impl->db == nullptr)
	{
		LOG_ERROR(std::string("ConcurrentTursoWriter: tsq_open failed: ") + tsq_last_error());
		return;
	}
	tsq_exec(m_impl->db, "PRAGMA journal_mode = 'mvcc'");
	// No FOREIGN_KEYS: under MVCC snapshot isolation a concurrent writer cannot see
	// another writer's uncommitted node, so FK checks on cross-writer references
	// would spuriously fail. (Sourcetrail validates/rebuilds structure afterward.)
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS element(id INTEGER PRIMARY KEY)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS node(id INTEGER PRIMARY KEY, type INTEGER, serialized_name TEXT)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS edge(id INTEGER PRIMARY KEY, type INTEGER, source_node_id INTEGER, target_node_id INTEGER)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS local_symbol(id INTEGER PRIMARY KEY, name TEXT)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS symbol(id INTEGER PRIMARY KEY, definition_kind INTEGER)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS file(id INTEGER PRIMARY KEY, path TEXT, language TEXT, modification_time TEXT, indexed INTEGER, complete INTEGER, line_count INTEGER)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS filecontent(id INTEGER PRIMARY KEY, content TEXT)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS source_location(id INTEGER PRIMARY KEY, file_node_id INTEGER, start_line INTEGER, start_column INTEGER, end_line INTEGER, end_column INTEGER, type INTEGER)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS occurrence(element_id INTEGER, source_location_id INTEGER, PRIMARY KEY(element_id, source_location_id))");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS component_access(node_id INTEGER PRIMARY KEY, type INTEGER)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS element_component(id INTEGER PRIMARY KEY, element_id INTEGER, type INTEGER, data TEXT)");
	tsq_exec(m_impl->db, "CREATE TABLE IF NOT EXISTS error(id INTEGER PRIMARY KEY, message TEXT, fatal INTEGER, indexed INTEGER, translation_unit TEXT)");
	m_impl->index.seed(1);

	auto sched = m_impl->pool.get_scheduler();
	const int n = numWriters > 0 ? numWriters : 1;
	for (int w = 0; w < n; ++w)
	{
		TsqConn* conn = tsq_new_connection(m_impl->db);
		m_impl->conns.push_back(conn);
		Impl* impl = m_impl.get();
		m_impl->scope.spawn(
			ex::schedule(sched) | ex::then([impl, conn]() noexcept { impl->workerLoop(conn); }));
	}
}

ConcurrentTursoWriter::~ConcurrentTursoWriter()
{
	if (m_impl)
	{
		finish();
		if (m_impl->db != nullptr)
		{
			tsq_close(m_impl->db);
			m_impl->db = nullptr;
		}
	}
}

void ConcurrentTursoWriter::submit(Batch batch)
{
	m_impl->queue.push(std::move(batch));
}

void ConcurrentTursoWriter::finish()
{
	if (m_impl->finished)
	{
		return;
	}
	m_impl->finished = true;
	m_impl->queue.setDone();
	ex::sync_wait(m_impl->scope.on_empty());  // drain barrier

	for (TsqConn* c: m_impl->conns)
	{
		tsq_conn_close(c);
	}
	m_impl->conns.clear();

	// Persist the committed MVCC data to the .turso file. The checkpoint must be
	// driven via prepare+step (like tsq_step): routing it through tsq_exec's
	// prepare_execute_batch path SIGBUS-crashes turso_core 0.7.0-pre.18.
	if (m_impl->db != nullptr)
	{
		TsqStmt* ckpt = tsq_prepare(m_impl->db, "PRAGMA wal_checkpoint(TRUNCATE)");
		if (ckpt != nullptr)
		{
			while (tsq_step(ckpt) == TSQ_ROW)
			{
			}
			tsq_finalize(ckpt);
		}
		else
		{
			LOG_ERROR(std::string("concurrent writer checkpoint failed: ") + tsq_last_error());
		}
	}
}

long long ConcurrentTursoWriter::failedBatches() const
{
	return m_impl->failed.load(std::memory_order_relaxed);
}

long long ConcurrentTursoWriter::retriedBatches() const
{
	return m_impl->retried.load(std::memory_order_relaxed);
}

long long ConcurrentTursoWriter::scalar(const std::string& sql) const
{
	if (m_impl->db == nullptr)
	{
		return -1;
	}
	TsqStmt* stmt = tsq_prepare(m_impl->db, sql.c_str());
	if (stmt == nullptr)
	{
		return -1;
	}
	long long v = -1;
	if (tsq_step(stmt) == TSQ_ROW)
	{
		v = tsq_column_int(stmt, 0);
	}
	tsq_finalize(stmt);
	return v;
}

#endif  // SOURCETRAIL_TURSO_CONCURRENT
