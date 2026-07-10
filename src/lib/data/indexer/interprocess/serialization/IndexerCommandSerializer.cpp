#include "IndexerCommandSerializer.h"

#include "language_package_flags.h"

#include "IndexerCommand.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "IndexerCommandCxx.h"
#endif
#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"
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
		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> features = 0;
		bool allFeatures = false;
		bool noDefaultFeatures = false;
		flatbuffers::Offset<flatbuffers::String> targetTriple = 0;
		flatbuffers::Offset<flatbuffers::String> specializationScope = 0;

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
		if (auto* rustCmd = dynamic_cast<IndexerCommandRust*>(cmd.get()))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Rust;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : rustCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			workingDirectory = builder.CreateString(rustCmd->getWorkingDirectory().str());

			std::vector<flatbuffers::Offset<flatbuffers::String>> feats;
			for (const auto& f : rustCmd->getFeatures())
				feats.push_back(builder.CreateString(f));
			features = builder.CreateVector(feats);
			allFeatures = rustCmd->getAllFeatures();
			noDefaultFeatures = rustCmd->getNoDefaultFeatures();
			targetTriple = builder.CreateString(rustCmd->getTargetTriple());
			specializationScope = builder.CreateString(rustCmd->getSpecializationScope());
		}
		if (auto* swiftCmd = dynamic_cast<IndexerCommandSwift*>(cmd.get()))
		{
			type = Sourcetrail::Ipc::IndexerCommandType_Swift;

			std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
			for (const auto& p : swiftCmd->getIndexedPaths())
				paths.push_back(builder.CreateString(p.str()));
			indexedPaths = builder.CreateVector(paths);

			workingDirectory = builder.CreateString(swiftCmd->getWorkingDirectory().str());
		}

		fbCommands.push_back(Sourcetrail::Ipc::CreateIndexerCommand(
			builder, type, sourceFilePath, indexedPaths, excludeFilters,
			includeFilters, workingDirectory, compilerFlags, compilerPath,
			features, allFeatures, noDefaultFeatures, targetTriple,
			specializationScope));
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
		case Sourcetrail::Ipc::IndexerCommandType_Cxx:
#if BUILD_CXX_LANGUAGE_PACKAGE
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
			}
#endif
			break;
		case Sourcetrail::Ipc::IndexerCommandType_Rust:
		{
			std::set<FilePath> indexedPaths;
			if (fbCmd->indexed_paths())
				for (const auto* p : *fbCmd->indexed_paths())
					indexedPaths.insert(FilePath(p->c_str()));

			FilePath workingDir;
			if (fbCmd->working_directory())
				workingDir = FilePath(fbCmd->working_directory()->c_str());

			std::vector<std::string> features;
			if (fbCmd->features())
				for (const auto* f : *fbCmd->features())
					features.push_back(f->str());

			std::string targetTriple;
			if (fbCmd->target_triple())
				targetTriple = fbCmd->target_triple()->str();

			std::string specializationScope;
			if (fbCmd->specialization_scope())
				specializationScope = fbCmd->specialization_scope()->str();

			result.push_back(std::make_shared<IndexerCommandRust>(
				FilePath(fbCmd->source_file_path()->c_str()),
				indexedPaths, workingDir,
				features, fbCmd->all_features(), fbCmd->no_default_features(),
				targetTriple, specializationScope));
			break;
		}
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
