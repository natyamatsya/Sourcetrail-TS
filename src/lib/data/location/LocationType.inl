// Inline implementations for LocationType.h (included at the end of that header).
// `intToEnum` / `lookupEnum` come from utilityEnum (srctrl.utility) -- via #include in the header
// build, via `import srctrl.utility;` in the module build.

#pragma once

template <>
inline LocationType intToEnum(int value)
{
	static const LocationType LOCATION_TYPES[] = {
		LocationType::TOKEN,
		LocationType::SCOPE,
		LocationType::QUALIFIER,
		LocationType::LOCAL_SYMBOL,
		LocationType::SIGNATURE,
		LocationType::COMMENT,
		LocationType::ERROR,
		LocationType::FULLTEXT_SEARCH,
		LocationType::SCREEN_SEARCH,
		LocationType::UNSOLVED};

	return lookupEnum(value, LOCATION_TYPES, LocationType::TOKEN);
}

inline std::size_t operator << (std::size_t bits, LocationType locationType)
{
	return bits << static_cast<int>(locationType);
}
