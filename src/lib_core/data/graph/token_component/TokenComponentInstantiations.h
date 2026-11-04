#ifndef TOKEN_COMPONENT_INSTANTIATIONS_H
#define TOKEN_COMPONENT_INSTANTIATIONS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "TokenComponent.h"
#endif

// On a static dispatch edge: the names of the template's instantiations it was derived from
// (render<Circle> for render -> Circle::draw), for its tooltip. See
// context/DESIGN_STATIC_DISPATCH_EDGES.md.
SRCTRL_EXPORT class TokenComponentInstantiations: public TokenComponent
{
public:
	explicit TokenComponentInstantiations(std::vector<std::string> names)
		: names(std::move(names))
	{
	}

	std::shared_ptr<TokenComponent> copy() const override
	{
		return std::make_shared<TokenComponentInstantiations>(*this);
	}

	const std::vector<std::string> names;
};

#endif	  // TOKEN_COMPONENT_INSTANTIATIONS_H
