#include "AgentControlController.h"

#ifndef SOURCETRAIL_AGENT_CONTROL

// Flag off: no-op stub so the class is always declarable/linkable but does nothing
// and pulls in no thoth-ipc / FlatBuffers / stdexec dependencies.
struct AgentControlController::Impl
{
};
AgentControlController::AgentControlController(StorageAccess*, execution::ISchedulers*, const std::string&) {}
AgentControlController::~AgentControlController() = default;
void AgentControlController::startListening() {}
void AgentControlController::stopListening() {}
bool AgentControlController::isListening() const
{
	return false;
}

#else

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <atomic>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <thoth-ipc/ipc.h>
#include <thoth-ipc/async_recv.h>	// self-guards on THOTH_IPC_STDEXEC; empty when off

#include "QtUiSnapshot.h"	// Qt-free interface; captures the widget tree on the GUI thread

#include <flatbuffers/flatbuffers.h>

// Build identity (`git:<hash>+thoth-ipc:<version>`) stamped by src/lib/CMakeLists.txt
// (SOURCETRAIL_AGENT_CONTROL block) into AppInfo.build_id, so the MCP bridge can
// detect an app<->bridge thoth-ipc drift — the app-side half of the bridge's
// build.rs stamp. Empty when not stamped (e.g. a non-CMake build).
#ifndef AGENT_APP_BUILD_ID
#define AGENT_APP_BUILD_ID ""
#endif

#include "agent_command_generated.h"
#include "agent_event_generated.h"
#include "agent_frame_generated.h"
#include "agent_state_generated.h"

#include "ISchedulers.h"
#include "StdexecPrelude.h"

#include "Application.h"
#include "Edge.h"
#include "ErrorCountInfo.h"
#include "FilePath.h"
#include "DisplayedGraph.h"
#include "Graph.h"
#include "GraphControls.h"
#include "Id.h"
#include "LogManager.h"
#include "LogMessage.h"
#include "Logger.h"
#include "MessageListener.h"
#include "MessageQueue.h"
#include "QtLogBridge.h"
#include "QtUiControl.h"
#include "QtUiQuery.h"
#include "TabIds.h"
#include "TaskManager.h"
#include "TaskScheduler.h"
#include "NameHierarchy.h"
#include "Node.h"
#include "NodeType.h"
#include "NodeTypeSet.h"
#include "RefreshInfo.h"
#include "ActivationStateMachine.h"
#include "AppStateMachine.h"
#include "IndexingStateMachine.h"
#include "ProjectStateMachine.h"
#include "RemoteStateMachine.h"
#include "StorageAccess.h"
#include "Version.h"
#include "logging.h"

#include "Bookmark.h"
#include "BookmarkCategory.h"
#include "EdgeBookmark.h"
#include "NodeBookmark.h"
#include "SearchMatch.h"

#include "MessageAgentCommand.h"
#include "MessageCause.h"
#include "MessageActivateFile.h"
#include "MessageActivateNodes.h"
#include "MessageActivateOverview.h"
#include "MessageActivateTokens.h"
#include "MessageActivateTrail.h"
#include "MessageGraphNodeBundleSplit.h"
#include "MessageGraphNodeExpand.h"
#include "MessageGraphNodeHide.h"
#include "MessageErrorCountUpdate.h"
#include "MessageHistoryRedo.h"
#include "MessageHistoryUndo.h"
#include "MessageIndexingFinished.h"
#include "MessageIndexingStarted.h"
#include "MessageScrollToLine.h"
#include "MessageSearch.h"
#include "MessageTabActivate.h"
#include "MessageTabsChanged.h"

namespace fb = Sourcetrail::Agent;

namespace
{
// Channel name for `base` under an instance id. Empty id -> the default
// `st.agent.<base>`; otherwise `st.agent.<id>.<base>`, so multiple app instances
// (e.g. two checkouts in a comparison test) don't collide. Must match the bridge.
std::string agentChannel(const std::string& instanceId, const char* base)
{
	return instanceId.empty() ? (std::string("st.agent.") + base)
							  : ("st.agent." + instanceId + "." + base);
}

// The full set of agent-control channels, matched to the bridge's connect set
// (ipc.rs: cmd/state/events/frames/snapshot). Single source of truth so the
// startup wipe below can never drift from the routes the Impl opens.
constexpr const char* kAgentChannelBases[] = {"cmd", "state", "events", "frames", "snapshot"};

// Wipe any shared-memory left behind by a prior instance on this channel
// namespace, so a fresh start begins with a clean connection count. This runs
// BEFORE the routes attach.
//
// Why it is necessary (regression guard): a peer that is SIGKILLed — or any
// reuse of an instance id across app runs — leaves phantom receiver bits in the
// persistent, same-named AC_CONN segment. The dead-connection reaper
// deliberately SKIPS owner-less bits (so it never false-reaps a live peer), so
// those phantoms accumulate. A sender then reads a non-zero connection count
// with no real reader and its commands are never consumed — a silent failure
// that looks exactly like a deep transport bug. clear_storage() resets the
// connection-info, liveness, waiter, and notify segments to a clean slate.
//
// Why it is safe: this runs at startup, before this app opens its own routes, so
// there is no live LOCAL peer to evict; the only remote peer — a transient bridge
// — is designed to reconnect, and a restarted app has fresh channels regardless.
void clearStaleAgentChannels(const std::string& instanceId)
{
	for (const char* base : kAgentChannelBases)
	{
		thoth::route::clear_storage(agentChannel(instanceId, base).c_str());
	}
}

// Forwards Sourcetrail LogManager messages to a level-tagged sink (0/1/2). The
// sink applies the agent filter and publishes a LogEvent. See forwardLog().
class AgentLogger: public Logger
{
public:
	using Sink = std::function<void(int level, const LogMessage&)>;
	explicit AgentLogger(Sink sink): Logger("AgentLogger"), m_sink(std::move(sink)) {}

private:
	void logInfo(const LogMessage& message) override
	{
		m_sink(0, message);
	}
	void logWarning(const LogMessage& message) override
	{
		m_sink(1, message);
	}
	void logError(const LogMessage& message) override
	{
		m_sink(2, message);
	}
	Sink m_sink;
};

// A schema ElementRef decoded into QtUiControl's plain selector form.
struct DecodedRef
{
	std::string objectName;
	std::vector<utility::qt::RefStep> path;
};

DecodedRef decodeElementRef(const fb::ElementRef* ref)
{
	DecodedRef out;
	if (ref == nullptr)
	{
		return out;
	}
	if (ref->object_name() != nullptr)
	{
		out.objectName = ref->object_name()->str();
	}
	if (const auto* steps = ref->path())
	{
		for (const auto* step: *steps)
		{
			out.path.push_back(
				{step->role() != nullptr ? step->role()->str() : "",
				 step->name() != nullptr ? step->name()->str() : "",
				 step->index()});
		}
	}
	return out;
}

// A schema InputEvent gesture decoded into QtUiControl's form.
std::vector<utility::qt::InputEvent> decodeInputEvents(const flatbuffers::Vector<flatbuffers::Offset<fb::InputEvent>>* events)
{
	std::vector<utility::qt::InputEvent> out;
	if (events == nullptr)
	{
		return out;
	}
	for (const auto* e: *events)
	{
		utility::qt::InputEvent event;
		event.kind = static_cast<utility::qt::InputKind>(e->kind());
		event.x = e->x();
		event.y = e->y();
		event.centered = e->centered();
		event.button = static_cast<std::int32_t>(e->button());
		event.buttons = e->buttons();
		event.modifiers = e->modifiers();
		event.wheelDelta = e->wheel_delta();
		event.key = e->key();
		event.text = e->text() != nullptr ? e->text()->str() : "";
		out.push_back(std::move(event));
	}
	return out;
}

utility::qt::SnapshotFormat toSnapshotFormat(fb::SnapshotFormat format)
{
	return format == fb::SnapshotFormat_ObjectTree ? utility::qt::SnapshotFormat::ObjectTree
												   : utility::qt::SnapshotFormat::Accessibility;
}

::RefreshMode toRefreshMode(fb::RefreshMode mode)
{
	switch (mode)
	{
	case fb::RefreshMode_UpdatedFiles:
		return RefreshMode::UPDATED_FILES;
	case fb::RefreshMode_AllFiles:
		return RefreshMode::ALL_FILES;
	default:
		return RefreshMode::NONE;
	}
}

Id toId(std::uint64_t id)
{
	return Id(static_cast<Id::type>(id));
}

std::uint64_t nowMs()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
											std::chrono::system_clock::now().time_since_epoch())
										.count());
}

