#ifndef IDE_COMMUNICATION_CONTROLLER_H
#define IDE_COMMUNICATION_CONTROLLER_H

#include <mutex>
#include <string>

#include "Controller.h"
#include "NetworkProtocolHelper.h"

#include "MessageListener.h"
#include "MessageMoveIDECursor.h"
#include "MessageOpenInEditor.h"
#include "MessagePingReceived.h"
#include "MessagePluginPortChange.h"
#include "MessageWindowFocus.h"

class StorageAccess;

class IDECommunicationController
	: public Controller
	, public MessageListener<MessageWindowFocus>
	, public MessageListener<MessageMoveIDECursor>
	, public MessageListener<MessageOpenInEditor>
	, public MessageListener<MessagePingReceived>
	, public MessageListener<MessagePluginPortChange>
{
public:
	IDECommunicationController(StorageAccess* storageAccess);
	~IDECommunicationController() override;

	void clear() override;

	virtual void startListening() = 0;
	virtual void stopListening() = 0;
	virtual bool isListening() const = 0;

	void handleIncomingMessage(const std::string& message);

	bool getEnabled() const;
	void setEnabled(const bool enabled);

protected:
	void sendUpdatePing();

private:
	void handleSetActiveTokenMessage(const NetworkProtocolHelper::SetActiveTokenMessage& message);
	static void handleCreateProjectMessage(const NetworkProtocolHelper::CreateProjectMessage& message);
	static void handleCreateCDBProjectMessage(const NetworkProtocolHelper::CreateCDBProjectMessage& message);
	static void handlePing(const NetworkProtocolHelper::PingMessage& message);

	void handleMessage(MessageWindowFocus* message) override;
	void handleMessage(MessageMoveIDECursor* message) override;
	void handleMessage(MessageOpenInEditor* message) override;
	void handleMessage(MessagePingReceived* message) override;
	void handleMessage(MessagePluginPortChange* message) override;
	virtual void sendMessage(const std::string& message) const = 0;

	StorageAccess* m_storageAccess;

	bool m_enabled = true;

	// The IDE whose plugin answered the last ping; empty for none.
	mutable std::mutex m_ideMutex;
	std::string m_connectedIde;
};

#endif	  // IDE_COMMUNICATION_CONTROLLER_H
