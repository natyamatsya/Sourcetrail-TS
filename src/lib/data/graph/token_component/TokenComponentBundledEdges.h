#ifndef TOKEN_COMPONENT_BUNDLED_EDGES_H
#define TOKEN_COMPONENT_BUNDLED_EDGES_H

#include <map>
#include <set>

#include "types.h"

#include "TokenComponent.h"

class TokenComponentBundledEdges: public TokenComponent
{
public:
	enum class Direction
	{
		DIRECTION_NONE,
		DIRECTION_FORWARD,
		DIRECTION_BACKWARD,
		DIRECTION_INVALID
	};

	static Direction opposite(Direction direction);

	TokenComponentBundledEdges();
	~TokenComponentBundledEdges() override;

	std::shared_ptr<TokenComponent> copy() const override;

	int getBundledEdgesCount() const;
	std::set<Id> getBundledEdgesIds() const;

	void addBundledEdgesId(Id id, bool forward);
	void removeBundledEdgesId(Id id);

	Direction getDirection();

private:
	std::map<Id, Direction> m_ids;
	Direction m_direction = Direction::DIRECTION_INVALID;
};

#endif	  // TOKEN_COMPONENT_BUNDLED_EDGES_H
