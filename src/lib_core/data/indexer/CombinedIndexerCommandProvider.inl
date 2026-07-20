// Inline implementations for CombinedIndexerCommandProvider.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommand.h"
#include "logging.h"
#include "utility.h"
#endif

inline void CombinedIndexerCommandProvider::addProvider(
	std::shared_ptr<IndexerCommandProvider> provider, const std::string& sourceGroupId)
{
	if (provider)
	{
		m_providers.emplace_back(provider, sourceGroupId);
	}
	else
	{
		LOG_WARNING("Trying to add non-initialized indexer command provider.");
	}
}

inline std::vector<std::string> CombinedIndexerCommandProvider::getSourceGroupIds() const
{
	std::vector<std::string> groupIds;
	for (const auto& [provider, groupId]: m_providers)
	{
		if (std::find(groupIds.begin(), groupIds.end(), groupId) == groupIds.end())
		{
			groupIds.push_back(groupId);
		}
	}
	return groupIds;
}

inline std::shared_ptr<IndexerCommand> CombinedIndexerCommandProvider::consumeCommandForSourceGroup(
	const std::string& sourceGroupId)
{
	for (const auto& [provider, groupId]: m_providers)
	{
		if (groupId != sourceGroupId)
		{
			continue;
		}
		std::shared_ptr<IndexerCommand> command = provider->consumeCommand();
		if (command)
		{
			command->setSourceGroupId(groupId);
			return command;
		}
	}
	return std::shared_ptr<IndexerCommand>();
}

inline std::vector<FilePath> CombinedIndexerCommandProvider::getAllSourceFilePaths() const
{
	std::vector<FilePath> paths;
	paths.reserve(size());

	for (const auto& [provider, groupId]: m_providers)
	{
		utility::append(paths, provider->getAllSourceFilePaths());
	}

	return paths;
}

inline std::shared_ptr<IndexerCommand> CombinedIndexerCommandProvider::consumeCommand()
{
	for (const auto& [provider, groupId]: m_providers)
	{
		std::shared_ptr<IndexerCommand> command = provider->consumeCommand();
		if (command)
		{
			command->setSourceGroupId(groupId);
			return command;
		}
	}
	return std::shared_ptr<IndexerCommand>();
}

inline std::shared_ptr<IndexerCommand> CombinedIndexerCommandProvider::consumeCommandForSourceFilePath(
	const FilePath& filePath)
{
	for (const auto& [provider, groupId]: m_providers)
	{
		std::shared_ptr<IndexerCommand> command = provider->consumeCommandForSourceFilePath(filePath);
		if (command)
		{
			command->setSourceGroupId(groupId);
			return command;
		}
	}
	return std::shared_ptr<IndexerCommand>();
}

inline std::vector<std::shared_ptr<IndexerCommand>> CombinedIndexerCommandProvider::consumeAllCommands()
{
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	commands.reserve(size());

	for (const auto& [provider, groupId]: m_providers)
	{
		std::vector<std::shared_ptr<IndexerCommand>> providerCommands = provider->consumeAllCommands();
		for (const std::shared_ptr<IndexerCommand>& command: providerCommands)
		{
			command->setSourceGroupId(groupId);
		}
		utility::append(commands, providerCommands);
	}
	return commands;
}

inline void CombinedIndexerCommandProvider::clear()
{
	for (const auto& [provider, groupId]: m_providers)
	{
		provider->clear();
	}
}

inline size_t CombinedIndexerCommandProvider::size() const
{
	size_t size = 0;
	for (const auto& [provider, groupId]: m_providers)
	{
		size += provider->size();
	}
	return size;
}
