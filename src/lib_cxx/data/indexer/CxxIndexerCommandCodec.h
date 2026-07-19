#ifndef CXX_INDEXER_COMMAND_CODEC_H
#define CXX_INDEXER_COMMAND_CODEC_H

// Registers the Cxx IndexerCommand codec into the (lib-resident) IndexerCommandCodecRegistry. Each
// executable that links lib_cxx (the app and the indexer subprocess) calls this next to its
// LanguagePackageCxx registration, so lib's IndexerCommandSerializer can (de)serialize Cxx commands
// without lib itself depending on lib_cxx.
void registerCxxIndexerCommandCodec();

#endif	  // CXX_INDEXER_COMMAND_CODEC_H
