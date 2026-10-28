#ifndef INDEX_DATABASE_FILES_H
#define INDEX_DATABASE_FILES_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

class FilePath;

// Why the database an indexing run wrote did not become the project's index. In
// every case the index stays as it was.
enum class IndexSwapError : std::uint8_t
{
	TempIndexMissing,
	TempIndexInvalid,
	TempIndexInUse,
	IndexInUse,
	ReplaceFailed
};

constexpr std::string_view to_std_sv(IndexSwapError error) noexcept
{
	using enum IndexSwapError;
	switch (error)
	{
	case TempIndexMissing:
		return "the database the indexing wrote is gone (was it deleted while indexing?)";
	case TempIndexInvalid:
		return "the database the indexing wrote is not an index of this version of Sourcetrail";
	case TempIndexInUse:
		return "the database the indexing wrote is still open";
	case IndexInUse:
		return "the index is open in another process";
	case ReplaceFailed:
		return "the file system refused to replace the index";
	}
	return "unknown error";
}

// How far the run that writes a temporary index got, as its database says (the meta
// value "indexing_run"): what to ask about one left behind. A crash leaves Running.
enum class IndexingRunMark : std::uint8_t
{
	Running,	// indexing, or it ended without a word: a crash
	Stopped,	// stopped before it finished (the app closed while indexing)
	Finished	// done and kept, but not swapped in (the swap failed, or the app closed first)
};

constexpr std::string_view to_std_sv(IndexingRunMark mark) noexcept
{
	using enum IndexingRunMark;
	switch (mark)
	{
	case Running:
		return "running";
	case Stopped:
		return "stopped";
	case Finished:
		return "finished";
	}
	return "running";
}

// The files of a project's index: the database a run writes (`<project>.srctrl.db.tmp`)
// becomes the index (`<project>.srctrl.db`) in one rename, once it is known to be an
// index, and never by removing the index first.
namespace index_database
{
// Whether `db` is an index database of `storageVersion`. Opens it without creating
// it, and closes it again: SQLite moves what its write-ahead log holds into the file.
std::expected<void, IndexSwapError> check(const FilePath& db, std::size_t storageVersion);

// Renames `tempIndexDb` over `indexDb`, once check() accepts it and neither is open
// (the index's own write-ahead log is merged and gone). On failure both files stay
// as they were; when `tempIndexDb` is gone, what SQLite left beside it goes too.
std::expected<void, IndexSwapError> replace(
	const FilePath& indexDb, const FilePath& tempIndexDb, std::size_t storageVersion);

// The meta key of the IndexingRunMark.
inline constexpr const char* runMarkKey = "indexing_run";
// The mark of the run that wrote `db`; nullopt when it has none (or is no database).
std::optional<IndexingRunMark> runMark(const FilePath& db);
// Marks the run that wrote `db` Stopped, unless it got further.
void markStopped(const FilePath& db);

// Removes `db` with its write-ahead log, shared memory and journal: a database made
// under that name later would read a log left behind as its own.
void remove(const FilePath& db);
}	 // namespace index_database

#endif	  // INDEX_DATABASE_FILES_H
