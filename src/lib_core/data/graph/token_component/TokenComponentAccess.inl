// Inline member definitions for TokenComponentAccess.h (included at the end of that header). All
// members are inline because an out-of-line member of an exported class does not resolve for module
// importers.

#pragma once

inline std::string TokenComponentAccess::getAccessString(AccessKind access)
{
	switch (access)
	{
	case AccessKind::NONE:
		break;
	case AccessKind::PUBLIC:
		return "public";
	case AccessKind::PROTECTED:
		return "protected";
	case AccessKind::PRIVATE:
		return "private";
	case AccessKind::DEFAULT:
		return "default";
	case AccessKind::TEMPLATE_PARAMETER:
		return "template parameter";
	case AccessKind::TYPE_PARAMETER:
		return "type parameter";
	case AccessKind::PACKAGE:
		return "package";
	}
	return "";
}


inline TokenComponentAccess::TokenComponentAccess(AccessKind access): m_access(access) {}

inline TokenComponentAccess::~TokenComponentAccess() = default;

inline std::shared_ptr<TokenComponent> TokenComponentAccess::copy() const
{
	return std::make_shared<TokenComponentAccess>(*this);
}

inline AccessKind TokenComponentAccess::getAccess() const
{
	return m_access;
}

inline std::string TokenComponentAccess::getAccessString() const
{
	return getAccessString(m_access);
}
