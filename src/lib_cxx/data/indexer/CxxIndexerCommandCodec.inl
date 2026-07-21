// Inline implementations for CxxIndexerCommandCodec.h. Included at the end of that header (classic)
// or via the srctrl.cxx:package wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <set>
#include <string>
#include <vector>

#include "FilePath.h"
#include "FilePathFilter.h"
#include "IndexerCommand.h"
#include "IndexerCommandCodec.h"
#include "IndexerCommandCodecRegistry.h"
#include "IndexerCommandCxx.h"
#include "IndexerCommandType.h"
#endif

// ODR-safe home for the codec provider (anonymous namespaces are an ODR trap in headers/inls).
namespace cxx_indexer_command_codec_detail
{
namespace Ipc = Sourcetrail::Ipc;

// Codec provider for Cxx indexer commands. A plain value satisfying IndexerCommandCodecC (no base class).
// Lives in lib_cxx because it is the only place that names the concrete IndexerCommandCxx.
struct CxxIndexerCommandCodecProvider
{
	flatbuffers::Offset<Ipc::IndexerCommand> serialize(
		flatbuffers::FlatBufferBuilder& builder, const IndexerCommand& base) const
	{
		// Safe: the registry only invokes this codec for commands whose type is INDEXER_COMMAND_CXX.
		const IndexerCommandCxx& cmd = *base.target<IndexerCommandCxx>();

		std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
		for (const auto& p: cmd.getIndexedPaths())
			paths.push_back(builder.CreateString(p.str()));

		std::vector<flatbuffers::Offset<flatbuffers::String>> exc;
		for (const auto& f: cmd.getExcludeFilters())
			exc.push_back(builder.CreateString(f.str()));

		std::vector<flatbuffers::Offset<flatbuffers::String>> inc;
		for (const auto& f: cmd.getIncludeFilters())
			inc.push_back(builder.CreateString(f.str()));

		std::vector<flatbuffers::Offset<flatbuffers::String>> flags;
		for (const auto& f: cmd.getCompilerFlags())
			flags.push_back(builder.CreateString(f));

		return Ipc::CreateIndexerCommand(
			builder, Ipc::IndexerCommandType_Cxx,
			builder.CreateString(base.getSourceFilePath().str()),
			builder.CreateVector(paths), builder.CreateVector(exc), builder.CreateVector(inc),
			builder.CreateString(cmd.getWorkingDirectory().str()),
			builder.CreateVector(flags), builder.CreateString(cmd.getCompilerPath()),
			0, false, false, 0, 0,
			builder.CreateString(base.getSourceGroupId()), false,
			0, 0, 0, 0);
	}

	std::shared_ptr<IndexerCommand> deserialize(const Ipc::IndexerCommand& fbCmd) const
	{
		std::set<FilePath> indexedPaths;
		if (fbCmd.indexed_paths())
			for (const auto* p: *fbCmd.indexed_paths())
				indexedPaths.insert(FilePath(p->c_str()));

		std::set<FilePathFilter> excludeFilters;
		if (fbCmd.exclude_filters())
			for (const auto* f: *fbCmd.exclude_filters())
				excludeFilters.insert(FilePathFilter(f->c_str()));

		std::set<FilePathFilter> includeFilters;
		if (fbCmd.include_filters())
			for (const auto* f: *fbCmd.include_filters())
				includeFilters.insert(FilePathFilter(f->c_str()));

		FilePath workingDir;
		if (fbCmd.working_directory())
			workingDir = FilePath(fbCmd.working_directory()->c_str());

		std::vector<std::string> compilerFlags;
		if (fbCmd.compiler_flags())
			for (const auto* flag: *fbCmd.compiler_flags())
				compilerFlags.push_back(flag->c_str());

		std::string compilerPath;
		if (fbCmd.compiler_path())
			compilerPath = fbCmd.compiler_path()->c_str();

		return std::make_shared<IndexerCommand>(
			FilePath(fbCmd.source_file_path()->c_str()),
			IndexerCommandCxx(
				FilePath(fbCmd.source_file_path()->c_str()),
				indexedPaths, excludeFilters, includeFilters, workingDir, compilerFlags, compilerPath));
	}
};
}	 // namespace cxx_indexer_command_codec_detail

inline void registerCxxIndexerCommandCodec()
{
	IndexerCommandCodecRegistry::getInstance().registerCodec(
		IndexerCommandType::INDEXER_COMMAND_CXX,
		eraseIndexerCommandCodec(cxx_indexer_command_codec_detail::CxxIndexerCommandCodecProvider{}));
}
