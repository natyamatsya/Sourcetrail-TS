#ifndef MESSAGE_AGENT_COMMAND_H
#define MESSAGE_AGENT_COMMAND_H

#include <cstdint>
#include <string>
#include <vector>

#include "Message.h"
#include "TabIds.h"

// Internal to the agent-control channel: carries one raw CommandEnvelope from the
// thoth-ipc reader thread onto the message-processing thread, so it is handled in
// the same place (and thus the same thread) as every other controller's messages
// — where the non-thread-safe StorageCache is safe to query. Not one of the
// public message types; only AgentControlController dispatches/handles it.
class MessageAgentCommand: public Message<MessageAgentCommand>
{
public:
	static const std::string getStaticType()
	{
		return "MessageAgentCommand";
	}

	explicit MessageAgentCommand(std::vector<std::uint8_t> bytes): bytes(std::move(bytes))
	{
		setSchedulerId(TabIds::app());
	}

	std::vector<std::uint8_t> bytes;
};

#endif	  // MESSAGE_AGENT_COMMAND_H
