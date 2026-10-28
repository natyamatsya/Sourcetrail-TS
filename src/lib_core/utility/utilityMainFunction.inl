// Inline implementations for utilityMainFunction.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cassert>
#include <cstddef>
#include <utility>

#include "NameHierarchy.h"
#include "utilityString.h"
#endif

// Note: This only fixes duplicated main functions, other duplicated functions still experience the described problem!
// Related issues:
// - "Nodes for different symbols with the same name are merged" (https://github.com/CoatiSoftware/Sourcetrail/issues/233)
// - "Not obvious which main() is chosen in Custom Trail dialog" (https://github.com/CoatiSoftware/Sourcetrail/issues/728)
// TODO: replace duplicate main definition fix with better solution.

// ODR-safe home for the constants: anonymous namespaces / plain statics are an ODR trap in
// headers/inls (each includer gets a distinct entity referenced from inline bodies).
namespace utility_main_function_detail
{
inline const std::string MAIN_NAME = "main";
inline const std::string DECODED_MAIN_NAME = ".:main:."; // Could this cause problems with module partitions? (https://en.cppreference.com/w/cpp/language/modules.html#Module_partitions)

// nameHierarchy with its first element replaced.
inline NameHierarchy replaceFirstElement(const NameHierarchy &nameHierarchy, NameElement first)
{
	NameHierarchy replaced = nameHierarchy.getRange(0, 0);
	replaced.push(std::move(first));
	for (std::size_t i = 1; i < nameHierarchy.size(); i++)
	{
		replaced.push(nameHierarchy.getRange(i, i + 1).back());
	}
	return replaced;
}
}

// Whether nameHierarchy is the global main().
inline bool isMainFunction(const NameHierarchy &nameHierarchy)
{
	return nameHierarchy.size() == 1 && nameHierarchy.back().hasSignature() &&
		nameHierarchy.back().getName() == utility_main_function_detail::MAIN_NAME;
}

// Whether nameHierarchy is declared in the global main() -- a lambda, a local class or enum: its
// first element is main's.
inline bool isInMainFunction(const NameHierarchy &nameHierarchy)
{
	return nameHierarchy.size() > 1 && nameHierarchy.front().hasSignature() &&
		nameHierarchy.front().getName() == utility_main_function_detail::MAIN_NAME;
}

// Makes main() unique by appending uniqueAppendix (its file) to its name. A name declared in main()
// gets the same first element, so it stays a child of that main().
inline void uniquifyMainFunction(NameHierarchy *main, const std::string &uniqueAppendix)
{
	assert(isMainFunction(*main) || isInMainFunction(*main));

	const NameElement::Signature signature = main->front().getSignature();

	*main = utility_main_function_detail::replaceFirstElement(
		*main,
		NameElement(
			utility_main_function_detail::DECODED_MAIN_NAME + uniqueAppendix,
			signature.getPrefix(),
			signature.getPostfix()));
}

// Whether nameHierarchy is a uniquified main(), or a name declared in one.
inline bool isUniquifiedMainFunction(const NameHierarchy &nameHierarchy)
{
	using utility_main_function_detail::DECODED_MAIN_NAME;
	if (nameHierarchy.size() == 0)
	{
		return false;
	}
	const NameElement &first = nameHierarchy.front();
	return first.hasSignature() && !first.getName().empty() &&
		first.getName()[0] == DECODED_MAIN_NAME[0] &&
		utility::isPrefix(DECODED_MAIN_NAME, first.getName());
}

inline void deuniquifyMainFunction(NameHierarchy *main)
{
	assert(isUniquifiedMainFunction(*main));

	const NameElement::Signature signature = main->front().getSignature();

	*main = utility_main_function_detail::replaceFirstElement(
		*main,
		NameElement(
			utility_main_function_detail::MAIN_NAME, signature.getPrefix(), signature.getPostfix()));
}
