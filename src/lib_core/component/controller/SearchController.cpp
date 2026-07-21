#include "SearchController.h"

#ifndef SRCTRL_MODULE_BUILD
#include "MessageTabState.h"
#endif
#include "SearchView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "StorageAccess.h"
#endif
#include "logging.h"
#include "tracing.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.storage;
#endif

SearchController::SearchController(StorageAccess* storageAccess): m_storageAccess(storageAccess) {}

TabId SearchController::getSchedulerId() const
{
	return Controller::getTabId();
}

void SearchController::handleActivation(const MessageActivateBase* message)
{
	if (const MessageActivateTokens* m = dynamic_cast<const MessageActivateTokens*>(message))
	{
		if (!m->isEdge)
		{
			updateMatches(message, !m->keepContent());
		}
	}
	else if (const MessageActivateTrail* m = dynamic_cast<const MessageActivateTrail*>(message))
	{
		if (m->custom)
		{
			updateMatches(message);
		}
	}
	else
	{
		updateMatches(message);
	}
}

void SearchController::handleMessage(MessageFind* message)
{
	if (message->findFulltext)
	{
		getView()->findFulltext();
	}
	else
	{
		getView()->setFocus();
	}
}

void SearchController::handleMessage(MessageSearchAutocomplete* message)
{
	TRACE("search autocomplete");

	SearchView* view = getView();

	// Don't autocomplete if autocompletion request is not up-to-date anymore
	if (message->query != view->getQuery())
	{
		return;
	}

	LOG_INFO("autocomplete string: \"" + message->query + "\"");
	view->setAutocompletionList(m_storageAccess->getAutocompletionMatches(
		message->query, message->acceptedNodeTypes, true));
}

SearchView* SearchController::getView()
{
	return Controller::getView<SearchView>();
}

void SearchController::clear()
{
	updateMatches(nullptr);
}

void SearchController::updateMatches(const MessageActivateBase* message, bool updateView)
{
	std::vector<SearchMatch> matches;

	if (message)
	{
		matches = message->getSearchMatches();
	}

	if (updateView)
	{
		getView()->setMatches(matches);
	}

	MessageTabState(Controller::getTabId(), matches).dispatch();
}
