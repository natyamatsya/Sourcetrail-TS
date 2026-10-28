#include "IndexingLock.h"

#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "FilePath.h"

namespace
{
std::filesystem::path lockFilePath(const FilePath& indexDb)
{
	std::filesystem::path path = indexDb.getPath();
	path += ".lock";
	return path;
}

#ifdef _WIN32
void* const kNoHandle = INVALID_HANDLE_VALUE;	// a cast: not constexpr
#else
constexpr int kNoHandle = -1;
#endif
}	 // namespace

std::expected<IndexingLock, IndexingLockError> IndexingLock::acquire(const FilePath& indexDb)
{
	const std::filesystem::path path = lockFilePath(indexDb);
#ifdef _WIN32
	// Not inherited: an indexer subprocess must not keep it.
	const HANDLE handle = CreateFileW(
		path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return std::unexpected(IndexingLockError::CannotOpen);
	}
	OVERLAPPED overlapped {};
	if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped))
	{
		const DWORD error = GetLastError();
		CloseHandle(handle);
		return std::unexpected(
			error == ERROR_LOCK_VIOLATION ? IndexingLockError::HeldElsewhere : IndexingLockError::CannotOpen);
	}
	return IndexingLock(handle);
#else
	// Close-on-exec: an indexer subprocess must not keep it. flock() locks the open
	// file, so a second open in this process conflicts as another process's does.
	const int handle = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (handle < 0)
	{
		return std::unexpected(IndexingLockError::CannotOpen);
	}
	if (::flock(handle, LOCK_EX | LOCK_NB) != 0)
	{
		const int error = errno;
		::close(handle);
		return std::unexpected(error == EWOULDBLOCK ? IndexingLockError::HeldElsewhere : IndexingLockError::CannotOpen);
	}
	return IndexingLock(handle);
#endif
}

bool IndexingLock::isHeld(const FilePath& indexDb)
{
	const std::expected<IndexingLock, IndexingLockError> lock = acquire(indexDb);
	return !lock && lock.error() == IndexingLockError::HeldElsewhere;
}

IndexingLock::IndexingLock(Handle handle): m_handle(handle) {}

IndexingLock::IndexingLock(IndexingLock&& other) noexcept: m_handle(std::exchange(other.m_handle, kNoHandle)) {}

IndexingLock& IndexingLock::operator=(IndexingLock&& other) noexcept
{
	if (this != &other)
	{
		release();
		m_handle = std::exchange(other.m_handle, kNoHandle);
	}
	return *this;
}

IndexingLock::~IndexingLock()
{
	release();
}

void IndexingLock::release() noexcept
{
	if (m_handle == kNoHandle)
	{
		return;
	}
	// The file stays: removing it would let a process that opened it before lock a
	// file nobody else can find any more.
#ifdef _WIN32
	CloseHandle(m_handle);
#else
	::close(m_handle);
#endif
	m_handle = kNoHandle;
}
