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

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <libipc/ipc.h>

#include <flatbuffers/flatbuffers.h>

#include "agent_command_generated.h"
#include "agent_state_generated.h"

#include "ISchedulers.h"
#include "StdexecPrelude.h"

#include "Application.h"
#include "ErrorCountInfo.h"
#include "FilePath.h"
#include "Id.h"
#include "NameHierarchy.h"
#include "NodeTypeSet.h"
#include "RefreshInfo.h"
#include "StorageAccess.h"
#include "logging.h"

#include "MessageActivateFile.h"
#include "MessageActivateNodes.h"
#include "MessageHistoryRedo.h"
#include "MessageHistoryUndo.h"
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
}	 // namespace

struct AgentControlController::Impl
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

	// Reader thread: blocking recv only. Every command is handed to the UI thread
	// (where the message bus and StorageCache are safe) and processed there before
	// the next recv — serialized, with no work outliving this controller.
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
			std::vector<std::uint8_t> copy(bytes, bytes + buffer.size());
			stdexec::sync_wait(
				stdexec::schedule(m_schedulers->ui()) |
				stdexec::then([this, copy = std::move(copy)]() { handleCommand(copy.data(), copy.size()); }));
		}
	}

	// Runs on the Qt UI thread.
	void handleCommand(const std::uint8_t* data, std::size_t size)
	{
		flatbuffers::Verifier verifier(data, size);
		if (!fb::VerifyCommandEnvelopeBuffer(verifier))
		{
			LOG_ERROR("agent-control: malformed CommandEnvelope, dropped");
			return;
		}
		const auto* envelope = fb::GetCommandEnvelope(data);
		switch (envelope->command_type())
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
			publishUiState(envelope->request_id());
			break;
		default:
			LOG_INFO("agent-control: unhandled command type");
			break;
		}
	}

	// Runs on the Qt UI thread (StorageCache reads must). First cut: project path +
	// error/stat counts; the active graph, search results, code view, tabs and
	// bookmarks are follow-ups (they need live controller state).
	void publishUiState(std::uint64_t requestId)
	{
		flatbuffers::FlatBufferBuilder builder;
		std::string projectPath;
		if (const auto app = Application::getInstance())
		{
			projectPath = app->getCurrentProjectPath().str();
		}
		const ErrorCountInfo errors = m_storageAccess->getErrorCount();

		const auto projectOffset = builder.CreateString(projectPath);
		const auto uiState = fb::CreateUiState(
			builder,
			projectOffset,
			/*active_nodes*/ 0,
			/*graph*/ 0,
			/*code_view*/ 0,
			/*search_matches*/ 0,
			/*tabs*/ 0,
			/*bookmarks*/ 0,
			static_cast<std::uint32_t>(errors.total),
			static_cast<std::uint32_t>(errors.fatal),
			/*indexing_active*/ false);
		builder.Finish(fb::CreateUiStateEnvelope(builder, requestId, uiState));
		m_stateChannel.send(builder.GetBufferPointer(), builder.GetSize());
	}

	StorageAccess* m_storageAccess;
	execution::ISchedulers* m_schedulers;

	ipc::route m_cmdChannel;
	ipc::route m_stateChannel;
	ipc::route m_eventsChannel;

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