flatbuffers::Offset<fb::NodeRef> makeNodeRef(
	flatbuffers::FlatBufferBuilder& builder,
	Id id,
	const NameHierarchy& name,
	NodeKind kind,
	const std::string& displayName = "")
{
	const std::string display = displayName.empty() ? name.getQualifiedName() : displayName;
	return fb::CreateNodeRef(
		builder,
		static_cast<std::uint64_t>(id),
		builder.CreateString(NameHierarchy::serialize(name)),
		static_cast<std::int32_t>(kind),
		display.empty() ? 0 : builder.CreateString(display));
}

flatbuffers::Offset<fb::SearchMatch> makeSearchMatch(
	flatbuffers::FlatBufferBuilder& builder, const SearchMatch& match)
{
	std::vector<std::uint64_t> nodeIds;
	nodeIds.reserve(match.tokenIds.size());
	for (const Id id: match.tokenIds)
	{
		nodeIds.push_back(static_cast<std::uint64_t>(id));
	}
	const auto textOffset = builder.CreateString(match.name);
	// An empty node_ids is left *absent* rather than written as a zero-length
	// vector, and this is load-bearing rather than tidiness.
	//
	// node_ids is [uint64], so its elements must start 8-aligned. With no
	// elements there is nothing to align, and the builder does not force the
	// element region to an 8-boundary — a legal buffer. But the Rust verifier
	// (flatbuffers 25.12.19, verify_vector_range) checks is_aligned::<T>(start)
	// *before* it looks at the length, so it rejects the whole message over the
	// alignment of elements that do not exist.
	//
	// Search matches that are commands ("overview", "legend", "boundary") are
	// exactly the ones with no node ids, so a search for a command name could
	// make every following GetUiState unreadable to the bridge. Absent and empty
	// mean the same thing to every reader here, and absent is the one that
	// survives the trip.
	const auto nodeIdsOffset = nodeIds.empty()
		? flatbuffers::Offset<flatbuffers::Vector<std::uint64_t>>()
		: builder.CreateVector(nodeIds);
	return fb::CreateSearchMatch(
		builder, textOffset, static_cast<std::int32_t>(match.nodeType.getKind()), nodeIdsOffset, match.score);
}

flatbuffers::Offset<fb::BookmarkInfo> makeBookmark(
	flatbuffers::FlatBufferBuilder& builder, const Bookmark& bookmark)
{
	return fb::CreateBookmarkInfo(
		builder,
		static_cast<std::uint64_t>(bookmark.getId()),
		builder.CreateString(bookmark.getName()),
		builder.CreateString(bookmark.getComment()),
		builder.CreateString(bookmark.getCategory().getName()));
}

flatbuffers::Offset<fb::TabInfo> makeTabInfo(
	flatbuffers::FlatBufferBuilder& builder, const MessageTabsChanged::TabInfo& tab)
{
	return fb::CreateTabInfo(
		builder,
		static_cast<std::uint64_t>(static_cast<unsigned>(tab.tabId)),
		builder.CreateString(tab.title),
		tab.active);
}

flatbuffers::Offset<fb::MachineState> makeMachineState(
	flatbuffers::FlatBufferBuilder& builder, const MachineSnapshot& snapshot)
{
	std::vector<flatbuffers::Offset<flatbuffers::String>> events;
	for (const std::string& event: snapshot.events)
	{
		events.push_back(builder.CreateString(event));
	}
	const auto machine = builder.CreateString(snapshot.machine);
	const auto state = builder.CreateString(snapshot.state);
	const auto eventsOffset = builder.CreateVector(events);
	const auto context = builder.CreateString(snapshot.context);
	return fb::CreateMachineState(builder, machine, state, eventsOffset, context, snapshot.revision);
}

std::vector<flatbuffers::Offset<fb::MachineState>> makeMachineStates(flatbuffers::FlatBufferBuilder& builder)
{
	std::vector<flatbuffers::Offset<fb::MachineState>> machines;
	for (const AnyStateMachine& machine: StateMachineRegistry::instance().all())
	{
		machines.push_back(makeMachineState(builder, machine.snapshot()));
	}
	return machines;
}

std::string machineNames()
{
	std::string names;
	for (const AnyStateMachine& machine: StateMachineRegistry::instance().all())
	{
		names += (names.empty() ? "" : ", ") + machine.name();
	}
	return names;
}

std::string edgeKindNames()
{
	std::string names;
	for (Edge::TypeMask mask = 1; mask <= Edge::EDGE_TYPE_MAX_VALUE; mask *= 2)
	{
		const auto type = static_cast<Edge::EdgeType>(mask);
		if (type != Edge::EDGE_BUNDLED_EDGES)
		{
			names += (names.empty() ? "" : ", ") + Edge::getReadableTypeString(type);
		}
	}
	return names;
}

std::string nodeKindNames()
{
	std::string names;
	for (NodeKindMask mask = NODE_KIND_FIRST_VALUE; mask <= NODE_KIND_LAST_VALUE; mask <<= 1)
	{
		names += (names.empty() ? "" : ", ") + getReadableNodeKindString(static_cast<NodeKind>(mask));
	}
	return names;
}

fb::GraphViewNodeKind toGraphViewNodeKind(DisplayedGraphNode::Kind kind)
{
	switch (kind)
	{
	case DisplayedGraphNode::Kind::Bundle:
		return fb::GraphViewNodeKind_Bundle;
	case DisplayedGraphNode::Kind::Group:
		return fb::GraphViewNodeKind_Group;
	default:
		return fb::GraphViewNodeKind_Node;
	}
}

// What the graph view of `tab` draws (UiState.view_graph, protocol v7).
std::vector<flatbuffers::Offset<fb::GraphViewNode>> makeGraphView(flatbuffers::FlatBufferBuilder& builder, TabId tab)
{
	std::vector<flatbuffers::Offset<fb::GraphViewNode>> nodes;
	for (const DisplayedGraphNode& node: DisplayedGraph::instance().get(tab))
	{
		const auto name = builder.CreateString(node.name);
		const auto group = builder.CreateString(node.group);
		nodes.push_back(fb::CreateGraphViewNode(
			builder,
			toGraphViewNodeKind(node.kind),
			static_cast<std::uint64_t>(node.id),
			name,
			static_cast<std::int32_t>(node.nodeKind),
			node.parent,
			node.active,
			node.expandable,
			node.expanded,
			static_cast<std::uint32_t>(node.hiddenMembers),
			static_cast<std::uint32_t>(node.bundled),
			node.splittable,
			group));
	}
	return nodes;
}

// Receives raw CommandEnvelope frames from a thoth-ipc route and hands each to a
// sink. Two implementations, selected by how thoth-ipc was built:
//
//   * THOTH_IPC_STDEXEC (POSIX): thoth::async_recv — no dedicated thread. Receives are
//     driven by thoth-ipc's process-global reactor; an exec::async_scope owns the
//     repeating receive and joins it on stop(). This is the RFC end-state
//     (submodules/thoth-ipc/context/stdexec-async-recv-rfc.md).
//   * otherwise (e.g. Windows, where thoth-ipc's Layer-1 readiness fd is not yet
//     implemented): a dedicated jthread blocking on recv().
//
// Both expose the same start(sink, scheduler) / stop() contract, so
// AgentControlController never learns which mechanism is underneath.
class AgentCommandReader
{
public:
	using Sink = std::function<void(std::vector<std::uint8_t>)>;

	explicit AgentCommandReader(const char* channelName): m_route(channelName, thoth::receiver) {}

	// `on` is the scheduler each command frame is delivered on (io()).
	void start(Sink sink, [[maybe_unused]] execution::AnyScheduler on)
	{
		m_sink = std::move(sink);
#if defined(THOTH_IPC_STDEXEC)
		// Repeat one async receive until the effect yields true; each completes on
		// `on`. async_recv delivers thoth::recv_result (a message, or a recv_errc —
		// its error channel is pruned per ADR-0001): a message yields false (keep
		// receiving); an error is logged and yields true (stop, rather than spin).
		// Cancellation comes from the scope's stop token (async_recv set_stopped).
		m_scope.spawn(exec::repeat_effect_until(
			thoth::async_recv(m_route, std::move(on)) |
			stdexec::then([this](thoth::recv_result result) {
				if (result)
				{
					deliver(*result);
					return false;
				}
				LOG_ERROR(thoth::recv_message(result.error()));
				return true;
			})));
#else
		if (!m_thread.joinable())
		{
			m_thread = std::jthread([this]() { loop(); });
		}
#endif
	}

