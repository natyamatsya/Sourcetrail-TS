// `srctrl.messaging` -- the message-bus mesh (MessageBase/MessageQueue/MessageListener/Message)
// and ALL message types (type/ + filter_types/). Attached wholesale: a converted TU cannot mix
// imported mesh with textual type headers (two MessageBase entities in one TU), so the family is
// all-or-nothing. MessageQueue's stdexec run-loop stays behind its pimpl in the CLASSIC
// MessageQueue.cpp forever (stdexec breaks clang-scan-deps; members of attached classes keep
// ordinary mangling, so the classic definitions serve both worlds). Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <expected>
#include <thread>
#include <vector>
#endif

// Non-modularized GMF deps, classic impls linked: TabIds (component-layer statics; also carries
// Q_DECLARE_METATYPE, so it stays global with its Qt macro machinery textual).
#include "TabIds.h"
// IndexingOutcome's textual deps (it is honorary family, attached via MessageIndexingFinished):
// std::expected via the std block above in non-import-std mode; utilityExpected is classic-clean.
#include "utilityExpected.h"

#include "types.h"   // Id (classic-global, Qt metatype machinery)

export module srctrl.messaging;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;    // utilityString/utilityEnum/Status/Vector2
import srctrl.file;       // FilePath payloads
import srctrl.data;       // Edge/Node/NameHierarchy/SearchMatch/Bookmark/Tooltip/Error payloads
import srctrl.storage;    // ErrorFilter/ErrorCountInfo (:error)
import srctrl.view;       // CodeScrollParams (code-view messages)
import srctrl.logging;    // Logger (MessageLogFilterChanged) + LOG_* backend

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (backend imported).
#include "logging.h"
// The mesh in dependency order, then every message type (self-resolving: family-internal
// includes are unguarded and headers precede use).
#include "MessageBase.h"
#include "MessageQueue.h"
#include "MessageFilter.h"
#include "MessageListenerBase.h"
#include "MessageListener.h"
#include "Message.h"
#include "MessageActivateBase.h"
#include "MessageActivateEdge.h"
#include "MessageActivateErrors.h"
#include "MessageActivateFile.h"
#include "MessageActivateFullTextSearch.h"
#include "MessageActivateLegend.h"
#include "MessageActivateLocalSymbols.h"
#include "MessageActivateNodes.h"
#include "MessageActivateOverview.h"
#include "MessageActivateSourceLocations.h"
#include "MessageActivateTokenIds.h"
#include "MessageActivateTokens.h"
#include "MessageActivateTrail.h"
#include "MessageActivateTrailEdge.h"
#include "MessageActivateWindow.h"
#include "MessageBookmarkActivate.h"
#include "MessageBookmarkBrowse.h"
#include "MessageBookmarkButtonState.h"
#include "MessageBookmarkCreate.h"
#include "MessageBookmarkDelete.h"
#include "MessageBookmarkEdit.h"
#include "MessageChangeFileView.h"
#include "MessageClearStatusView.h"
#include "MessageCloseProject.h"
#include "MessageCodeReference.h"
#include "MessageCodeShowDefinition.h"
#include "MessageCustomTrailShow.h"
#include "MessageDeactivateEdge.h"
#include "MessageErrorCountClear.h"
#include "MessageErrorCountUpdate.h"
#include "MessageErrorsAll.h"
#include "MessageErrorsForFile.h"
#include "MessageErrorsHelpMessage.h"
#include "MessageFilterErrorCountUpdate.h"
#include "MessageFilterFocusInOut.h"
#include "MessageFilterSearchAutocomplete.h"
#include "MessageFind.h"
#include "MessageFlushUpdates.h"
#include "MessageFocusChanged.h"
#include "MessageFocusIn.h"
#include "MessageFocusOut.h"
#include "MessageFocusView.h"
#include "MessageFocusedSearchView.h"
#include "MessageGraphNodeBundleSplit.h"
#include "MessageGraphNodeExpand.h"
#include "MessageGraphNodeHide.h"
#include "MessageGraphNodeMove.h"
#include "MessageHistoryRedo.h"
#include "MessageHistoryToPosition.h"
#include "MessageHistoryUndo.h"
#include "MessageIndexingFinished.h"
#include "MessageIndexingInterrupted.h"
#include "MessageIndexingShowDialog.h"
#include "MessageIndexingStarted.h"
#include "MessageIndexingStatus.h"
#include "MessageLoadProject.h"
#include "MessageLogFilterChanged.h"
#include "MessageMoveIDECursor.h"
#include "MessagePingReceived.h"
#include "MessagePluginPortChange.h"
#include "MessageProjectEdit.h"
#include "MessageProjectNew.h"
#include "MessageQuitApplication.h"
#include "MessageRefresh.h"
#include "MessageRefreshUI.h"
#include "MessageRefreshUIState.h"
#include "MessageResetZoom.h"
#include "MessageSaveAsImage.h"
#include "MessageScrollCode.h"
#include "MessageScrollGraph.h"
#include "MessageScrollSpeedChange.h"
#include "MessageScrollToLine.h"
#include "MessageSearch.h"
#include "MessageSearchAutocomplete.h"
#include "MessageShowError.h"
#include "MessageShowReference.h"
#include "MessageShowScope.h"
#include "MessageShowStatus.h"
#include "MessageStatus.h"
#include "MessageStatusFilterChanged.h"
#include "MessageSwitchColorScheme.h"
#include "MessageTabActivate.h"
#include "MessageTabClose.h"
#include "MessageTabOpen.h"
#include "MessageTabOpenWith.h"
#include "MessageTabSelect.h"
#include "MessageTabState.h"
#include "MessageTabsChanged.h"
#include "MessageTextEncodingChanged.h"
#include "MessageToNextCodeReference.h"
#include "MessageTooltipHide.h"
#include "MessageTooltipShow.h"
#include "MessageWindowChanged.h"
#include "MessageWindowClosed.h"
#include "MessageWindowFocus.h"
#include "MessageZoom.h"

// close the purview-wide extern "C++" linkage block
}
