// `srctrl.data:graph` partition -- the token graph's leaf layer: the TokenComponent polymorphic base
// and its ~9 concrete subtypes. Module build only.
//
// Node/Edge/Graph/Token (the interconnected graph *core*) are a later, larger step; this partition is
// the self-contained token_component/ cluster plus nothing else.

module;

// Global module fragment: std + the non-modularized deps (FilePath, Id via types.h) stay global-module.
// AccessKind is NOT here -- it's an intToEnum-specializing enum in :types, imported below.
#include <map>
#include <memory>
#include <set>
#include <string>
#include "types.h"
#include "FilePath.h"

export module srctrl.data:graph;

import :types;   // AccessKind (TokenComponentAccess)

#define SRCTRL_MODULE_PURVIEW
// Base first so it is complete before every derived class pulls it in (their own guarded
// `#include "TokenComponent.h"` is skipped in the purview).
#include "TokenComponent.h"
#include "TokenComponentAbstraction.h"
#include "TokenComponentAccess.h"
#include "TokenComponentBundledEdges.h"
#include "TokenComponentConst.h"
#include "TokenComponentFilePath.h"
#include "TokenComponentStatic.h"
#include "TokenComponentInheritanceChain.h"
#include "TokenComponentIsAmbiguous.h"
