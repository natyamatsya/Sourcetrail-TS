#include "ActivationController.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "StorageAccess.h"
#endif

#ifndef SRCTRL_MODULE_BUILD
#include "MessageActivateLegend.h"
#include "MessageActivateOverview.h"
#include "MessageActivateTokens.h"
#include "MessageChangeFileView.h"
#include "MessageErrorsAll.h"
#include "MessageFlushUpdates.h"
#include "MessageRefreshUI.h"
#include "MessageScrollToLine.h"
#include "MessageStatus.h"
#include "MessageTooltipShow.h"
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.settings;
import srctrl.storage;
import srctrl.utility;
// TooltipOrigin / CodeScrollParams came transitively through guarded includes; visibility
// does not flow through imports, so name the owners directly.
import srctrl.data;
import srctrl.view;
#endif

ActivationController::ActivationController(StorageAccess* storageAccess)
	: m_storageAccess(storageAccess)
{
}

void ActivationController::clear() {}

void ActivationController::handleMessage(MessageActivateEdge* message)
{
	if (message->isBundledEdges())
	{
		MessageActivateTokens m(message);
		m.tokenIds = message->bundledEdgesIds;
		m.setKeepContent(false);
		m.isBundledEdges = true;
		m.dispatchImmediately();
	}
	else
	{
		MessageActivateTokens m(message);
		m.tokenIds.push_back(message->tokenId);
		m.isEdge = true;
		m.dispatchImmediately();
	}
}

void ActivationController::handleMessage(MessageActivateFile* message)
{
	using enum MessageChangeFileView::FileState;
	using enum MessageChangeFileView::ViewMode;
	using enum TooltipOrigin;
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	Id fileId = m_storageAccess->getNodeIdForFileNode(message->filePath);

	if (fileId)
	{
		MessageActivateTokens messageActivateTokens(message);
		messageActivateTokens.tokenIds.push_back(fileId);
		messageActivateTokens.searchMatches = m_storageAccess->getSearchMatchesForTokenIds({fileId});
		messageActivateTokens.dispatchImmediately();
	}
	else
	{
		MessageChangeFileView msg(
			message->filePath,
			FILE_MAXIMIZED,
			VIEW_CURRENT,
			CodeScrollParams::toFile(message->filePath, CodeScrollParams::Target::VISIBLE));
		msg.setSchedulerId(message->getSchedulerId());
		msg.dispatchImmediately();
	}

	if (message->line > 0)
	{
		MessageScrollToLine msg(message->filePath, message->line);
		msg.setSchedulerId(message->getSchedulerId());
		msg.dispatch();
	}
}

void ActivationController::handleMessage(MessageActivateNodes* message)
{
	MessageActivateTokens m(message);
	for (const MessageActivateNodes::ActiveNode& node: message->nodes)
	{
		Id nodeId = node.nodeId;
		if (!nodeId)
		{
			nodeId = m_storageAccess->getNodeIdForNameHierarchy(node.nameHierarchy);
		}

		if (nodeId > 0)
		{
			m.tokenIds.push_back(nodeId);
		}
	}
	m.searchMatches = m_storageAccess->getSearchMatchesForTokenIds(m.tokenIds);
	m.dispatchImmediately();
}

void ActivationController::handleMessage(MessageActivateTokenIds* message)
{
	MessageActivateTokens m(message);
	m.tokenIds = message->tokenIds;
	m.searchMatches = m_storageAccess->getSearchMatchesForTokenIds(message->tokenIds);
	m.dispatchImmediately();
}

void ActivationController::handleMessage(MessageActivateSourceLocations* message)
{
	using enum MessageChangeFileView::FileState;
	using enum MessageChangeFileView::ViewMode;
	using enum TooltipOrigin;
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	MessageActivateNodes msg;
	for (Id nodeId: m_storageAccess->getNodeIdsForLocationIds(message->locationIds))
	{
		msg.addNode(nodeId);
	}

	if (message->containsUnsolvedLocations && msg.nodes.size() == 1 &&
		m_storageAccess->getNameHierarchyForNodeId(msg.nodes[0].nodeId).getQualifiedName() ==
			"unsolved symbol")
	{
		MessageTooltipShow m(message->locationIds, {}, TooltipOrigin::TOOLTIP_ORIGIN_CODE);
		m.force = true;
		m.dispatch();
		return;
	}

	msg.setSchedulerId(message->getSchedulerId());
	msg.dispatchImmediately();
}

void ActivationController::handleMessage(MessageResetZoom*  /*message*/)
{
	ApplicationSettings* settings = ApplicationSettings::getInstance().get();
	int fontSizeStd = settings->getFontSizeStd();

	if (settings->getFontSize() != fontSizeStd)
	{
		settings->setFontSize(fontSizeStd);
		settings->save();

		MessageRefreshUI().dispatch();
	}

	MessageStatus("Font size: " + std::to_string(fontSizeStd)).dispatch();
}

void ActivationController::handleMessage(MessageSearch* message)
{
	using enum MessageChangeFileView::FileState;
	using enum MessageChangeFileView::ViewMode;
	using enum TooltipOrigin;
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	const std::vector<SearchMatch>& matches = message->getMatches();

	if (matches.size() && matches.back().searchType == SEARCH_COMMAND)
	{
		switch (matches.back().getCommandType())
		{
		case COMMAND_ALL:
		case COMMAND_NODE_FILTER:
		{
			MessageActivateOverview msg(message->acceptedNodeTypes);
			msg.setSchedulerId(message->getSchedulerId());
			msg.dispatch();
			return;
		}

		case COMMAND_ERROR:
		{
			MessageErrorsAll msg;
			msg.setSchedulerId(message->getSchedulerId());
			msg.dispatch();
			return;
		}

		case COMMAND_LEGEND:
		{
			MessageActivateLegend msg;
			msg.setSchedulerId(message->getSchedulerId());
			msg.dispatch();
			return;
		}
		}
	}

	MessageActivateTokens m(message);
	m.isFromSearch = true;
	for (const SearchMatch& match: matches)
	{
		if (match.tokenIds.size() && match.tokenIds[0] != 0)
		{
			utility::append(m.tokenIds, match.tokenIds);
			m.searchMatches.push_back(match);
		}
	}
	m.dispatchImmediately();
}

void ActivationController::handleMessage(MessageZoom* message)
{
	bool zoomIn = message->zoomIn;

	ApplicationSettings* settings = ApplicationSettings::getInstance().get();

	int fontSize = settings->getFontSize();
	int maxSize = settings->getFontSizeMax();
	int minSize = settings->getFontSizeMin();

	if ((fontSize >= maxSize && zoomIn) || (fontSize <= minSize && !zoomIn))
	{
		return;
	}

	settings->setFontSize(fontSize + (message->zoomIn ? 1 : -1));
	settings->save();

	MessageStatus("Font size: " + std::to_string(settings->getFontSize())).dispatch();
	MessageRefreshUI().dispatch();
}
