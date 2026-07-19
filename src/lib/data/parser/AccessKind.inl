// Inline implementations for AccessKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline AccessKind intToEnum(int value)
{
	static const AccessKind ACCESS_KINDS[] = {
		AccessKind::NONE,
		AccessKind::PUBLIC,
		AccessKind::PROTECTED,
		AccessKind::PRIVATE,
		AccessKind::DEFAULT,
		AccessKind::TEMPLATE_PARAMETER,
		AccessKind::TYPE_PARAMETER,
		AccessKind::PACKAGE
	};

	return lookupEnum(value, ACCESS_KINDS, AccessKind::NONE);
}

inline std::string accessKindToString(AccessKind t)
{
	switch (t)
	{
	case AccessKind::NONE:
		return "";
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
