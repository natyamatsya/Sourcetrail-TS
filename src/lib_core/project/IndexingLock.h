#ifndef INDEXING_LOCK_H
#define INDEXING_LOCK_H

#include <cstdint>
#include <expected>
#include <string_view>

class FilePath;

enum class IndexingLockError : std::uint8_t
{
	HeldElsewhere,
	CannotOpen
};

constexpr std::string_view to_std_sv(IndexingLockError error) noexcept
{
	using enum IndexingLockError;
	switch (error)
	{
	case HeldElsewhere:
		return "another Sourcetrail process is indexing this project";
	case CannotOpen:
		return "the project's indexing lock file cannot be opened";
	}
	return "unknown error";
}

// The lock a process holds while it indexes a project: on a file beside the index
// (`<project>.srctrl.db.lock`), across processes, released by the system however the
// holder ends. Two runs of one project write the same temporary database and swap it
// over the index, which corrupted it: the second is refused.
class IndexingLock
{
public:
	// Takes the lock of the project whose index is `indexDb`, or says why not.
	static std::expected<IndexingLock, IndexingLockError> acquire(const FilePath& indexDb);
	// Whether someone (another process, or another holder in this one) holds it now.
	static bool isHeld(const FilePath& indexDb);

	IndexingLock(IndexingLock&& other) noexcept;
	IndexingLock& operator=(IndexingLock&& other) noexcept;
	IndexingLock(const IndexingLock&) = delete;
	IndexingLock& operator=(const IndexingLock&) = delete;
	~IndexingLock();

private:
#ifdef _WIN32
	using Handle = void*;
#else
	using Handle = int;
#endif
	explicit IndexingLock(Handle handle);
	void release() noexcept;

	Handle m_handle;
};

#endif	  // INDEXING_LOCK_H
