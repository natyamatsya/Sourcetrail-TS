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

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <libipc/ipc.h>
#include <libipc/async_recv.h>	// self-guards on LIBIPC_STDEXEC; empty when off

#include <flatbuffers/flatbuffers.h>

#include "agent_command_generated.h"
#include "agent_event_generated.h"
#include "agent_state_generated.h"

#include "ISchedulers.h"
#include "StdexecPrelude.h"

#include "Application.h"
#include "Edge.h"
#include "ErrorCountInfo.h"
#include "FilePath.h"
#include "Graph.h"
#include "Id.h"
#include "MessageListener.h"
#include "NameHierarchy.h"
#include "Node.h"
#include "NodeTypeSet.h"
#include "RefreshInfo.h"
#include "StorageAccess.h"
#include "logging.h"

#include "Bookmark.h"
#include "BookmarkCategory.h"
#include "EdgeBookmark.h"
#include "NodeBookmark.h"
#include "SearchMatch.h"

#include "MessageAgentCommand.h"
#include "MessageActivateFile.h"
#include "MessageActivateNodes.h"
#include "MessageErrorCountUpdate.h"
#include "MessageHistoryRedo.h"
#include "MessageHistoryUndo.h"
#include "MessageIndexingFinished.h"
#include "MessageIndexingStarted.h"
#include "MessageLoadProject.h"
#include "MessageScrollToLine.h"
#include "MessageSearch.h"

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

std::uint64_t nowMs()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
											std::chrono::system_clock::now().time_since_epoch())
										.count());
}

