// Inline implementations for LanguageMask.h (included at the end of that header). An out-of-line
// definition of an exported free function does not resolve for module importers, so it lives here as
// `inline`.

#pragma once

inline std::string languageMaskToString(LanguageMask languages)
{
	std::string result;
	const auto add = [&result](const char* label) {
		if (!result.empty())
		{
			result += ' ';
		}
		result += label;
	};

	// Declaration order, so a shared node reads the same way every time.
	if (languageMaskHas(languages, LANGUAGE_CXX))
	{
		add("cxx");
	}
	if (languageMaskHas(languages, LANGUAGE_RUST))
	{
		add("rust");
	}
	if (languageMaskHas(languages, LANGUAGE_SWIFT))
	{
		add("swift");
	}
	if (languageMaskHas(languages, LANGUAGE_ZIG))
	{
		add("zig");
	}
	return result;
}
