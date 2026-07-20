// Inline implementations for MemoryIndexerCommandProvider.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommand.h"
#endif

inline MemoryIndexerCommandProvider::MemoryIndexerCommandProvider(
	const std::vector<std::shared_ptr<IndexerCommand>>& commands)
{
	for (const std::shared_ptr<IndexerCommand>& command: commands)
	{
		m_commands[command->getSourceFilePath()] = command;
	}
}

inline std::vector<FilePath> MemoryIndexerCommandProvider::getAllSourceFilePaths() const
{
	std::vector<FilePath> paths;
	paths.reserve(m_commands.size());

	for (std::map<FilePath, std::shared_ptr<IndexerCommand>>::const_iterator it = m_commands.begin();
		 it != m_commands.end();
		 it++)
	{
		paths.emplace_back(it->first);
	}

	return paths;
}

inline std::shared_ptr<IndexerCommand> MemoryIndexerCommandProvider::consumeCommand()
{
	if (!m_commands.empty())
	{
		std::map<FilePath, std::shared_ptr<IndexerCommand>>::const_iterator it = m_commands.begin();
		std::shared_ptr<IndexerCommand> command = it->second;
		m_commands.erase(it);
		return command;
	}
	return std::shared_ptr<IndexerCommand>();
}

inline std::shared_ptr<IndexerCommand> MemoryIndexerCommandProvider::consumeCommandForSourceFilePath(
	const FilePath& filePath)
{
	std::map<FilePath, std::shared_ptr<IndexerCommand>>::const_iterator it = m_commands.find(filePath);
	if (it != m_commands.end())
	{
		std::shared_ptr<IndexerCommand> command = it->second;
		m_commands.erase(it);
		return command;
	}
	return std::shared_ptr<IndexerCommand>();
}

inline std::vector<std::shared_ptr<IndexerCommand>> MemoryIndexerCommandProvider::consumeAllCommands()
{
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	commands.reserve(m_commands.size());
	for (std::map<FilePath, std::shared_ptr<IndexerCommand>>::const_iterator it = m_commands.begin();
		 it != m_commands.end();
		 it++)
	{
		commands.emplace_back(it->second);
	}
	m_commands.clear();
	return commands;
}

inline void MemoryIndexerCommandProvider::clear()
{
	m_commands.clear();
}

inline size_t MemoryIndexerCommandProvider::size() const
{
	return m_commands.size();
}
