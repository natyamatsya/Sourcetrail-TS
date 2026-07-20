#include "setupLocale.h"

#include <iostream>
#include <locale>
#include <stdexcept>

void setupDefaultLocale()
{
	try
	{
		std::locale defaultLocale("");
		std::locale::global(defaultLocale);
		std::cout.imbue(defaultLocale);
		std::cerr.imbue(defaultLocale);
	}
	catch (const std::runtime_error&)
	{
		// Fall back to classic locale if the system locale is unavailable
	}
}
