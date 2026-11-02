#include "Catch2.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "FilePath.h"
#include "IndexingLock.h"
#include "TestTempDirectory.h"

TEST_CASE("a project's indexing lock has one holder")
{
	const TestTempDirectory directory {"IndexingLockTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));

	REQUIRE_FALSE(IndexingLock::isHeld(indexDb));
	{
		std::expected<IndexingLock, IndexingLockError> first = IndexingLock::acquire(indexDb);
		REQUIRE(first);
		REQUIRE(IndexingLock::isHeld(indexDb));

		const std::expected<IndexingLock, IndexingLockError> second = IndexingLock::acquire(indexDb);
		REQUIRE_FALSE(second);
		REQUIRE(second.error() == IndexingLockError::HeldElsewhere);

		// Moved, it is still held, once.
		const IndexingLock moved = std::move(*first);
		REQUIRE(IndexingLock::isHeld(indexDb));
	}
	REQUIRE_FALSE(IndexingLock::isHeld(indexDb));
	REQUIRE(IndexingLock::acquire(indexDb));
}

TEST_CASE("a project's indexing lock is per project")
{
	const TestTempDirectory directory {"IndexingLockTestSuite"};
	const std::expected<IndexingLock, IndexingLockError> a =
		IndexingLock::acquire(FilePath(directory.getFilePath("a.srctrl.db")));
	const std::expected<IndexingLock, IndexingLockError> b =
		IndexingLock::acquire(FilePath(directory.getFilePath("b.srctrl.db")));

	REQUIRE(a);
	REQUIRE(b);
}

TEST_CASE("a project's indexing lock cannot be taken where the project cannot be written")
{
	const TestTempDirectory directory {"IndexingLockTestSuite"};
	const std::expected<IndexingLock, IndexingLockError> lock =
		IndexingLock::acquire(FilePath(directory.getFilePath("missing/project.srctrl.db")));

	REQUIRE_FALSE(lock);
	REQUIRE(lock.error() == IndexingLockError::CannotOpen);
}

#ifndef _WIN32
// Two Sourcetrail processes indexing one project corrupted its index.
TEST_CASE("a project's indexing lock keeps out another process")
{
	const TestTempDirectory directory {"IndexingLockTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));

	const auto otherProcessGetsIt = [&]() {
		const pid_t child = fork();
		if (child == 0)
		{
			_exit(IndexingLock::acquire(indexDb) ? 0 : 1);
		}
		int status = 0;
		waitpid(child, &status, 0);
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	};

	{
		const std::expected<IndexingLock, IndexingLockError> lock = IndexingLock::acquire(indexDb);
		REQUIRE(lock);
		REQUIRE_FALSE(otherProcessGetsIt());
	}
	REQUIRE(otherProcessGetsIt());
}
#endif
