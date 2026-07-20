// `srctrl.data:types` partition -- small self-contained data enums/types. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#endif

export module srctrl.data:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

// Cross-module dependency: LocationType uses utilityEnum's intToEnum/lookupEnum. Importing before the
// purview means the header build's `#include "utilityEnum.h"` is skipped and these come from the module
// instead. This is the migration's first inter-module `import`.
import srctrl.utility;

#define SRCTRL_MODULE_PURVIEW
#include "TooltipOrigin.h"
#include "NameDelimiterType.h"
#include "LocationType.h"
// More intToEnum-specializing data enums (same cross-module pattern as LocationType). AccessKind and
// DefinitionKind are consumed by the graph token-components / Node, so they live in :types (the graph
// partition imports :types) rather than being duplicated into the GMF.
#include "ElementComponentKind.h"
#include "DefinitionKind.h"
#include "AccessKind.h"
#include "SymbolKind.h"
// Node classification enums the graph core (Node) needs: NodeKind (intToEnum-specializing, like the
// above) and NodeModifier (a plain bitmask enum + nodeModifierToString). Kept in :types so :graph and
// the storage layer import them rather than duplicating them into a GMF.
#include "NodeKind.h"
#include "NodeModifier.h"
// NodeAttributeKind (sparse node_attribute key) -- another intToEnum enum, consumed by StorageNodeAttribute.
#include "NodeAttributeKind.h"
