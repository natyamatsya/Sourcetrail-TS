#ifndef MESSAGE_QUIT_APPLICATION_H
#define MESSAGE_QUIT_APPLICATION_H

#include "Message.h"

class MessageQuitApplication: public Message<MessageQuitApplication>
{
public:
	MessageQuitApplication() = default;

	// Headless runs propagate the indexing outcome as the process exit code (0 = success).
	explicit MessageQuitApplication(int exitCode): exitCode(exitCode) {}

	static const std::string getStaticType()
	{
		return "MessageQuitApplication";
	}

	int exitCode{0};
};

#endif	  // MESSAGE_QUIT_APPLICATION_H
