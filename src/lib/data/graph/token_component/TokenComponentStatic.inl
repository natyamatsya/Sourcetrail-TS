// Inline member definitions for TokenComponentStatic.h (included at the end of that header). All
// members are inline because an out-of-line member of an exported class does not resolve for module
// importers.

#pragma once

inline std::shared_ptr<TokenComponent> TokenComponentStatic::copy() const
{
	return std::make_shared<TokenComponentStatic>(*this);
}
