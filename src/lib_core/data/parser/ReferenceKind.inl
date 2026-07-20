// Inline implementations for ReferenceKind.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline ReferenceKind intToEnum(int value)
{
	static const ReferenceKind REFERENCE_KINDS[] = {
		ReferenceKind::UNDEFINED,
		ReferenceKind::TYPE_USAGE,
		ReferenceKind::USAGE,
		ReferenceKind::CALL,
		ReferenceKind::INHERITANCE,
		ReferenceKind::OVERRIDE,
		ReferenceKind::TYPE_ARGUMENT,
		ReferenceKind::TEMPLATE_SPECIALIZATION,
		ReferenceKind::INCLUDE,
		ReferenceKind::IMPORT,
		ReferenceKind::MACRO_USAGE,
		ReferenceKind::ANNOTATION_USAGE
	};

	return lookupEnum(value, REFERENCE_KINDS, ReferenceKind::UNDEFINED);
}
