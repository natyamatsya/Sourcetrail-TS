#ifndef TASK_CLEAN_STORAGE_H
#define TASK_CLEAN_STORAGE_H

#include <memory>
#include <vector>

class DialogView;
class FilePath;
class PersistentStorage;
struct IndexingRunState;

// The run's first step: clears what the files to re-index left in the storage.
class TaskCleanStorage
{
public:
	TaskCleanStorage(
		std::weak_ptr<PersistentStorage> storage,
		std::shared_ptr<DialogView> dialogView,
		const std::vector<FilePath>& filePaths,
		bool clearAllErrors);

	void run(IndexingRunState& state);

private:
	std::weak_ptr<PersistentStorage> m_storage;
	std::shared_ptr<DialogView> m_dialogView;
	std::vector<FilePath> m_filePaths;
	bool m_clearAllErrors;
};

#endif	  // TASK_CLEAN_STORAGE_H
