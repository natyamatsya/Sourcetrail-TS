#include "IntermediateStorageSerializer.h"

#include "IntermediateStorage.h"

#include "intermediate_storage_generated.h"

namespace IpcSerializer
{

flatbuffers::DetachedBuffer serializeIntermediateStorage(const IntermediateStorage& storage)
{
	flatbuffers::FlatBufferBuilder builder(16384);

	// Nodes
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageNode>> fbNodes;
	for (const auto& n : storage.getStorageNodes())
		fbNodes.push_back(Sourcetrail::Ipc::CreateStorageNode(
			builder, static_cast<int64_t>(n.id), static_cast<int32_t>(n.type),
			builder.CreateString(n.serializedName), static_cast<int32_t>(n.modifiers)));

	// Files
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageFile>> fbFiles;
	for (const auto& f : storage.getStorageFiles())
		fbFiles.push_back(Sourcetrail::Ipc::CreateStorageFile(
			builder, static_cast<int64_t>(f.id),
			builder.CreateString(f.filePath),
			builder.CreateString(f.languageIdentifier),
			f.indexed, f.complete));

	// Edges
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageEdge>> fbEdges;
	for (const auto& e : storage.getStorageEdges())
		fbEdges.push_back(Sourcetrail::Ipc::CreateStorageEdge(
			builder, static_cast<int64_t>(e.id), static_cast<int32_t>(e.type),
			static_cast<int64_t>(e.sourceNodeId), static_cast<int64_t>(e.targetNodeId)));

	// Symbols
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageSymbol>> fbSymbols;
	for (const auto& s : storage.getStorageSymbols())
		fbSymbols.push_back(Sourcetrail::Ipc::CreateStorageSymbol(
			builder, static_cast<int64_t>(s.id), static_cast<int32_t>(s.definitionKind)));

	// SourceLocations
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageSourceLocation>> fbLocs;
	for (const auto& loc : storage.getStorageSourceLocations())
		fbLocs.push_back(Sourcetrail::Ipc::CreateStorageSourceLocation(
			builder, static_cast<int64_t>(loc.id), static_cast<int64_t>(loc.fileNodeId),
			static_cast<uint32_t>(loc.startLine), static_cast<uint32_t>(loc.startCol),
			static_cast<uint32_t>(loc.endLine), static_cast<uint32_t>(loc.endCol),
			static_cast<int32_t>(loc.type)));

	// LocalSymbols
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageLocalSymbol>> fbLocalSyms;
	for (const auto& ls : storage.getStorageLocalSymbols())
		fbLocalSyms.push_back(Sourcetrail::Ipc::CreateStorageLocalSymbol(
			builder, static_cast<int64_t>(ls.id), builder.CreateString(ls.name)));

	// Occurrences
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageOccurrence>> fbOccs;
	for (const auto& o : storage.getStorageOccurrences())
		fbOccs.push_back(Sourcetrail::Ipc::CreateStorageOccurrence(
			builder, static_cast<int64_t>(o.elementId), static_cast<int64_t>(o.sourceLocationId)));

	// ComponentAccesses
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageComponentAccess>> fbAccesses;
	for (const auto& a : storage.getComponentAccesses())
		fbAccesses.push_back(Sourcetrail::Ipc::CreateStorageComponentAccess(
			builder, static_cast<int64_t>(a.nodeId), static_cast<int32_t>(a.type)));

	// NodeAttributes
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageNodeAttribute>> fbNodeAttributes;
	for (const auto& attr : storage.getNodeAttributes())
		fbNodeAttributes.push_back(Sourcetrail::Ipc::CreateStorageNodeAttribute(
			builder, static_cast<int64_t>(attr.nodeId), static_cast<int32_t>(attr.key),
			builder.CreateString(attr.value)));

	// Errors
	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::StorageError>> fbErrors;
	for (const auto& err : storage.getErrors())
		fbErrors.push_back(Sourcetrail::Ipc::CreateStorageError(
			builder, static_cast<int64_t>(err.id),
			builder.CreateString(err.message),
			builder.CreateString(err.translationUnit),
			err.fatal, err.indexed));

	auto fbStorage = Sourcetrail::Ipc::CreateIntermediateStorage(
		builder,
		static_cast<int64_t>(storage.getNextId()),
		builder.CreateVector(fbNodes),
		builder.CreateVector(fbFiles),
		builder.CreateVector(fbEdges),
		builder.CreateVector(fbSymbols),
		builder.CreateVector(fbLocs),
		builder.CreateVector(fbLocalSyms),
		builder.CreateVector(fbOccs),
		builder.CreateVector(fbAccesses),
		builder.CreateVector(fbNodeAttributes),
		builder.CreateVector(fbErrors));

	auto queue = Sourcetrail::Ipc::CreateIntermediateStorageQueue(
		builder, builder.CreateVector(
			std::vector<flatbuffers::Offset<Sourcetrail::Ipc::IntermediateStorage>>{fbStorage}));

	builder.Finish(queue);
	return builder.Release();
}

std::shared_ptr<IntermediateStorage> deserializeIntermediateStorage(
	const uint8_t* buf, std::size_t /*len*/)
{
	auto queue = Sourcetrail::Ipc::GetIntermediateStorageQueue(buf);
	if (!queue || !queue->storages() || queue->storages()->size() == 0)
		return nullptr;

	const auto* fb = queue->storages()->Get(0);
	auto storage = std::make_shared<IntermediateStorage>();

	storage->setNextId(Id(fb->next_id()));

	// Nodes
	if (fb->nodes())
	{
		std::vector<StorageNode> nodes;
		for (const auto* n : *fb->nodes())
			nodes.emplace_back(Id(n->id()), static_cast<NodeKind>(n->type()),
				n->serialized_name() ? n->serialized_name()->str() : "",
				static_cast<NodeModifierMask>(n->modifiers()));
		storage->setStorageNodes(std::move(nodes));
	}

	// Files
	if (fb->files())
	{
		std::vector<StorageFile> files;
		for (const auto* f : *fb->files())
			files.emplace_back(Id(f->id()),
				f->file_path() ? f->file_path()->str() : "",
				f->language_identifier() ? f->language_identifier()->str() : "",
				"", f->indexed(), f->complete());
		storage->setStorageFiles(std::move(files));
	}

	// Edges
	if (fb->edges())
	{
		std::vector<StorageEdge> edges;
		for (const auto* e : *fb->edges())
			edges.emplace_back(Id(e->id()), static_cast<Edge::EdgeType>(e->type()),
				Id(e->source_node_id()), Id(e->target_node_id()));
		storage->setStorageEdges(std::move(edges));
	}

	// Symbols
	if (fb->symbols())
	{
		std::vector<StorageSymbol> symbols;
		for (const auto* s : *fb->symbols())
			symbols.emplace_back(Id(s->id()), static_cast<DefinitionKind>(s->definition_kind()));
		storage->setStorageSymbols(std::move(symbols));
	}

	// SourceLocations
	if (fb->source_locations())
	{
		std::set<StorageSourceLocation> locs;
		for (const auto* loc : *fb->source_locations())
			locs.emplace(Id(loc->id()), Id(loc->file_node_id()),
				loc->start_line(), loc->start_col(),
				loc->end_line(), loc->end_col(),
				static_cast<LocationType>(loc->type()));
		storage->setStorageSourceLocations(std::move(locs));
	}

	// LocalSymbols
	if (fb->local_symbols())
	{
		std::set<StorageLocalSymbol> localSyms;
		for (const auto* ls : *fb->local_symbols())
			localSyms.emplace(Id(ls->id()), ls->name() ? ls->name()->str() : "");
		storage->setStorageLocalSymbols(std::move(localSyms));
	}

	// Occurrences
	if (fb->occurrences())
	{
		std::set<StorageOccurrence> occs;
		for (const auto* o : *fb->occurrences())
			occs.emplace(Id(o->element_id()), Id(o->source_location_id()));
		storage->setStorageOccurrences(std::move(occs));
	}

	// ComponentAccesses
	if (fb->component_accesses())
	{
		std::set<StorageComponentAccess> accesses;
		for (const auto* a : *fb->component_accesses())
			accesses.emplace(Id(a->node_id()), static_cast<AccessKind>(a->type()));
		storage->setComponentAccesses(std::move(accesses));
	}

	// NodeAttributes
	if (fb->node_attributes())
	{
		std::set<StorageNodeAttribute> nodeAttributes;
		for (const auto* attr : *fb->node_attributes())
			nodeAttributes.emplace(Id(attr->node_id()),
				static_cast<NodeAttributeKind>(attr->key()),
				attr->value() ? attr->value()->str() : "");
		storage->setNodeAttributes(std::move(nodeAttributes));
	}

	// Errors
	if (fb->errors())
	{
		std::vector<StorageError> errors;
		for (const auto* err : *fb->errors())
			errors.emplace_back(Id(err->id()),
				err->message() ? err->message()->str() : "",
				err->translation_unit() ? err->translation_unit()->str() : "",
				err->fatal(), err->indexed());
		storage->setErrors(std::move(errors));
	}

	return storage;
}

}
