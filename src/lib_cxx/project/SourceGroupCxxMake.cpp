#include "SourceGroupCxxMake.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "Application.h"
#include "MessageStatus.h"
#include "SourceGroupSettingsCxxMake.h"
#include "logging.h"

namespace
{
make::Invocation invocationOf(const SourceGroupSettingsCxxMake& settings)
{
	return {
		.directory = settings.getMakefileDirectoryExpandedAndAbsolute().str(),
		.program = settings.getMakeProgram(),
		.arguments = make::splitArguments(settings.getMakeArguments())};
}

std::string readFile(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}	 // namespace

SourceGroupCxxMake::SourceGroupCxxMake(std::shared_ptr<SourceGroupSettingsCxxMake> settings)
	: SourceGroupCxxCdb(settings), m_settings(settings)
{
}

std::expected<make::DryRun, make::Error> SourceGroupCxxMake::writeCompilationDatabase(SourceGroupSettingsCxxMake& settings)
{
	settings.useGeneratedCompilationDatabase();
	const make::Invocation invocation{invocationOf(settings)};
	const std::expected<make::DryRun, make::Error> dryRun{make::runDryRun(invocation)};
	if (!dryRun)
	{
		return dryRun;
	}
	if (dryRun->commands.empty())
	{
		return std::unexpected(make::noCompileCommands(*dryRun, invocation));
	}

	const std::filesystem::path path{settings.getGeneratedCompilationDatabasePath().str()};
	const std::string database{make::toCompilationDatabase(make::firstPerFile(dryRun->commands))};
	if (readFile(path) != database)
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << database;
		if (!out)
		{
			return std::unexpected(make::Error{
				make::Error::Code::DirectoryMissing,
				std::format("The compile commands could not be written to \"{}\".", path.generic_string())});
		}
	}
	LOG_INFO(std::format(
		"{} compile commands from \"{}\" in \"{}\"", dryRun->commands.size(), dryRun->commandLine, invocation.directory));
	return dryRun;
}

std::expected<void, PrepareIndexingError> SourceGroupCxxMake::prepareIndexing()
{
	if (const auto written = writeCompilationDatabase(*m_settings); !written)
	{
		const std::string error = "Can't refresh project. " + make::describe(written.error());
		LOG_ERROR(error);
		MessageStatus(error, true).dispatch();
		Application::getInstance()->showNotice(error, {"Ok"});
		switch (written.error().code)
		{
		case make::Error::Code::DirectoryMissing:
		case make::Error::Code::NoMakefile:
			return std::unexpected(PrepareIndexingError::SourcePathMissing);
		case make::Error::Code::MakeNotFound:
			return std::unexpected(PrepareIndexingError::ConfigurationIncomplete);
		case make::Error::Code::NoCompileCommands:
			return std::unexpected(PrepareIndexingError::InputUnusable);
		}
	}
	return SourceGroupCxxCdb::prepareIndexing();
}

std::set<FilePath> SourceGroupCxxMake::filterToContainedFilePaths(const std::set<FilePath>& filePaths) const
{
	ensureCompilationDatabase();
	return SourceGroupCxxCdb::filterToContainedFilePaths(filePaths);
}

std::set<FilePath> SourceGroupCxxMake::getAllSourceFilePaths() const
{
	ensureCompilationDatabase();
	return SourceGroupCxxCdb::getAllSourceFilePaths();
}

std::shared_ptr<IndexerCommandProvider> SourceGroupCxxMake::getIndexerCommandProvider(const RefreshInfo& info) const
{
	ensureCompilationDatabase();
	return SourceGroupCxxCdb::getIndexerCommandProvider(info);
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupCxxMake::getIndexerCommands(const RefreshInfo& info) const
{
	ensureCompilationDatabase();
	return SourceGroupCxxCdb::getIndexerCommands(info);
}

void SourceGroupCxxMake::ensureCompilationDatabase() const
{
	m_settings->useGeneratedCompilationDatabase();
	if (!m_settings->getGeneratedCompilationDatabasePath().exists())
	{
		if (const auto written = writeCompilationDatabase(*m_settings); !written)
		{
			LOG_WARNING(make::describe(written.error()));
		}
	}
}
