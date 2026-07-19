// Inline implementations for DefinitionKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template<>
inline DefinitionKind intToEnum(int value)
{
	static const DefinitionKind DEFINITION_KINDS[] = {
		DefinitionKind::NONE,
		DefinitionKind::IMPLICIT,
		DefinitionKind::EXPLICIT
	};

	return lookupEnum(value, DEFINITION_KINDS, DefinitionKind::NONE);
}