	void stop()
	{
#if defined(THOTH_IPC_STDEXEC)
		m_scope.request_stop();
		stdexec::sync_wait(m_scope.on_empty());	 // join the spawned receive loop
#else
		m_stopSource.request_stop();
		if (m_thread.joinable())	// reader only blocks on recv, so this joins within the timeout
		{
			m_thread.join();
		}
#endif
	}

private:
	void deliver(const thoth::buff_t& buffer)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(buffer.data());
		m_sink(std::vector<std::uint8_t>(bytes, bytes + buffer.size()));
	}

#if !defined(THOTH_IPC_STDEXEC)
	void loop()
	{
		while (!m_stopSource.stop_requested())
		{
			thoth::buff_t buffer = m_route.recv(200 /*ms; re-checks stop*/);
			if (!buffer.empty())
			{
				deliver(buffer);
			}
		}
	}
#endif

	thoth::route m_route;
	Sink m_sink;
#if defined(THOTH_IPC_STDEXEC)
	exec::async_scope m_scope;
#else
	stdexec::inplace_stop_source m_stopSource;
	std::jthread m_thread;
#endif
};
}	 // namespace

// Threads: agent commands are handled on the app TaskScheduler (MessageAgentCommand
// is scheduled there), but the messages that change what is on screen —
// activations, the code view — are scheduled on the current tab's scheduler, a
// thread of its own, and parallel ones fan out further. So the state cache is
// guarded by m_stateMutex. Commands that only need the GUI thread skip the message
// bus entirely (see handleGuiCommand).
struct AgentControlController::Impl
	: public MessageListener<MessageAgentCommand>
	, public MessageListener<MessageActivateTokens>
	, public MessageListener<MessageActivateOverview>
	, public MessageListener<MessageActivateFile>
	, public MessageListener<MessageScrollToLine>
	, public MessageListener<MessageSearch>
	, public MessageListener<MessageIndexingStarted>
	, public MessageListener<MessageIndexingFinished>
	, public MessageListener<MessageErrorCountUpdate>
	, public MessageListener<MessageTabsChanged>
{
	Impl(StorageAccess* storageAccess, execution::ISchedulers* schedulers, std::string instanceId)
		: m_storageAccess(storageAccess)
		, m_schedulers(schedulers)
		, m_instanceId(std::move(instanceId))
		, m_reader(agentChannel(m_instanceId, "cmd").c_str())
		, m_stateChannel(agentChannel(m_instanceId, "state").c_str(), thoth::sender)
		, m_eventsChannel(agentChannel(m_instanceId, "events").c_str(), thoth::sender)
		, m_snapshotChannel(agentChannel(m_instanceId, "snapshot").c_str(), thoth::sender)
		, m_framesChannel(agentChannel(m_instanceId, "frames").c_str(), thoth::sender)
	{
	}

	void start()
	{
		// Each command frame is forwarded onto the message thread via
		// MessageAgentCommand (dispatch is thread-safe), so it is handled where the
		// non-thread-safe StorageCache is safe to query. Delivery from the reader
		// runs on io(); the dispatch() hop then lands it on the message thread.
		m_reader.start(
			[this](std::vector<std::uint8_t> bytes) {
				if (!handleGuiCommand(bytes))
				{
					MessageAgentCommand(std::move(bytes)).dispatch();
				}
			},
			m_schedulers->io());
		// Settle barrier: a request has settled once the work queued on its behalf
		// is done (CauseWork) and the views have drawn (the activation machine).
		CauseWork::onDone([this, alive = std::weak_ptr<void>(m_lifetime)](std::uint64_t) {
			if (alive.lock())
			{
				flushSettled();
			}
		});
		utility::qt::watchDialogs(true, [this, alive = std::weak_ptr<void>(m_lifetime)](const std::string& dialog) {
			if (alive.lock())
			{
				publishDialogChanged(dialog);
			}
		});
		m_machineObserver = StateMachineRegistry::instance().addObserver([this](const MachineTransition& transition) {
			publishTransition(transition);
			// The views have drawn what the activation gave them: what waited for that
			// may settle now.
			if (transition.machine == "activation" && transition.to == "idle")
			{
				flushSettled();
			}
			// AppState is derived from these.
			if (transition.machine == "app" || transition.machine == "project" || transition.machine == "indexing")
			{
				updateAppState(transition.cause);
			}
		});

		// Log forwarding: capture both LogManager (app) and Qt logging into one
		// LogEvent stream. Off until the agent opts in via SetLogFilter.
		m_agentLogger = std::make_shared<AgentLogger>([this](int level, const LogMessage& message) {
			forwardLog(level, fb::LogSource_App, "", message.message, message.getFileName(),
				message.functionName, static_cast<int>(message.line));
		});
		LogManager::getInstance()->addLogger(m_agentLogger);
		utility::qt::installQtLogSink([this](int level, const char* category, const std::string& message,
											 const char* file, const char* function, int line) {
			forwardLog(level, fb::LogSource_Qt, category != nullptr ? category : "", message,
				file != nullptr ? file : "", function != nullptr ? function : "", line);
		});
	}

	void stop()
	{
		m_stopping = true;
		StateMachineRegistry::instance().removeObserver(m_machineObserver);
		utility::qt::installQtLogSink(nullptr);
		utility::qt::setQtLogRules("");	// restore default Qt verbosity
		if (m_agentLogger)
		{
			LogManager::getInstance()->removeLogger(m_agentLogger);
			m_agentLogger.reset();
		}
		utility::qt::watchDialogs(false);
		CauseWork::onDone(nullptr);	// before we tear down
		m_reader.stop();
		m_uiScope.request_stop();
		stdexec::sync_wait(m_uiScope.on_empty());	 // join any in-flight snapshot capture
	}

	// Runs `work` on the GUI thread; stop() joins what is still running, and refuses
	// what comes after.
	template <std::invocable Work>
	void onGui(Work work)
	{
		if (!m_stopping)
		{
			m_uiScope.spawn(stdexec::schedule(m_schedulers->ui()) | stdexec::then(std::move(work)));
		}
	}

	// ---- MessageListener overrides (message-processing thread) ----------------

	void handleMessage(MessageAgentCommand* message) override
	{
		handleCommand(message->bytes.data(), message->bytes.size());
	}

	// Every activation ends here, whoever started it: an agent's ActivateNode, a
	// click in the graph or the search, and the back/forward history replaying an
	// earlier one. Listening for MessageActivateNodes alone missed all but the
	// first, so active_nodes went stale after a click or nav_history.
	void handleMessage(MessageActivateTokens* message) override
	{
		if (message->isEdge || message->isBundledEdges)
		{
			return;	// the active nodes stay what they were
		}
		std::vector<Id> ids = message->tokenIds;
		flatbuffers::FlatBufferBuilder builder;
		std::vector<flatbuffers::Offset<fb::NodeRef>> nodes;
		bool showsFile = false;
		for (const Id id: ids)
		{
			const NodeType type = m_storageAccess->getNodeTypeForNodeWithId(id);
			showsFile = showsFile || type.isFile();
			nodes.push_back(makeNodeRef(builder, id, m_storageAccess->getNameHierarchyForNodeId(id), type.getKind()));
		}
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			m_activeNodeIds = std::move(ids);
			// The code view now lists the activated nodes' references across files,
			// so no single file is "the" one shown; a file node is the exception
			// (MessageActivateFile sets that one).
			if (!showsFile)
			{
				m_currentFile.clear();
				m_scrollLine = 0;
			}
		}
		const auto event = fb::CreateNodesActivated(builder, builder.CreateVector(nodes));
		publishEvent(builder, fb::Event_NodesActivated, event.Union());
	}

	// The overview (the toolbar's home button, or Back to where a session began)
	// activates no node: nothing is active any more, and the code view shows no file.
	void handleMessage(MessageActivateOverview*) override
	{
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			m_activeNodeIds.clear();
			m_currentFile.clear();
			m_scrollLine = 0;
		}
		flatbuffers::FlatBufferBuilder builder;
		const auto event = fb::CreateNodesActivated(builder, builder.CreateVector(std::vector<flatbuffers::Offset<fb::NodeRef>>{}));
		publishEvent(builder, fb::Event_NodesActivated, event.Union());
	}

	void handleMessage(MessageActivateFile* message) override
	{
		const std::string file = message->filePath.str();
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			m_currentFile = file;
			m_scrollLine = 0;
		}
		flatbuffers::FlatBufferBuilder builder;
		const auto event = fb::CreateFileActivated(builder, builder.CreateString(file));
		publishEvent(builder, fb::Event_FileActivated, event.Union());
		updateAppState();
	}

	void handleMessage(MessageScrollToLine* message) override
	{
		const std::lock_guard<std::mutex> lock(m_stateMutex);
		m_currentFile = message->filePath.str();
		m_scrollLine = static_cast<std::uint32_t>(message->line);
	}

	void handleMessage(MessageSearch* message) override
	{
		const std::vector<SearchMatch> found = message->getMatches();
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			m_lastSearchMatches = found;
		}
		flatbuffers::FlatBufferBuilder builder;
		std::vector<flatbuffers::Offset<fb::SearchMatch>> matches;
		for (const SearchMatch& match: found)
		{
			matches.push_back(makeSearchMatch(builder, match));
		}
		const auto event = fb::CreateSearchCompleted(builder, builder.CreateVector(matches));
		publishEvent(builder, fb::Event_SearchCompleted, event.Union());
	}

	void handleMessage(MessageIndexingStarted*) override
	{
		m_indexing.store(true);
		flatbuffers::FlatBufferBuilder builder;
		publishEvent(builder, fb::Event_IndexingStarted, fb::CreateIndexingStarted(builder).Union());
		updateAppState();
	}

	void handleMessage(MessageIndexingFinished*) override
	{
		m_indexing.store(false);
		flatbuffers::FlatBufferBuilder builder;
		publishEvent(builder, fb::Event_IndexingFinished, fb::CreateIndexingFinished(builder, true).Union());
		updateAppState();
	}

	void handleMessage(MessageErrorCountUpdate* message) override
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto event = fb::CreateErrorCountChanged(
			builder,
			static_cast<std::uint32_t>(message->errorCount.total),
			static_cast<std::uint32_t>(message->errorCount.fatal));
		publishEvent(builder, fb::Event_ErrorCountChanged, event.Union());
	}

	void handleMessage(MessageTabsChanged* message) override
	{
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			m_tabsCache = message->tabs;
		}
		flatbuffers::FlatBufferBuilder builder;
		std::vector<flatbuffers::Offset<fb::TabInfo>> tabs;
		for (const auto& tab: message->tabs)
		{
			tabs.push_back(makeTabInfo(builder, tab));
		}
		const auto event = fb::CreateTabsChanged(builder, builder.CreateVector(tabs));
		publishEvent(builder, fb::Event_TabsChanged, event.Union());
	}

	// ---- commands that only need the GUI thread (reader thread) ---------------

	// GetInfo and the structural UI commands touch no storage and no message
	// state, so they skip the message bus: decoded here on the reader thread and
	// run on the GUI thread (ui()). That keeps them answering while the message
	// thread is blocked, which is exactly when an agent needs them: a confirm()
	// dialog parks the thread that asked until someone answers, and through the
	// bus the agent could neither see the dialog (GetFrame) nor press its button
	// (InvokeAction). Returns false for every other command, which then goes
	// through MessageAgentCommand to handleCommand as before.
	bool handleGuiCommand(const std::vector<std::uint8_t>& bytes)
	{
		flatbuffers::Verifier verifier(bytes.data(), bytes.size());
		if (!fb::VerifyCommandEnvelopeBuffer(verifier))
		{
			return false;	// handleCommand reports it
		}
		const auto* envelope = fb::GetCommandEnvelope(bytes.data());
		const std::uint64_t requestId = envelope->request_id();
		switch (envelope->command_type())
		{
		case fb::Command_GetInfo:
			publishAppInfo(requestId);
			return true;
		case fb::Command_GetSnapshot:
		{
			const auto* c = envelope->command_as_GetSnapshot();
			const utility::qt::SnapshotFormat format =
				toSnapshotFormat(c != nullptr ? c->format() : fb::SnapshotFormat_Accessibility);
			// Ack, then capture on the GUI thread and send the UiSnapshot on
			// st.agent.snapshot: the bridge reads it once it has the ack (see
			// publishFrame for why that order matters). Scoped so stop() joins it.
			emitResult(requestId, true);
			onGui([this, requestId, format]() {
				const std::vector<std::uint8_t> buffer = utility::qt::captureUiSnapshot(format, requestId);
				if (!buffer.empty())
				{
					m_snapshotChannel.send(buffer.data(), buffer.size());
				}
			});
			return true;
		}
		case fb::Command_InvokeAction:
		{
			const auto* c = envelope->command_as_InvokeAction();
			if (c == nullptr || c->target() == nullptr)
			{
				emitResult(requestId, false, "invoke_action: missing target");
				return true;
			}
			DecodedRef ref = decodeElementRef(c->target());
			std::string action = c->action() ? c->action()->str() : "";
			std::string text = c->text() ? c->text()->str() : "";
			const std::uint64_t expectHash = c->expect_hash();
			// QAccessible touches widgets, so resolve + invoke on the GUI thread, then
			// ack with the result. The ack is the reply (no Settled). It goes behind
			// what the action posted to the GUI thread (a button's click): by then
			// the click has dispatched what it does, and the activation machine says
			// whether that is still going on.
			onGui([this, requestId, objectName = std::move(ref.objectName),
					path = std::move(ref.path), action = std::move(action),
					text = std::move(text), expectHash]() {
				const CauseScope cause(requestId);
				const utility::qt::ControlResult r =
					utility::qt::invokeAction(objectName, path, action, text, expectHash);
				onGui([this, requestId, r]() { emitResult(requestId, r.ok, r.message); });
			});
			return true;
		}
		case fb::Command_CaptureElement:
		{
			const auto* c = envelope->command_as_CaptureElement();
			if (c == nullptr || c->target() == nullptr)
			{
				emitResult(requestId, false, "capture_element: missing target");
				return true;
			}
			DecodedRef ref = decodeElementRef(c->target());
			const std::uint64_t expectHash = c->expect_hash();
			onGui([this, requestId, objectName = std::move(ref.objectName),
					path = std::move(ref.path), expectHash]() {
				const utility::qt::CaptureResult r = utility::qt::captureElement(objectName, path, expectHash);
				// Ack first, frame second: see publishFrame.
				emitResult(requestId, r.ok, r.message);
				if (r.ok)
				{
					publishFrame(requestId, r);
				}
			});
			return true;
		}
		case fb::Command_GetFrame:
		{
			const auto* c = envelope->command_as_GetFrame();
			if (c == nullptr || c->format() != fb::FrameFormat_Png)
			{
				emitResult(requestId, false, "get_frame: unsupported format (only Png)");
				return true;
			}
			const std::uint32_t maxWidth = c->max_width();
			const fb::Rect region = c->region() != nullptr ? *c->region() : fb::Rect(0, 0, 0, 0);
			// The ack's message names the composited windows, so a dialog over the
			// main window shows in the reply too.
			onGui([this, requestId, maxWidth, region]() {
				const utility::qt::CaptureResult r =
					utility::qt::captureScreen(maxWidth, region.x(), region.y(), region.w(), region.h());
				// Ack first, frame second: see publishFrame.
				emitResult(requestId, r.ok, r.message);
				if (r.ok)
				{
					publishFrame(requestId, r);
				}
			});
			return true;
		}
		// The state machines answer here on the reader thread: they are thread-safe,
		// and a question they hold parks the message thread, which is exactly when
		// an agent needs to answer it.
		case fb::Command_GetMachines:
		{
			const auto* c = envelope->command_as_GetMachines();
			const std::string name = (c != nullptr && c->machine() != nullptr) ? c->machine()->str() : "";
			flatbuffers::FlatBufferBuilder builder;
			std::vector<flatbuffers::Offset<fb::MachineState>> machines;
			if (name.empty())
			{
				machines = makeMachineStates(builder);
			}
			else if (const auto machine = StateMachineRegistry::instance().find(name))
			{
				machines.push_back(makeMachineState(builder, machine->snapshot()));
			}
			// MachineStates follows the ack even on a refusal (then empty), so the
			// bridge always has one reply to wait for.
			emitResult(
				requestId,
				!machines.empty() || name.empty(),
				machines.empty() && !name.empty() ? "no state machine '" + name + "' (" + machineNames() + ")" : "");
			publishMachineStates(builder, requestId, machines);
			return true;
		}
		case fb::Command_SendMachineEvent:
		{
			const auto* c = envelope->command_as_SendMachineEvent();
			const std::string name = (c != nullptr && c->machine() != nullptr) ? c->machine()->str() : "";
			const std::optional<AnyStateMachine> machine = StateMachineRegistry::instance().find(name);
			if (!machine)
			{
				emitResult(requestId, false, "no state machine '" + name + "' (" + machineNames() + ")");
				flatbuffers::FlatBufferBuilder builder;
				publishMachineStates(builder, requestId, {});
				return true;
			}
			const MachineFireResult result = machine->fire(
				c->event() != nullptr ? c->event()->str() : "",
				c->args() != nullptr ? c->args()->str() : "",
				EventSource::Agent);
			emitResult(requestId, result.ok, result.message);
			flatbuffers::FlatBufferBuilder builder;
			publishMachineStates(builder, requestId, {makeMachineState(builder, machine->snapshot())});
			return true;
		}
		// A load goes through the project machine, which is thread-safe and loading
		// before the ack goes out; the load itself runs later on the message thread.
		// So the ack does not wait for whatever that thread is busy with.
		case fb::Command_LoadProject:
		{
			const auto* c = envelope->command_as_LoadProject();
			if (c == nullptr || c->project_file_path() == nullptr)
			{
				emitResult(requestId, false, "rejected: load_project: missing project path");
				return true;
			}
			const CauseScope cause(requestId);
			const MachineFireResult result = ProjectStateMachine::instance()->load(
				c->project_file_path()->str(), toRefreshMode(c->refresh_mode()), EventSource::Agent);
			if (!result.ok)
			{
				emitResult(requestId, false, "rejected: " + result.message);
				return true;
			}
			emitResult(requestId, true);
			expectSettle(requestId);
			return true;
		}
		case fb::Command_SendInput:
		{
			const auto* c = envelope->command_as_SendInput();
			if (c == nullptr || c->events() == nullptr || c->events()->size() == 0)
			{
				emitResult(requestId, false, "send_input: no events");
				return true;
			}
			std::vector<utility::qt::InputEvent> events = decodeInputEvents(c->events());
			const bool atPoint = c->use_target_point();
			const std::int32_t x = c->target_x();
			const std::int32_t y = c->target_y();
			DecodedRef ref = decodeElementRef(c->target());
			// Resolve and queue on the GUI thread, then ack. The events are posted, so
			// the ack does not wait for whatever they open (a menu, a dialog); once the
			// last one is delivered the request waits for Settled like a navigation
			// command, so the caller can read the state the gesture led to.
			onGui([this, requestId, atPoint, x, y, ref = std::move(ref), events = std::move(events)]() {
				auto delivered = [this, alive = std::weak_ptr<void>(m_lifetime), requestId]() {
					if (alive.lock())
					{
						expectSettle(requestId);
					}
				};
				const CauseScope cause(requestId);
				const utility::qt::ControlResult r = atPoint
					? utility::qt::sendInputAt(x, y, events, delivered)
					: utility::qt::sendInput(ref.objectName, ref.path, events, delivered);
				emitResult(requestId, r.ok, r.message);
			});
			return true;
		}
		case fb::Command_QueryUi:
		{
			const auto* c = envelope->command_as_QueryUi();
			std::string jsonPath = (c != nullptr && c->jsonpath() != nullptr) ? c->jsonpath()->str() : "";
			// Evaluate the JSONPath over the live tree on the GUI thread; ack, then
			// send a UiSnapshot of the matched nodes on st.agent.snapshot.
			onGui([this, requestId, jsonPath = std::move(jsonPath)]() {
				const utility::qt::QueryResult r = utility::qt::queryUi(jsonPath, requestId);
				emitResult(requestId, r.ok, r.message);
				if (r.ok && !r.snapshot.empty())
				{
					m_snapshotChannel.send(r.snapshot.data(), r.snapshot.size());
				}
			});
			return true;
		}
		default:
			return false;
		}
	}

	// ---- command handling (message-processing thread) -------------------------

	void handleCommand(const std::uint8_t* data, std::size_t size)
	{
		flatbuffers::Verifier verifier(data, size);
		if (!fb::VerifyCommandEnvelopeBuffer(verifier))
		{
			LOG_ERROR("agent-control: malformed CommandEnvelope, dropped");
			return;
		}
		const auto* envelope = fb::GetCommandEnvelope(data);
		const std::uint64_t requestId = envelope->request_id();
		const fb::Command type = envelope->command_type();
		// What the command dispatches, and all that follows from it, is on its behalf.
		const CauseScope cause(requestId);

		// State gate: everything but get_ui_state / set_log_filter needs a project
		// (the GUI-only commands and load_project never get here).
		const bool needsProject = !(type == fb::Command_GetUiState || type == fb::Command_SetLogFilter);
		if (needsProject && computeAppState() == fb::AppState_NoProject)
		{
			emitResult(requestId, false, "rejected: no project loaded");
			return;
		}
		// Navigating while a load is still opening its first tab races the load's
		// own activation of the overview, which can land after it and become a
		// history step. Refuse instead, so the agent waits for Ready.
		if (needsProject && computeAppState() == fb::AppState_Loading)
		{
			emitResult(requestId, false, "rejected: the project is still loading; wait for app_state Ready");
			return;
		}

		switch (type)
		{
		case fb::Command_Search:
			if (const auto* c = envelope->command_as_Search(); c && c->query())
			{
				const std::vector<SearchMatch> matches =
					m_storageAccess->getAutocompletionMatches(c->query()->str(), NodeTypeSet::all(), true);
				MessageSearch(matches, NodeTypeSet::all()).dispatch();
			}
			break;
		case fb::Command_ActivateNode:
			if (const auto* c = envelope->command_as_ActivateNode())
			{
				Id nodeId = toId(c->node_id());
				if (nodeId == 0 && c->serialized_name())
				{
					nodeId = m_storageAccess->getNodeIdForNameHierarchy(
						NameHierarchy::deserialize(c->serialized_name()->str()));
				}
				if (!(nodeId == 0))
				{
					MessageActivateNodes(nodeId).dispatch();
				}
			}
			break;
		case fb::Command_ActivateFile:
			if (const auto* c = envelope->command_as_ActivateFile(); c && c->file_path())
			{
				MessageActivateFile(FilePath(c->file_path()->str())).dispatch();
			}
			break;
		case fb::Command_ActivateTab:
			if (const auto* c = envelope->command_as_ActivateTab())
			{
				MessageTabActivate(static_cast<TabId>(static_cast<unsigned>(c->tab_id()))).dispatch();
			}
			break;
		case fb::Command_ScrollToLine:
			if (const auto* c = envelope->command_as_ScrollToLine(); c && c->file_path())
			{
				MessageScrollToLine(FilePath(c->file_path()->str()), c->line()).dispatch();
			}
			break;
		case fb::Command_HistoryBack:
			MessageHistoryUndo().dispatch();
			break;
		case fb::Command_HistoryForward:
			MessageHistoryRedo().dispatch();
			break;
		case fb::Command_GetUiState:
			publishUiState(requestId);
			break;
		case fb::Command_SetLogFilter:
		{
			const auto* c = envelope->command_as_SetLogFilter();
			if (c != nullptr)
			{
				const std::string pattern = c->event_pattern() != nullptr ? c->event_pattern()->str() : "";
				// Validate the regex *before* touching any config, so a bad pattern
				// leaves the prior filter fully intact (no partial apply of qt_rules
				// or min_level).
				std::regex compiled;
				if (!pattern.empty())
				{
					try
					{
						compiled = std::regex(pattern);
					}
					catch (const std::regex_error& e)
					{
						emitResult(requestId, false, std::string("bad event_pattern: ") + e.what());
						return;
					}
				}
				if (c->qt_rules() != nullptr)
				{
					utility::qt::setQtLogRules(c->qt_rules()->str());
				}
				// Commit all filter config under one lock; flip m_logEnabled last so a
				// concurrent forwardLog never sees enabled with a half-updated config.
				const std::lock_guard<std::mutex> lock(m_logConfigMutex);
				m_logPattern = std::move(compiled);
				m_logHasPattern = !pattern.empty();
				m_logMinLevel.store(static_cast<int>(c->min_level()), std::memory_order_relaxed);
				m_logEnabled.store(true, std::memory_order_relaxed);
			}
			break;
		}
		case fb::Command_ExpandNode:
		case fb::Command_HideNode:
		case fb::Command_SplitBundle:
		case fb::Command_ActivateTrail:
			if (const std::optional<std::string> refusal = sendGraphCommand(*envelope))
			{
				emitResult(requestId, false, "rejected: " + *refusal);
				return;
			}
			break;
		default:
			emitResult(requestId, false, "unknown command");
			return;
		}
		emitResult(requestId, true);
		// handleCommand runs *inside* the drain (as the MessageAgentCommand
		// listener), so the effects are enqueued but not yet processed.
		expectSettle(requestId);
	}

	// The graph's controls (protocol v7), on what the current tab's graph view shows,
	// as its own buttons would. Nothing when the command went out; else why not.
	std::optional<std::string> sendGraphCommand(const fb::CommandEnvelope& envelope)
	{
		const std::vector<DisplayedGraphNode> shown = DisplayedGraph::instance().get(TabIds::currentTab());
		const auto displayed = [&](std::uint64_t id) -> const DisplayedGraphNode* {
			const auto it = std::ranges::find_if(
				shown, [id](const DisplayedGraphNode& node) { return node.id == toId(id); });
			return it != shown.end() ? &*it : nullptr;
		};

		switch (envelope.command_type())
		{
		case fb::Command_ExpandNode:
		{
			const auto* c = envelope.command_as_ExpandNode();
			const DisplayedGraphNode* node = displayed(c->node_id());
			if (node == nullptr || node->kind != DisplayedGraphNode::Kind::Node)
			{
				return std::format("node {} is not in the graph (see ui_state.view_graph)", c->node_id());
			}
			if (!node->expandable)
			{
				return std::format("'{}' has no members to expand or collapse", node->name);
			}
			MessageGraphNodeExpand(node->id, c->expand()).dispatch();
			return std::nullopt;
		}
		case fb::Command_HideNode:
		{
			// A node of the graph, or an edge (by its id, which view_graph does not list).
			MessageGraphNodeHide(toId(envelope.command_as_HideNode()->node_id())).dispatch();
			return std::nullopt;
		}
		case fb::Command_SplitBundle:
		{
			const auto* c = envelope.command_as_SplitBundle();
			const DisplayedGraphNode* bundle = displayed(c->bundle_id());
			if (bundle == nullptr || bundle->kind == DisplayedGraphNode::Kind::Node)
			{
				return std::format("no bundle or group {} in the graph (see ui_state.view_graph)", c->bundle_id());
			}
			if (!bundle->splittable)
			{
				return bundle->group == "file" || bundle->group == "namespace"
					? std::format("the {} group '{}' is not split: activate its node instead", bundle->group, bundle->name)
					: std::format("'{}' cannot be split", bundle->name);
			}
			MessageGraphNodeBundleSplit(bundle->id, bundle->splitRemovesOthers, bundle->splitToList).dispatch();
			return std::nullopt;
		}
		case fb::Command_ActivateTrail:
			return sendTrail(*envelope.command_as_ActivateTrail());
		default:
			return "not a graph command";
		}
	}

	// A trail from or to a node, with the edges the graph's trail buttons would
	// follow unless named.
	std::optional<std::string> sendTrail(const fb::ActivateTrail& c)
	{
		const Id nodeId = toId(c.node_id());
		if (nodeId == 0 || m_storageAccess->getNameHierarchyForNodeId(nodeId).getQualifiedName().empty())
		{
			return std::format("no node {}", c.node_id());
		}
		const NodeType type = m_storageAccess->getNodeTypeForNodeWithId(nodeId);

		Edge::TypeMask edgeTypes = 0;
		bool horizontalByDefault = true;
		if (c.edge_kinds() != nullptr && c.edge_kinds()->size() > 0)
		{
			for (const flatbuffers::String* name: *c.edge_kinds())
			{
				const Edge::EdgeType edgeType = Edge::getTypeForReadableTypeString(name->str());
				if (edgeType == Edge::EDGE_UNDEFINED)
				{
					return std::format("unknown edge kind '{}' ({})", name->str(), edgeKindNames());
				}
				edgeTypes |= edgeType;
			}
		}
		else if (const std::optional<TrailDefaults> trail = trailDefaultsFor(type))
		{
			edgeTypes = trail->edgeTypes;
			horizontalByDefault = trail->horizontal;
		}
		else
		{
			return std::format(
				"no default trail for a {}: name the edge_kinds ({})",
				getReadableNodeKindString(type.getKind()),
				edgeKindNames());
		}

		NodeKindMask nodeTypes = 0;
		if (c.node_kinds() != nullptr)
		{
			for (const flatbuffers::String* name: *c.node_kinds())
			{
				const NodeKind kind = getNodeKindForReadableNodeKindString(name->str());
				// An unknown name reads as "symbol".
				if (kind == NODE_SYMBOL && name->str() != getReadableNodeKindString(NODE_SYMBOL))
				{
					return std::format("unknown node kind '{}' ({})", name->str(), nodeKindNames());
				}
				nodeTypes |= kind;
			}
		}

		const bool horizontal =
			c.layout() == fb::TrailLayout_Auto ? horizontalByDefault : c.layout() == fb::TrailLayout_Horizontal;
		Id originId = nodeId;
		Id targetId = 0;
		if (c.target_id() != 0)
		{
			targetId = toId(c.target_id());
		}
		else if (c.direction() == fb::TrailDirection_Incoming)
		{
			originId = 0;
			targetId = nodeId;
		}

		// With a target or node kinds it is the custom trail dialog's.
		MessageActivateTrail message = c.target_id() != 0 || nodeTypes != 0
			? MessageActivateTrail(
				  originId, targetId, nodeTypes, edgeTypes, c.include_non_indexed(), c.depth(), horizontal)
			: MessageActivateTrail(originId, targetId, edgeTypes, c.depth(), horizontal);
		message.searchMatches = m_storageAccess->getSearchMatchesForTokenIds({nodeId});
		message.dispatch();
		return std::nullopt;
	}

	// Emit `Settled { requestId }` once the request's work is done: what it queued
	// on its behalf (messages, tasks, and what those queued in turn: CauseWork) has
	// ended, and the views have drawn what it changed (the activation machine).
	void expectSettle(std::uint64_t requestId)
	{
		{
			const std::lock_guard<std::mutex> lock(m_settleMutex);
			m_pendingSettle.push_back(requestId);
		}
		flushSettled();
	}

	// Whether request `requestId`'s work is done and the views have drawn.
	static bool isSettled(std::uint64_t requestId)
	{
		return CauseWork::pending(requestId) == 0 && ActivationStateMachine::instance()->isIdle();
	}

	// Publish a `Settled` for every pending request whose work is done: when one's
	// work ends, when the activation machine goes idle, and as a request comes in.
	// The graph and code views report when they have drawn (their animations
	// included), so a frame taken after Settled shows it. The other views' last hop
	// is a post to the GUI thread no one counts: Settled goes out from the GUI
	// thread, behind everything posted there before, and a request whose GUI work
	// queued more meanwhile waits for that too.
	void flushSettled()
	{
		std::vector<std::uint64_t> settled;
		{
			const std::lock_guard<std::mutex> lock(m_settleMutex);
			std::erase_if(m_pendingSettle, [&settled](std::uint64_t requestId) {
				if (!isSettled(requestId))
				{
					return false;
				}
				settled.push_back(requestId);
				return true;
			});
		}
		if (settled.empty())
		{
			return;
		}
		onGui([this, settled = std::move(settled)]() {
			std::vector<std::uint64_t> notYet;
			for (const std::uint64_t requestId: settled)
			{
				if (!isSettled(requestId))
				{
					notYet.push_back(requestId);
					continue;
				}
				flatbuffers::FlatBufferBuilder builder;
				publishEvent(builder, fb::Event_Settled, fb::CreateSettled(builder, requestId).Union(), requestId);
			}
			if (!notYet.empty())
			{
				{
					const std::lock_guard<std::mutex> lock(m_settleMutex);
					m_pendingSettle.insert(m_pendingSettle.end(), notYet.begin(), notYet.end());
				}
				flushSettled();	// their work may have ended before they were back in the list
			}
		});
	}

	// Handshake reply: publish an `AppInfo` event carrying this build's protocol
	// version + identity, so the bridge can detect skew (see the Protocol handshake
	// section in DESIGN_AGENT_UI_CONTROL.md). protocol_version is compiled in from
	// this checkout's schema (fb::ProtocolVersion_Current).
	void publishAppInfo(std::uint64_t requestId)
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto appVersion = builder.CreateString(Version::getApplicationVersion().toDisplayString());
		const auto buildId = builder.CreateString(std::string(AGENT_APP_BUILD_ID));
		const auto instanceId = builder.CreateString(m_instanceId);
		fb::AppInfoBuilder ib(builder);
		ib.add_request_id(requestId);
		ib.add_protocol_version(static_cast<std::uint32_t>(fb::ProtocolVersion_Current));
		ib.add_app_version(appVersion);
		ib.add_build_id(buildId);
		ib.add_instance_id(instanceId);
		publishEvent(builder, fb::Event_AppInfo, ib.Finish().Union());
	}

	// The deterministic "what's on screen", assembled from StorageAccess and a copy
	// of the state cache taken under m_stateMutex.
	void publishUiState(std::uint64_t requestId)
	{
		std::vector<Id> activeNodeIds;
		std::string currentFile;
		std::uint32_t scrollLine = 0;
		std::vector<SearchMatch> lastSearchMatches;
		std::vector<MessageTabsChanged::TabInfo> tabsCache;
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			activeNodeIds = m_activeNodeIds;
			currentFile = m_currentFile;
			scrollLine = m_scrollLine;
			lastSearchMatches = m_lastSearchMatches;
			tabsCache = m_tabsCache;
		}

		flatbuffers::FlatBufferBuilder builder;

		std::string projectPath;
		if (const auto app = Application::getInstance())
		{
			projectPath = app->getCurrentProjectPath().str();
		}
		const auto projectOffset = builder.CreateString(projectPath);

		std::vector<flatbuffers::Offset<fb::NodeRef>> activeNodes;
		for (const Id id: activeNodeIds)
		{
			activeNodes.push_back(makeNodeRef(
				builder,
				id,
				m_storageAccess->getNameHierarchyForNodeId(id),
				m_storageAccess->getNodeTypeForNodeWithId(id).getKind()));
		}
		const auto activeOffset = builder.CreateVector(activeNodes);

		flatbuffers::Offset<fb::Graph> graphOffset = 0;
		if (!activeNodeIds.empty())
		{
			if (const std::shared_ptr<Graph> graph =
					m_storageAccess->getGraphForActiveTokenIds(activeNodeIds, {}, nullptr))
			{
				std::vector<flatbuffers::Offset<fb::NodeRef>> nodes;
				std::vector<flatbuffers::Offset<fb::EdgeRef>> edges;
				graph->forEachNode([&](Node* node) {
					nodes.push_back(makeNodeRef(
						builder, node->getId(), node->getNameHierarchy(), node->getType().getKind(), node->getName()));
				});
				graph->forEachEdge([&](Edge* edge) {
					edges.push_back(fb::CreateEdgeRef(
						builder,
						static_cast<std::uint64_t>(edge->getId()),
						static_cast<std::int32_t>(edge->getType()),
						builder.CreateString(NameHierarchy::serialize(edge->getFrom()->getNameHierarchy())),
						builder.CreateString(NameHierarchy::serialize(edge->getTo()->getNameHierarchy()))));
				});
				graphOffset = fb::CreateGraph(builder, builder.CreateVector(nodes), builder.CreateVector(edges));
			}
		}

		flatbuffers::Offset<fb::CodeViewState> codeOffset = 0;
		if (!currentFile.empty())
		{
			codeOffset = fb::CreateCodeViewState(builder, builder.CreateString(currentFile), scrollLine);
		}

		std::vector<flatbuffers::Offset<fb::SearchMatch>> searchMatches;
		for (const SearchMatch& match: lastSearchMatches)
		{
			searchMatches.push_back(makeSearchMatch(builder, match));
		}
		const auto searchOffset = builder.CreateVector(searchMatches);

		std::vector<flatbuffers::Offset<fb::BookmarkInfo>> bookmarks;
		for (const NodeBookmark& bookmark: m_storageAccess->getAllNodeBookmarks())
		{
			bookmarks.push_back(makeBookmark(builder, bookmark));
		}
		for (const EdgeBookmark& bookmark: m_storageAccess->getAllEdgeBookmarks())
		{
			bookmarks.push_back(makeBookmark(builder, bookmark));
		}
		const auto bookmarksOffset = builder.CreateVector(bookmarks);

		std::vector<flatbuffers::Offset<fb::TabInfo>> tabInfos;
		for (const auto& tab: tabsCache)
		{
			tabInfos.push_back(makeTabInfo(builder, tab));
		}
		const auto tabsOffset = builder.CreateVector(tabInfos);

		const std::string dialog = utility::qt::currentDialog();
		// Always written, empty included: an absent field is how the bridge tells an app
		// before protocol v3, which cannot report a dialog, from one that has none.
		const auto dialogOffset = builder.CreateString(dialog);

		const auto machinesOffset = builder.CreateVector(makeMachineStates(builder));

		const auto viewGraphOffset = builder.CreateVector(makeGraphView(builder, TabIds::currentTab()));

		const ErrorCountInfo errors = m_storageAccess->getErrorCount();
		const auto uiState = fb::CreateUiState(
			builder,
			projectOffset,
			activeOffset,
			graphOffset,
			codeOffset,
			searchOffset,
			tabsOffset,
			bookmarksOffset,
			static_cast<std::uint32_t>(errors.total),
			static_cast<std::uint32_t>(errors.fatal),
			m_indexing.load(),
			computeAppState(),
			dialogOffset,
			machinesOffset,
			viewGraphOffset);
		builder.Finish(fb::CreateUiStateEnvelope(builder, requestId, uiState));
		m_stateChannel.send(builder.GetBufferPointer(), builder.GetSize());
	}

	// The shared log funnel (LogManager + Qt handler both call it, on arbitrary
	// threads): apply the agent filter and publish a LogEvent. publishEvent is
	// mutex-serialized; a thread-local guard breaks recursion if the publish path
	// itself logs.
	void forwardLog(int level, fb::LogSource source, const std::string& category,
		const std::string& message, const std::string& file, const std::string& function, int line)
	{
		thread_local bool inSink = false;
		if (inSink || !m_logEnabled.load(std::memory_order_relaxed) ||
			level < m_logMinLevel.load(std::memory_order_relaxed))
		{
			return;
		}
		if (m_eventsChannel.recv_count() == 0)
		{
			return;	// no agent subscribed — don't pay to encode/publish
		}
		{
			const std::lock_guard<std::mutex> lock(m_logConfigMutex);
			if (m_logHasPattern && !std::regex_search(category + ": " + message, m_logPattern))
			{
				return;
			}
		}
		inSink = true;
		const struct Reset
		{
			bool& flag;
			~Reset()
			{
				flag = false;
			}
		} reset{inSink};

		flatbuffers::FlatBufferBuilder builder;
		const auto cat = builder.CreateString(category);
		const auto msg = builder.CreateString(message);
		const auto fil = builder.CreateString(file);
		const auto fun = builder.CreateString(function);
		const auto event = fb::CreateLogEvent(
			builder, static_cast<fb::LogLevel>(level), source, cat, msg, fil, fun,
			static_cast<std::uint32_t>(line));
		publishEvent(builder, fb::Event_LogEvent, event.Union());
	}

	// One FrameEnvelope for a CaptureElement / GetFrame reply, on st.agent.frames.
	// Single chunk (thoth-ipc fragments large messages itself); correlated by
	// request_id. Runs on the ui() thread, the sole sender on m_framesChannel.
	//
	// Callers ack before they publish. On Windows thoth-ipc never puts a large
	// message in chunk storage: it splits it into 64-byte fragments in the ring,
	// which holds about 16 KB, so a frame only gets through while the bridge is
	// reading the channel. The bridge reads frames once it has the ack; a frame
	// published first filled the ring with nobody reading, and anything over the
	// ring's size was lost (the bridge timed out waiting for it).
	void publishFrame(std::uint64_t requestId, const utility::qt::CaptureResult& result)
	{
		if (m_framesChannel.recv_count() == 0)
		{
			return;	// the requester went away: don't encode a frame nobody reads
		}
		flatbuffers::FlatBufferBuilder builder;
		const auto payload = builder.CreateVector(result.png);
		fb::FrameEnvelopeBuilder fb_builder(builder);
		fb_builder.add_frame_seq(requestId);
		fb_builder.add_timestamp_ms(nowMs());
		fb_builder.add_format(fb::FrameFormat_Png);
		fb_builder.add_width(result.width);
		fb_builder.add_height(result.height);
		fb_builder.add_chunk_index(0);
		fb_builder.add_chunk_count(1);
		fb_builder.add_total_bytes(result.png.size());
		fb_builder.add_payload(payload);
		fb_builder.add_request_id(requestId);
		builder.Finish(fb_builder.Finish());
		m_framesChannel.send(builder.GetBufferPointer(), builder.GetSize());
	}

	// `cause`: the request the event follows from (caused_by), by default the one the
	// publishing thread works for.
	void publishEvent(
		flatbuffers::FlatBufferBuilder& builder,
		fb::Event type,
		flatbuffers::Offset<void> event,
		std::uint64_t cause = currentCause())
	{
		// Serialized: events are normally published from the app-scheduler thread,
		// but InvokeAction acks from the ui() thread — the lock keeps the seq counter
		// and the route send consistent across both.
		const std::lock_guard<std::mutex> lock(m_eventMutex);
		builder.Finish(fb::CreateEventEnvelope(builder, m_eventSeq++, nowMs(), type, event, cause));
		// No bridge attached: skip the send, which thoth-ipc would otherwise report
		// as failed for every app event. The seq still advances, so a bridge that
		// attaches later sees the gap and knows it missed events.
		if (m_eventsChannel.recv_count() > 0)
		{
			m_eventsChannel.send(builder.GetBufferPointer(), builder.GetSize());
		}
	}

	// The coarse application state, derived from the app, project and indexing state
	// machines. Loading covers the app starting and a project loading: from the
	// request until the project is open and, when the load opens a tab, that tab's
	// first activation (the overview) has landed, so nothing of the load arrives
	// after an agent's first navigation. A refresh still being decided (indexing
	// pending: the mode choice or a question is about to come up) reads as Loading
	// too, so a load that leaves one returns with it in view. Busy is reserved.
	fb::AppState computeAppState() const
	{
		if (m_indexing)
		{
			return fb::AppState_Indexing;
		}
		if (AppStateMachine::instance()->isStarting())
		{
			return fb::AppState_Loading;
		}
		const std::string project = ProjectStateMachine::instance()->snapshot().state;
		if (project == "ready")
		{
			return IndexingStateMachine::instance()->snapshot().state == "pending" ? fb::AppState_Loading
																				   : fb::AppState_Ready;
		}
		return project == "no_project" ? fb::AppState_NoProject : fb::AppState_Loading;
	}

	// `cause`: what the change follows from (a machine transition's), by default the
	// request the calling thread works for.
	void updateAppState(std::uint64_t cause = currentCause())
	{
		const fb::AppState next = computeAppState();
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			if (next == m_appState)
			{
				return;
			}
			m_appState = next;
		}
		flatbuffers::FlatBufferBuilder builder;
		publishEvent(builder, fb::Event_AppStateChanged, fb::CreateAppStateChanged(builder, next).Union(), cause);
	}

	void publishTransition(const MachineTransition& transition)
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto machine = builder.CreateString(transition.machine);
		const auto from = builder.CreateString(transition.from);
		const auto to = builder.CreateString(transition.to);
		const auto event = builder.CreateString(transition.event);
		const auto source = builder.CreateString(eventSourceName(transition.source));
		const auto context = builder.CreateString(transition.context);
		const auto offset = fb::CreateMachineTransition(
			builder, machine, from, to, event, source, context, transition.revision);
		publishEvent(builder, fb::Event_MachineTransition, offset.Union(), transition.cause);
	}

	// A dialog came up or went away (protocol v7).
	void publishDialogChanged(const std::string& dialog)
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto offset = fb::CreateDialogChanged(builder, builder.CreateString(dialog));
		publishEvent(builder, fb::Event_DialogChanged, offset.Union());
	}

	void publishMachineStates(
		flatbuffers::FlatBufferBuilder& builder,
		std::uint64_t requestId,
		const std::vector<flatbuffers::Offset<fb::MachineState>>& machines)
	{
		const auto offset = fb::CreateMachineStates(builder, requestId, builder.CreateVector(machines));
		publishEvent(builder, fb::Event_MachineStates, offset.Union());
	}

	// Closed-loop ack for a command. ok=false with a "rejected: ..." message covers
	// state-gated rejections.
	void emitResult(std::uint64_t requestId, bool ok, const std::string& message = "")
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto result = fb::CreateCommandResult(builder, requestId, ok, builder.CreateString(message));
		publishEvent(builder, fb::Event_CommandResult, result.Union(), requestId);
	}

	StorageAccess* m_storageAccess;
	execution::ISchedulers* m_schedulers;	// io() drives the reader; ui() reserved for Phase C frame grab
	std::string m_instanceId;	// channel namespace (empty = default st.agent.*)

	AgentCommandReader m_reader;	// owns the st.agent[.<id>].cmd route + reader thread (RFC seam)
	thoth::route m_stateChannel;
	thoth::route m_eventsChannel;
	thoth::route m_snapshotChannel;	// UiSnapshot replies to GetSnapshot
	thoth::route m_framesChannel;		// FrameEnvelope replies to CaptureElement / GetFrame
	exec::async_scope m_uiScope;	// GUI-thread-hopped work (snapshot capture), joined on stop()

	// State cache, written by message handlers on the app and tab schedulers and
	// read by publishUiState: guarded by m_stateMutex.
	std::mutex m_stateMutex;
	std::vector<Id> m_activeNodeIds;
	std::string m_currentFile;	   // the file the code view shows on its own, if any
	std::uint32_t m_scrollLine = 0;	   // its line, from the last MessageScrollToLine
	std::vector<SearchMatch> m_lastSearchMatches;
	std::vector<MessageTabsChanged::TabInfo> m_tabsCache;
	fb::AppState m_appState = fb::AppState_NoProject;
	std::atomic<bool> m_indexing{false};

	std::uint64_t m_eventSeq = 0;
	std::mutex m_eventMutex;	// serializes publishEvent across the app-scheduler + ui() threads

	// Log forwarding (SetLogFilter). Off until the agent opts in; touched from
	// arbitrary log threads, so config is atomic / mutex-guarded.
	std::shared_ptr<Logger> m_agentLogger;
	std::atomic<bool> m_logEnabled{false};
	std::atomic<int> m_logMinLevel{1};	// 0=Info 1=Warning 2=Error
	std::mutex m_logConfigMutex;
	std::regex m_logPattern;
	bool m_logHasPattern = false;
	std::mutex m_settleMutex;	// m_pendingSettle: flushed from whichever thread ends a request's work
	std::vector<std::uint64_t> m_pendingSettle;	// accepted request_ids awaiting their Settled

	bool m_listening = false;
	std::atomic<bool> m_stopping = false;	// stop() began: no GUI work is spawned any more
	std::size_t m_machineObserver = 0;	// publishes every state machine transition
	// Expires with the Impl: GUI-thread callbacks that may run after stop() check it.
	std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};

