#include "AgentControlController.h"

#ifndef SOURCETRAIL_AGENT_CONTROL

// Flag off: no-op stub so the class is always declarable/linkable but does nothing
// and pulls in no thoth-ipc / FlatBuffers / stdexec dependencies.
struct AgentControlController::Impl
{
};
AgentControlController::AgentControlController(StorageAccess*, execution::ISchedulers*) {}
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
#include <string>
#include <thread>
#include <vector>

#include <libipc/ipc.h>

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
	Impl(StorageAccess* storageAccess, execution::ISchedulers* schedulers)
		: m_storageAccess(storageAccess)
		, m_schedulers(schedulers)
		, m_cmdChannel("st.agent.cmd", ipc::receiver)
		, m_stateChannel("st.agent.state", ipc::sender)
		, m_eventsChannel("st.agent.events", ipc::sender)
	{
	}

	void start()
	{
		if (m_reader.joinable())
		{
			return;
		}
		m_reader = std::jthread([this]() { readLoop(); });
	}

	void stop()
	{
		m_stopSource.request_stop();
		if (m_reader.joinable())
		{
			m_reader.join();
		}
	}

	// Reader thread: blocking recv only. Each command is forwarded onto the
	// message thread via MessageAgentCommand (dispatch is thread-safe). The reader
	// never blocks on another thread, so stop() joins it within the recv timeout.
	void readLoop()
	{
		while (!m_stopSource.stop_requested())
		{
			ipc::buff_t buffer = m_cmdChannel.recv(200 /*ms; re-checks stop*/);
			if (buffer.empty())
			{
				continue;
			}
			const auto* bytes = static_cast<const std::uint8_t*>(buffer.data());
			MessageAgentCommand(std::vector<std::uint8_t>(bytes, bytes + buffer.size())).dispatch();
		}
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
	[[maybe_unused]] execution::ISchedulers* m_schedulers;	// reserved for Phase C frame grab (ui())

	ipc::route m_cmdChannel;
	ipc::route m_stateChannel;
	ipc::route m_eventsChannel;

	// State cache, touched only from the message-processing thread.
	std::vector<Id> m_activeNodeIds;
	std::string m_currentFile;
	std::vector<SearchMatch> m_lastSearchMatches;
	bool m_indexing = false;
	fb::AppState m_appState = fb::AppState_NoProject;
	std::uint64_t m_eventSeq = 0;

	stdexec::inplace_stop_source m_stopSource;
	std::jthread m_reader;
	bool m_listening = false;
};

AgentControlController::AgentControlController(StorageAccess* storageAccess, execution::ISchedulers* schedulers)
	: m_impl(std::make_unique<Impl>(storageAccess, schedulers))
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
	LOG_INFO("agent-control: listening on st.agent.cmd (thoth-ipc)");
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
