// Inline implementations for SymbolKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline SymbolKind intToEnum(int value)
{
	static const SymbolKind SYMBOL_KINDS[] = {
		SymbolKind::UNDEFINED,
		SymbolKind::ANNOTATION,
		SymbolKind::BUILTIN_TYPE,
		SymbolKind::CLASS,
		SymbolKind::ENUM,
		SymbolKind::ENUM_CONSTANT,
		SymbolKind::FIELD,
		SymbolKind::FUNCTION,
		SymbolKind::GLOBAL_VARIABLE,
		SymbolKind::INTERFACE,
		SymbolKind::MACRO,
		SymbolKind::METHOD,
		SymbolKind::MODULE,
		SymbolKind::NAMESPACE,
		SymbolKind::PACKAGE,
		SymbolKind::STRUCT,
		SymbolKind::TYPEDEF,
		SymbolKind::TYPE_PARAMETER,
		SymbolKind::UNION,
		SymbolKind::RECORD,
		SymbolKind::CONCEPT
	};

	return lookupEnum(value, SYMBOL_KINDS, SymbolKind::UNDEFINED);
}
