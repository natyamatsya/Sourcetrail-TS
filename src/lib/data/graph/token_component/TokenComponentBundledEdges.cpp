#include "TokenComponentBundledEdges.h"

TokenComponentBundledEdges::Direction TokenComponentBundledEdges::opposite(Direction direction)
{
	using enum TokenComponentBundledEdges::Direction;
	if (direction == DIRECTION_FORWARD)
	{
		return DIRECTION_BACKWARD;
	}
	else if (direction == DIRECTION_BACKWARD)
	{
		return DIRECTION_FORWARD;
	}

	return direction;
}

TokenComponentBundledEdges::TokenComponentBundledEdges() = default;

TokenComponentBundledEdges::~TokenComponentBundledEdges() = default;

std::shared_ptr<TokenComponent> TokenComponentBundledEdges::copy() const
{
	return std::make_shared<TokenComponentBundledEdges>(*this);
}

int TokenComponentBundledEdges::getBundledEdgesCount() const
{
	return static_cast<int>(m_ids.size());
}

std::set<Id> TokenComponentBundledEdges::getBundledEdgesIds() const
{
	std::set<Id> ids;

	for (const auto &p: m_ids)
	{
		ids.insert(p.first);
	}

	return ids;
}

void TokenComponentBundledEdges::addBundledEdgesId(Id id, bool forward)
{
	using enum TokenComponentBundledEdges::Direction;
	m_ids.emplace(id, forward ? DIRECTION_FORWARD : DIRECTION_BACKWARD);

	m_direction = DIRECTION_INVALID;
}

void TokenComponentBundledEdges::removeBundledEdgesId(Id id)
{
	using enum TokenComponentBundledEdges::Direction;
	m_ids.erase(id);

	m_direction = DIRECTION_INVALID;
}

TokenComponentBundledEdges::Direction TokenComponentBundledEdges::getDirection()
{
	using enum TokenComponentBundledEdges::Direction;
	if (m_direction != DIRECTION_INVALID)
	{
		return m_direction;
	}

	m_direction = DIRECTION_NONE;

	for (const auto &p: m_ids)
	{
		if (m_direction == DIRECTION_NONE)
		{
			m_direction = p.second;
		}
		else if (m_direction != p.second)
		{
			m_direction = DIRECTION_NONE;
			break;
		}
	}

	return m_direction;
}
