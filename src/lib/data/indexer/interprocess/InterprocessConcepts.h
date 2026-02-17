#ifndef INTERPROCESS_CONCEPTS_H
#define INTERPROCESS_CONCEPTS_H

#include <concepts>
#include <memory>
#include <string>
#include <vector>

#include "FilePath.h"

class IndexerCommand;
class IntermediateStorage;
enum class ProcessId : std::size_t;

template <typename T>
concept IndexerCommandManagerConcept = requires(
	T t,
	const std::string& uuid, ProcessId pid, bool owner,
	const std::vector<std::shared_ptr<IndexerCommand>>& cmds)
{
	{ T(uuid, pid, owner) };
	{ t.pushIndexerCommands(cmds) } -> std::same_as<void>;
	{ t.popIndexerCommand() } -> std::same_as<std::shared_ptr<IndexerCommand>>;
	{ t.clearIndexerCommands() } -> std::same_as<void>;
	{ t.indexerCommandCount() } -> std::convertible_to<size_t>;
};

template <typename T>
concept IntermediateStorageManagerConcept = requires(
	T t,
	const std::string& uuid, ProcessId pid, bool owner,
	const std::shared_ptr<IntermediateStorage>& storage)
{
	{ T(uuid, pid, owner) };
	{ t.pushIntermediateStorage(storage) } -> std::same_as<void>;
	{ t.popIntermediateStorage() } -> std::same_as<std::shared_ptr<IntermediateStorage>>;
	{ t.getIntermediateStorageCount() } -> std::convertible_to<size_t>;
};

template <typename T>
concept IndexingStatusManagerConcept = requires(
	T t,
	const std::string& uuid, ProcessId pid, bool owner,
	const FilePath& filePath, bool interrupted)
{
	{ T(uuid, pid, owner) };
	{ t.startIndexingSourceFile(filePath) } -> std::same_as<void>;
	{ t.finishIndexingSourceFile() } -> std::same_as<void>;
	{ t.setIndexingInterrupted(interrupted) } -> std::same_as<void>;
	{ t.getIndexingInterrupted() } -> std::same_as<bool>;
	{ t.getNextFinishedProcessId() } -> std::same_as<ProcessId>;
	{ t.getCurrentlyIndexedSourceFilePaths() } -> std::same_as<std::vector<FilePath>>;
	{ t.getCrashedSourceFilePaths() } -> std::same_as<std::vector<FilePath>>;
};

template <typename T>
concept GarbageCollectorConcept = requires(
	T t,
	const std::string& uuid,
	const std::string& name)
{
	{ T::createInstance() } -> std::same_as<T*>;
	{ T::getInstance() } -> std::same_as<T*>;
	{ t.run(uuid) } -> std::same_as<void>;
	{ t.stop() } -> std::same_as<void>;
	{ t.registerSharedMemory(name) } -> std::same_as<void>;
	{ t.unregisterSharedMemory(name) } -> std::same_as<void>;
};

#endif // INTERPROCESS_CONCEPTS_H
