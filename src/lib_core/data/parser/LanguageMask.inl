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

inline std::string languageMaskToDisplayString(LanguageMask languages)
{
	std::string result;
	// " + " rather than a space: these read as group titles and node labels,
	// where "C++ + Swift" says two languages met and "C++ Swift" reads as one
	// odd name.
	const auto add = [&result](const char* label) {
		if (!result.empty())
		{
			result += " + ";
		}
		result += label;
	};

	if (languageMaskHas(languages, LANGUAGE_CXX))
	{
		add("C++");
	}
	if (languageMaskHas(languages, LANGUAGE_RUST))
	{
		add("Rust");
	}
	if (languageMaskHas(languages, LANGUAGE_SWIFT))
	{
		add("Swift");
	}
	if (languageMaskHas(languages, LANGUAGE_ZIG))
	{
		add("Zig");
	}
	return result;
}
