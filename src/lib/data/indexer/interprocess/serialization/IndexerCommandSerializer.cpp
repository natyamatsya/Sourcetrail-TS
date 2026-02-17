#include "IndexerCommandSerializer.h"

#include "language_packages.h"

#include "IndexerCommand.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommandCxx.h"
#endif
#include "logging.h"

#include "indexer_command_generated.h"

namespace IpcSerializer
{

flatbuffers::DetachedBuffer serializeIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& commands)
{
	flatbuffers::FlatBufferBuilder builder(4096);

	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::IndexerCommand>> fbCommands;
	fbCommands.reserve(commands.size());

	for (const auto& cmd : commands)
	{
		auto type = Sourcetrail::Ipc::IndexerCommandType_Unknown;
		flatbuffers::Offset<flatbuffers::String> sourceFilePath =
			builder.CreateString(cmd->getSourceFilePath().str());

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> indexedPaths = 0;
		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> excludeFilters = 0;
		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> includeFilters = 0;
		flatbuffers::Offset<flatbuffers::String> workingDirectory = 0;
		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> compilerFlags = 0;

#if BUILD_CXX_LANGUAGE_PACKAGE
		if (auto* cxxCmd = dynamic_cast<IndexerCommandCxx*>(cmd.get()))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Cxx;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : cxxCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			std::vector<flatbuffers::Offset<flatbuffers::String>> excludes;
			for (const auto& f : cxxCmd->getExcludeFilters())
				excludes.push_back(builder.CreateString(f.str()));
			excludeFilters = builder.CreateVector(excludes);

			std::vector<flatbuffers::Offset<flatbuffers::String>> includes;
			for (const auto& f : cxxCmd->getIncludeFilters())
				includes.push_back(builder.CreateString(f.str()));
			includeFilters = builder.CreateVector(includes);

			workingDirectory = builder.CreateString(cxxCmd->getWorkingDirectory().str());

			std::vector<flatbuffers::Offset<flatbuffers::String>> flags;
			for (const auto& flag : cxxCmd->getCompilerFlags())
				flags.push_back(builder.CreateString(flag));
			compilerFlags = builder.CreateVector(flags);
		}
#endif

		fbCommands.push_back(Sourcetrail::Ipc::CreateIndexerCommand(
			builder, type, sourceFilePath, indexedPaths, excludeFilters,
			includeFilters, workingDirectory, compilerFlags));
	}

	auto queue = Sourcetrail::Ipc::CreateIndexerCommandQueue(
		builder, builder.CreateVector(fbCommands));
	builder.Finish(queue);

	return builder.Release();
}

std::vector<std::shared_ptr<IndexerCommand>> deserializeIndexerCommands(
	const uint8_t* buf, std::size_t /*len*/)
{
	std::vector<std::shared_ptr<IndexerCommand>> result;

	auto queue = Sourcetrail::Ipc::GetIndexerCommandQueue(buf);
	if (!queue || !queue->commands())
		return result;

	for (const auto* fbCmd : *queue->commands())
	{
		if (!fbCmd)
			continue;

		switch (fbCmd->type())
		{
#if BUILD_CXX_LANGUAGE_PACKAGE
		case Sourcetrail::Ipc::IndexerCommandType_Cxx:
		{
			std::set<FilePath> indexedPaths;
			if (fbCmd->indexed_paths())
				for (const auto* p : *fbCmd->indexed_paths())
					indexedPaths.insert(FilePath(p->c_str()));

			std::set<FilePathFilter> excludeFilters;
			if (fbCmd->exclude_filters())
				for (const auto* f : *fbCmd->exclude_filters())
					excludeFilters.insert(FilePathFilter(f->c_str()));

			std::set<FilePathFilter> includeFilters;
			if (fbCmd->include_filters())
				for (const auto* f : *fbCmd->include_filters())
					includeFilters.insert(FilePathFilter(f->c_str()));

			FilePath workingDir;
			if (fbCmd->working_directory())
				workingDir = FilePath(fbCmd->working_directory()->c_str());

			std::vector<std::string> compilerFlags;
			if (fbCmd->compiler_flags())
				for (const auto* flag : *fbCmd->compiler_flags())
					compilerFlags.push_back(flag->c_str());

			result.push_back(std::make_shared<IndexerCommandCxx>(
				FilePath(fbCmd->source_file_path()->c_str()),
				indexedPaths, excludeFilters, includeFilters,
				workingDir, compilerFlags));
			break;
		}
#endif
		default:
			LOG_ERROR(
				"Cannot deserialize IndexerCommand for file: " +
				std::string(fbCmd->source_file_path() ? fbCmd->source_file_path()->c_str() : "<null>") +
				". Unknown type.");
			break;
		}
	}

	return result;
}

}