flatbuffers::Offset<fb::NodeRef> makeNodeRef(
	flatbuffers::FlatBufferBuilder& builder,
	Id id,
	const std::string& serializedName,
	const std::string& displayName = "")
{
	return fb::CreateNodeRef(
		builder,
		static_cast<std::uint64_t>(id),
		builder.CreateString(serializedName),
		/*node_kind*/ 0,
		displayName.empty() ? 0 : builder.CreateString(displayName));
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
	return fb::CreateSearchMatch(
		builder, builder.CreateString(match.name), /*node_kind*/ 0, builder.CreateVector(nodeIds), match.score);
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

// Receives raw CommandEnvelope frames from a thoth-ipc route and hands each to a
// sink. Two implementations, selected by how libipc was built:
//
//   * LIBIPC_STDEXEC (POSIX): ipc::async_recv — no dedicated thread. Receives are
//     driven by libipc's process-global reactor; an exec::async_scope owns the
//     repeating receive and joins it on stop(). This is the RFC end-state
//     (submodules/thoth-ipc/context/stdexec-async-recv-rfc.md).
//   * otherwise (e.g. Windows, where libipc's Layer-1 readiness fd is not yet
//     implemented): a dedicated jthread blocking on recv().
//
// Both expose the same start(sink, scheduler) / stop() contract, so
// AgentControlController never learns which mechanism is underneath.
class AgentCommandReader
{
public:
	using Sink = std::function<void(std::vector<std::uint8_t>)>;

	explicit AgentCommandReader(const char* channelName): m_route(channelName, ipc::receiver) {}

	// `on` is the scheduler each command frame is delivered on (io()).
	void start(Sink sink, [[maybe_unused]] execution::AnyScheduler on)
	{
		m_sink = std::move(sink);
#if defined(LIBIPC_STDEXEC)
		// Repeat one async receive until the effect yields true; each completes on
		// `on`. async_recv delivers ipc::recv_result (a message, or a recv_errc —
		// its error channel is pruned per ADR-0001): a message yields false (keep
		// receiving); an error is logged and yields true (stop, rather than spin).
		// Cancellation comes from the scope's stop token (async_recv set_stopped).
		m_scope.spawn(exec::repeat_effect_until(
			ipc::async_recv(m_route, std::move(on)) |
			stdexec::then([this](ipc::recv_result result) {
				if (result)
				{
					deliver(*result);
					return false;
				}
				LOG_ERROR(ipc::recv_message(result.error()));
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
#if defined(LIBIPC_STDEXEC)
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
	void deliver(const ipc::buff_t& buffer)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(buffer.data());
		m_sink(std::vector<std::uint8_t>(bytes, bytes + buffer.size()));
	}

#if !defined(LIBIPC_STDEXEC)
	void loop()
	{
		while (!m_stopSource.stop_requested())
		{
			ipc::buff_t buffer = m_route.recv(200 /*ms; re-checks stop*/);
			if (!buffer.empty())
			{
				deliver(buffer);
			}
		}
	}
#endif

	ipc::route m_route;
	Sink m_sink;
#if defined(LIBIPC_STDEXEC)
	exec::async_scope m_scope;
#else
	stdexec::inplace_stop_source m_stopSource;
	std::jthread m_thread;
#endif
};
}	 // namespace

// The listeners run on the message-processing thread, consistently with every
// other controller — so reading StorageCache and mutating the state cache below
// need no extra synchronization (and no UI-thread hop).
struct AgentControlController::Impl
	: public MessageListener<MessageAgentCommand>
	, public MessageListener<MessageActivateNodes>
	, public MessageListener<MessageActivateFile>
	, public MessageListener<MessageSearch>
	, public MessageListener<MessageIndexingStarted>
	, public MessageListener<MessageIndexingFinished>
	, public MessageListener<MessageErrorCountUpdate>
{
	Impl(StorageAccess* storageAccess, execution::ISchedulers* schedulers, std::string instanceId)
		: m_storageAccess(storageAccess)
		, m_schedulers(schedulers)
		, m_instanceId(std::move(instanceId))
		, m_reader(agentChannel(m_instanceId, "cmd").c_str())
		, m_stateChannel(agentChannel(m_instanceId, "state").c_str(), ipc::sender)
		, m_eventsChannel(agentChannel(m_instanceId, "events").c_str(), ipc::sender)
	{
	}

	void start()
	{
		// Each command frame is forwarded onto the message thread via
		// MessageAgentCommand (dispatch is thread-safe), so it is handled where the
		// non-thread-safe StorageCache is safe to query. Delivery from the reader
		// runs on io(); the dispatch() hop then lands it on the message thread.
		m_reader.start(
			[](std::vector<std::uint8_t> bytes) { MessageAgentCommand(std::move(bytes)).dispatch(); },
			m_schedulers->io());
	}

	void stop()
	{
		m_reader.stop();
	}

	// ---- MessageListener overrides (message-processing thread) ----------------

	void handleMessage(MessageAgentCommand* message) override
	{
		handleCommand(message->bytes.data(), message->bytes.size());
	}

	void handleMessage(MessageActivateNodes* message) override
	{
		m_activeNodeIds.clear();
		flatbuffers::FlatBufferBuilder builder;
		std::vector<flatbuffers::Offset<fb::NodeRef>> nodes;
		for (const auto& active: message->nodes)
		{
			m_activeNodeIds.push_back(active.nodeId);
			nodes.push_back(makeNodeRef(builder, active.nodeId, NameHierarchy::serialize(active.nameHierarchy)));
		}
		const auto event = fb::CreateNodesActivated(builder, builder.CreateVector(nodes));
		publishEvent(builder, fb::Event_NodesActivated, event.Union());
		updateAppState();
	}

	void handleMessage(MessageActivateFile* message) override
	{
		m_currentFile = message->filePath.str();
		flatbuffers::FlatBufferBuilder builder;
		const auto event = fb::CreateFileActivated(builder, builder.CreateString(m_currentFile));
		publishEvent(builder, fb::Event_FileActivated, event.Union());
		updateAppState();
	}

	void handleMessage(MessageSearch* message) override
	{
		m_lastSearchMatches = message->getMatches();
		flatbuffers::FlatBufferBuilder builder;
		std::vector<flatbuffers::Offset<fb::SearchMatch>> matches;
		for (const SearchMatch& match: m_lastSearchMatches)
		{
			matches.push_back(makeSearchMatch(builder, match));
		}
		const auto event = fb::CreateSearchCompleted(builder, builder.CreateVector(matches));
		publishEvent(builder, fb::Event_SearchCompleted, event.Union());
	}

	void handleMessage(MessageIndexingStarted*) override
	{
		m_indexing = true;
		flatbuffers::FlatBufferBuilder builder;
		publishEvent(builder, fb::Event_IndexingStarted, fb::CreateIndexingStarted(builder).Union());
		updateAppState();
	}

	void handleMessage(MessageIndexingFinished*) override
	{
		m_indexing = false;
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

		// State gate: everything but load_project / get_ui_state needs a project.
		const bool needsProject = !(type == fb::Command_LoadProject || type == fb::Command_GetUiState);
		if (needsProject && computeAppState() == fb::AppState_NoProject)
		{
			emitResult(requestId, false, "rejected: no project loaded");
			return;
		}

		switch (type)
		{
		case fb::Command_LoadProject:
			if (const auto* c = envelope->command_as_LoadProject(); c && c->project_file_path())
			{
				MessageLoadProject(FilePath(c->project_file_path()->str()), false, toRefreshMode(c->refresh_mode()))
					.dispatch();
			}
			break;
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
				Id nodeId(static_cast<Id::type>(c->node_id()));
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
		default:
			emitResult(requestId, false, "unknown command");
			return;
		}
		emitResult(requestId, true);
	}

	// The deterministic "what's on screen", assembled from StorageAccess + the
	// state cache (both message-thread-local).
	void publishUiState(std::uint64_t requestId)
	{
		flatbuffers::FlatBufferBuilder builder;

		std::string projectPath;
		if (const auto app = Application::getInstance())
		{
			projectPath = app->getCurrentProjectPath().str();
		}
		const auto projectOffset = builder.CreateString(projectPath);

		std::vector<flatbuffers::Offset<fb::NodeRef>> activeNodes;
		for (const Id id: m_activeNodeIds)
		{
			activeNodes.push_back(
				makeNodeRef(builder, id, NameHierarchy::serialize(m_storageAccess->getNameHierarchyForNodeId(id))));
		}
		const auto activeOffset = builder.CreateVector(activeNodes);

		flatbuffers::Offset<fb::Graph> graphOffset = 0;
		if (!m_activeNodeIds.empty())
		{
			if (const std::shared_ptr<Graph> graph =
					m_storageAccess->getGraphForActiveTokenIds(m_activeNodeIds, {}, nullptr))
			{
				std::vector<flatbuffers::Offset<fb::NodeRef>> nodes;
				std::vector<flatbuffers::Offset<fb::EdgeRef>> edges;
				graph->forEachNode([&](Node* node) {
					nodes.push_back(makeNodeRef(
						builder, node->getId(), NameHierarchy::serialize(node->getNameHierarchy()), node->getName()));
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
		if (!m_currentFile.empty())
		{
			codeOffset = fb::CreateCodeViewState(builder, builder.CreateString(m_currentFile));
		}

		std::vector<flatbuffers::Offset<fb::SearchMatch>> searchMatches;
		for (const SearchMatch& match: m_lastSearchMatches)
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

		const ErrorCountInfo errors = m_storageAccess->getErrorCount();
		const auto uiState = fb::CreateUiState(
			builder,
			projectOffset,
			activeOffset,
			graphOffset,
			codeOffset,
			searchOffset,
			/*tabs*/ 0,	 // tabs live in TabsController, not reachable here — follow-up
			bookmarksOffset,
			static_cast<std::uint32_t>(errors.total),
			static_cast<std::uint32_t>(errors.fatal),
			m_indexing,
			computeAppState());
		builder.Finish(fb::CreateUiStateEnvelope(builder, requestId, uiState));
		m_stateChannel.send(builder.GetBufferPointer(), builder.GetSize());
	}

	void publishEvent(flatbuffers::FlatBufferBuilder& builder, fb::Event type, flatbuffers::Offset<void> event)
	{
		builder.Finish(fb::CreateEventEnvelope(builder, m_eventSeq++, nowMs(), type, event));
		m_eventsChannel.send(builder.GetBufferPointer(), builder.GetSize());
	}

	// The coarse application FSM, derived from Sourcetrail's project + indexing
	// state. Loading/Busy are reserved (no clean signal to drive them yet).
	fb::AppState computeAppState() const
	{
		if (m_indexing)
		{
			return fb::AppState_Indexing;
		}
		bool hasProject = false;
		if (const auto app = Application::getInstance())
		{
			hasProject = !app->getCurrentProjectPath().empty();
		}
		return hasProject ? fb::AppState_Ready : fb::AppState_NoProject;
	}

	void updateAppState()
	{
		const fb::AppState next = computeAppState();
		if (next != m_appState)
		{
			m_appState = next;
			flatbuffers::FlatBufferBuilder builder;
			publishEvent(
				builder, fb::Event_AppStateChanged, fb::CreateAppStateChanged(builder, m_appState).Union());
		}
	}

	// Closed-loop ack for a command. ok=false with a "rejected: ..." message covers
	// state-gated rejections.
	void emitResult(std::uint64_t requestId, bool ok, const std::string& message = "")
	{
		flatbuffers::FlatBufferBuilder builder;
		const auto result = fb::CreateCommandResult(builder, requestId, ok, builder.CreateString(message));
		publishEvent(builder, fb::Event_CommandResult, result.Union());
	}

	StorageAccess* m_storageAccess;
	execution::ISchedulers* m_schedulers;	// io() drives the reader; ui() reserved for Phase C frame grab
	std::string m_instanceId;	// channel namespace (empty = default st.agent.*)

	AgentCommandReader m_reader;	// owns the st.agent[.<id>].cmd route + reader thread (RFC seam)
	ipc::route m_stateChannel;
	ipc::route m_eventsChannel;

	// State cache, touched only from the message-processing thread.
	std::vector<Id> m_activeNodeIds;
	std::string m_currentFile;
	std::vector<SearchMatch> m_lastSearchMatches;
	bool m_indexing = false;
	fb::AppState m_appState = fb::AppState_NoProject;
	std::uint64_t m_eventSeq = 0;

	bool m_listening = false;
};

AgentControlController::AgentControlController(
	StorageAccess* storageAccess, execution::ISchedulers* schedulers, const std::string& instanceId)
	: m_impl(std::make_unique<Impl>(storageAccess, schedulers, instanceId))
{
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
	m_impl->stop();
	m_impl->m_listening = false;
}

bool AgentControlController::isListening() const
{
	return m_impl->m_listening;
}

#endif	  // SOURCETRAIL_AGENT_CONTROL
