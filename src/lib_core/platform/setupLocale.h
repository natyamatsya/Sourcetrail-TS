#ifndef SETUP_LOCALE_H
#define SETUP_LOCALE_H

// Split out of setupApp.h so FilePath-free consumers (the indexer binary) don't drag FilePath/Version
// through the app-bootstrap header. std-only and include-free: safe in every context.
void setupDefaultLocale();

#endif	  // SETUP_LOCALE_H
