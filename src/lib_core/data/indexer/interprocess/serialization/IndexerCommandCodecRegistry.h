#ifndef INDEXER_COMMAND_CODEC_REGISTRY_H
#define INDEXER_COMMAND_CODEC_REGISTRY_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>

#include "IndexerCommandCodec.h"
#include "IndexerCommandType.h"
#endif

// Process-wide registry mapping each IndexerCommandType to its type-erased codec. `lib` registers the
// codecs for the languages it owns (Rust, Swift); `lib_cxx` registers the Cxx codec at the same point it
// registers LanguagePackageCxx (see registerCxxIndexerCommandCodec). IndexerCommandSerializer looks codecs
// up here, so it never depends on a concrete command class.
SRCTRL_EXPORT class IndexerCommandCodecRegistry
{
public:
	static IndexerCommandCodecRegistry& getInstance();

	void registerCodec(IndexerCommandType type, IndexerCommandCodec codec);
	const IndexerCommandCodec* find(IndexerCommandType type) const;

private:
	IndexerCommandCodecRegistry();

	std::map<IndexerCommandType, IndexerCommandCodec> m_codecs;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommandCodecRegistry.inl"
#endif

#endif	  // INDEXER_COMMAND_CODEC_REGISTRY_H
