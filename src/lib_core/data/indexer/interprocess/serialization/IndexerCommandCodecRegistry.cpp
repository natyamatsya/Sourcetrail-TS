#include "IndexerCommandCodecRegistry.h"

#include "IndexerCommand.h"

#include <set>
#include <string>
#include <vector>

#include "FilePath.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"

namespace
{
namespace Ipc = Sourcetrail::Ipc;

// Codec provider for Rust indexer commands. A plain value satisfying IndexerCommandCodecC (no base class).
struct RustIndexerCommandCodec
{
	flatbuffers::Offset<Ipc::IndexerCommand> serialize(
		flatbuffers::FlatBufferBuilder& builder, const IndexerCommand& base) const
	{
		// Safe: the registry only invokes this codec for commands whose payload is IndexerCommandRust.
		const IndexerCommandRust& cmd = *base.target<IndexerCommandRust>();

		std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
		for (const auto& p: cmd.getIndexedPaths())
			paths.push_back(builder.CreateString(p.str()));

		std::vector<flatbuffers::Offset<flatbuffers::String>> feats;
		for (const auto& f: cmd.getFeatures())
			feats.push_back(builder.CreateString(f));

		return Ipc::CreateIndexerCommand(
			builder, Ipc::IndexerCommandType_Rust,
			builder.CreateString(base.getSourceFilePath().str()),
			builder.CreateVector(paths), 0, 0,
			builder.CreateString(cmd.getWorkingDirectory().str()), 0, 0,
			builder.CreateVector(feats), cmd.getAllFeatures(), cmd.getNoDefaultFeatures(),
			builder.CreateString(cmd.getTargetTriple()),
			builder.CreateString(cmd.getSpecializationScope()),
			builder.CreateString(base.getSourceGroupId()), cmd.getRestrictToPackage(),
			0, 0, 0, 0);
	}

	std::shared_ptr<IndexerCommand> deserialize(const Ipc::IndexerCommand& fbCmd) const
	{
		std::set<FilePath> indexedPaths;
		if (fbCmd.indexed_paths())
			for (const auto* p: *fbCmd.indexed_paths())
				indexedPaths.insert(FilePath(p->c_str()));

		FilePath workingDir;
		if (fbCmd.working_directory())
			workingDir = FilePath(fbCmd.working_directory()->c_str());

		std::vector<std::string> features;
		if (fbCmd.features())
			for (const auto* f: *fbCmd.features())
				features.push_back(f->str());

		std::string targetTriple;
		if (fbCmd.target_triple())
			targetTriple = fbCmd.target_triple()->str();

		std::string specializationScope;
		if (fbCmd.specialization_scope())
			specializationScope = fbCmd.specialization_scope()->str();

		return std::make_shared<IndexerCommand>(
			FilePath(fbCmd.source_file_path()->c_str()),
			IndexerCommandRust(indexedPaths, workingDir, features, fbCmd.all_features(), fbCmd.no_default_features(),
			targetTriple, specializationScope, fbCmd.restrict_to_package()));
	}
};

// Codec provider for Swift indexer commands.
struct SwiftIndexerCommandCodec
{
	flatbuffers::Offset<Ipc::IndexerCommand> serialize(
		flatbuffers::FlatBufferBuilder& builder, const IndexerCommand& base) const
	{
		const IndexerCommandSwift& cmd = *base.target<IndexerCommandSwift>();

		std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
		for (const auto& p: cmd.getIndexedPaths())
			paths.push_back(builder.CreateString(p.str()));

		std::vector<flatbuffers::Offset<flatbuffers::String>> swiftArgs;
		for (const auto& a: cmd.getBuildArgs())
			swiftArgs.push_back(builder.CreateString(a));

		return Ipc::CreateIndexerCommand(
			builder, Ipc::IndexerCommandType_Swift,
			builder.CreateString(base.getSourceFilePath().str()),
			builder.CreateVector(paths), 0, 0,
			builder.CreateString(cmd.getWorkingDirectory().str()), 0, 0,
			0, false, false, 0, 0,
			builder.CreateString(base.getSourceGroupId()), false,
			builder.CreateVector(swiftArgs),
			builder.CreateString(cmd.getToolchainPath()),
			builder.CreateString(cmd.getIndexStorePath()),
			builder.CreateString(cmd.getSpecializationScope()));
	}

	std::shared_ptr<IndexerCommand> deserialize(const Ipc::IndexerCommand& fbCmd) const
	{
		std::set<FilePath> indexedPaths;
		if (fbCmd.indexed_paths())
			for (const auto* p: *fbCmd.indexed_paths())
				indexedPaths.insert(FilePath(p->c_str()));

		FilePath workingDir;
		if (fbCmd.working_directory())
			workingDir = FilePath(fbCmd.working_directory()->c_str());

		std::vector<std::string> swiftBuildArgs;
		if (fbCmd.swift_build_args())
			for (const auto* a: *fbCmd.swift_build_args())
				swiftBuildArgs.push_back(a->str());

		std::string swiftToolchainPath;
		if (fbCmd.swift_toolchain_path())
			swiftToolchainPath = fbCmd.swift_toolchain_path()->str();

		std::string swiftIndexStorePath;
		if (fbCmd.swift_index_store_path())
			swiftIndexStorePath = fbCmd.swift_index_store_path()->str();

		std::string swiftSpecializationScope;
		if (fbCmd.swift_specialization_scope())
			swiftSpecializationScope = fbCmd.swift_specialization_scope()->str();

		return std::make_shared<IndexerCommand>(
			FilePath(fbCmd.source_file_path()->c_str()),
			IndexerCommandSwift(indexedPaths, workingDir, swiftBuildArgs, swiftToolchainPath, swiftIndexStorePath,
			swiftSpecializationScope));
	}
};
}	 // namespace

IndexerCommandCodecRegistry& IndexerCommandCodecRegistry::getInstance()
{
	static IndexerCommandCodecRegistry instance;
	return instance;
}

IndexerCommandCodecRegistry::IndexerCommandCodecRegistry()
{
	// The languages `lib` owns register up front; `lib_cxx` adds the Cxx codec via
	// registerCxxIndexerCommandCodec() at its LanguagePackageCxx registration point.
	registerCodec(IndexerCommandType::INDEXER_COMMAND_RUST, eraseIndexerCommandCodec(RustIndexerCommandCodec{}));
	registerCodec(IndexerCommandType::INDEXER_COMMAND_SWIFT, eraseIndexerCommandCodec(SwiftIndexerCommandCodec{}));
}

void IndexerCommandCodecRegistry::registerCodec(IndexerCommandType type, IndexerCommandCodec codec)
{
	m_codecs[type] = std::move(codec);
}

const IndexerCommandCodec* IndexerCommandCodecRegistry::find(IndexerCommandType type) const
{
	const auto it = m_codecs.find(type);
	return it == m_codecs.end() ? nullptr : &it->second;
}
