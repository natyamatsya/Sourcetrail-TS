// Inline implementations for utilityMainFunction.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cassert>

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
}

inline bool isMainFunction(const NameHierarchy &nameHierarchy)
{
	return nameHierarchy.size() == 1 && nameHierarchy.back().hasSignature() &&
		nameHierarchy.back().getName() == utility_main_function_detail::MAIN_NAME;
}

inline void uniquifyMainFunction(NameHierarchy *main, const std::string &uniqueAppendix)
{
	assert(isMainFunction(*main));

	const NameElement::Signature signature = main->back().getSignature();

	main->pop();
	main->push(NameElement(
		utility_main_function_detail::DECODED_MAIN_NAME + uniqueAppendix,
		signature.getPrefix(),
		signature.getPostfix()));
}

inline bool isUniquifiedMainFunction(const NameHierarchy &nameHierarchy)
{
	using utility_main_function_detail::DECODED_MAIN_NAME;
	return nameHierarchy.size() == 1 && nameHierarchy.back().hasSignature() &&
		!nameHierarchy.back().getName().empty() &&
		nameHierarchy.back().getName()[0] == DECODED_MAIN_NAME[0] &&
		utility::isPrefix(DECODED_MAIN_NAME, nameHierarchy.back().getName());
}

inline void deuniquifyMainFunction(NameHierarchy *main)
{
	assert(isUniquifiedMainFunction(*main));

	NameElement::Signature signature = main->back().getSignature();
	main->pop();
	main->push(NameElement(
		utility_main_function_detail::MAIN_NAME, signature.getPrefix(), signature.getPostfix()));
}
