#include "IndexerCommandSerializer.h"

#include "language_package_flags.h"

#include "IndexerCommand.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommandCxx.h"
#endif
#if BUILD_RUST_LANGUAGE_PACKAGE
#include "IndexerCommandRust.h"
#endif
#if BUILD_SWIFT_LANGUAGE_PACKAGE
#include "IndexerCommandSwift.h"
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
		flatbuffers::Offset<flatbuffers::String> compilerPath = 0;

#if BUILD_CXX_LANGUAGE_PACKAGE
		if (auto cxxCmd = std::dynamic_pointer_cast<IndexerCommandCxx>(cmd))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Cxx;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : cxxCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			std::vector<flatbuffers::Offset<flatbuffers::String>> exc;
			for (const auto& f : cxxCmd->getExcludeFilters())
				exc.push_back(builder.CreateString(f.str()));
			excludeFilters = builder.CreateVector(exc);

			std::vector<flatbuffers::Offset<flatbuffers::String>> inc;
			for (const auto& f : cxxCmd->getIncludeFilters())
				inc.push_back(builder.CreateString(f.str()));
			includeFilters = builder.CreateVector(inc);

			workingDirectory = builder.CreateString(cxxCmd->getWorkingDirectory().str());

			std::vector<flatbuffers::Offset<flatbuffers::String>> flags;
			for (const auto& f : cxxCmd->getCompilerFlags())
				flags.push_back(builder.CreateString(f));
			compilerFlags = builder.CreateVector(flags);

			compilerPath = builder.CreateString(cxxCmd->getCompilerPath());
		}
#endif
#if BUILD_RUST_LANGUAGE_PACKAGE
		if (auto* rustCmd = dynamic_cast<IndexerCommandRust*>(cmd.get()))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Rust;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : rustCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			workingDirectory = builder.CreateString(rustCmd->getWorkingDirectory().str());
		}
#endif
#if BUILD_SWIFT_LANGUAGE_PACKAGE
		if (auto* swiftCmd = dynamic_cast<IndexerCommandSwift*>(cmd.get()))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Swift;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : swiftCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			workingDirectory = builder.CreateString(swiftCmd->getWorkingDirectory().str());
		}
#endif

		fbCommands.push_back(Sourcetrail::Ipc::CreateIndexerCommand(
			builder, type, sourceFilePath, indexedPaths, excludeFilters,
			includeFilters, workingDirectory, compilerFlags, compilerPath));
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

			std::string compilerPath;
			if (fbCmd->compiler_path())
				compilerPath = fbCmd->compiler_path()->c_str();

			result.push_back(std::make_shared<IndexerCommandCxx>(
				FilePath(fbCmd->source_file_path()->c_str()),
				indexedPaths, excludeFilters, includeFilters,
				workingDir, compilerFlags, compilerPath));
			break;
		}
#endif
#if BUILD_RUST_LANGUAGE_PACKAGE
		case Sourcetrail::Ipc::IndexerCommandType_Rust:
		{
			std::set<FilePath> indexedPaths;
			if (fbCmd->indexed_paths())
				for (const auto* p : *fbCmd->indexed_paths())
					indexedPaths.insert(FilePath(p->c_str()));

			FilePath workingDir;
			if (fbCmd->working_directory())
				workingDir = FilePath(fbCmd->working_directory()->c_str());

			result.push_back(std::make_shared<IndexerCommandRust>(
				FilePath(fbCmd->source_file_path()->c_str()),
				indexedPaths, workingDir));
			break;
		}
#endif
#if BUILD_SWIFT_LANGUAGE_PACKAGE
		case Sourcetrail::Ipc::IndexerCommandType_Swift:
		{
			std::set<FilePath> indexedPaths;
			if (fbCmd->indexed_paths())
				for (const auto* p : *fbCmd->indexed_paths())
					indexedPaths.insert(FilePath(p->c_str()));

			FilePath workingDir;
			if (fbCmd->working_directory())
				workingDir = FilePath(fbCmd->working_directory()->c_str());

			result.push_back(std::make_shared<IndexerCommandSwift>(
				FilePath(fbCmd->source_file_path()->c_str()),
				indexedPaths, workingDir));
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
