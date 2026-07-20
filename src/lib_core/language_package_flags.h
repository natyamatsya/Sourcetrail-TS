#ifndef LANGUAGE_PACKAGE_FLAGS_H
#define LANGUAGE_PACKAGE_FLAGS_H

#include "language_packages.h"

namespace language_packages
{
inline constexpr bool buildCxxLanguagePackage{BUILD_CXX_LANGUAGE_PACKAGE != 0};
inline constexpr bool buildRustLanguagePackage{BUILD_RUST_LANGUAGE_PACKAGE != 0};
inline constexpr bool buildSwiftLanguagePackage{BUILD_SWIFT_LANGUAGE_PACKAGE != 0};
inline constexpr bool buildZigLanguagePackage{BUILD_ZIG_LANGUAGE_PACKAGE != 0};
}

#endif	// LANGUAGE_PACKAGE_FLAGS_H