AgentControlController::AgentControlController(
	StorageAccess* storageAccess, execution::ISchedulers* schedulers, const std::string& instanceId)
{
	// Reset stale shm before the Impl's routes attach (its members connect in the
	// initializer list, so the wipe cannot move into Impl's own constructor).
	// Guards against phantom-receiver accumulation across instance-id reuse — see
	// clearStaleAgentChannels. Relying on the reaper alone is not enough: it skips
	// the owner-less phantom bits that are exactly the ones left by a killed peer.
	clearStaleAgentChannels(instanceId);
	m_impl = std::make_unique<Impl>(storageAccess, schedulers, instanceId);
}

AgentControlController::~AgentControlController()
{
	stopListening();
}

void AgentControlController::startListening()
{
	m_impl->start();
	m_impl->m_listening = true;
	LOG_INFO("agent-control: listening on " + agentChannel(m_impl->m_instanceId, "cmd") + " (thoth-ipc)");
}

void AgentControlController::stopListening()
{
	if (!m_impl->m_listening)
	{
		return;	// never started (the app quit before startup ended), or stopped already
	}
	m_impl->stop();
	m_impl->m_listening = false;
}

bool AgentControlController::isListening() const
{
	return m_impl->m_listening;
}

#endif	  // SOURCETRAIL_AGENT_CONTROL
