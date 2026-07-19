// Copyright 2020 Peter Most, PERA Software Solutions GmbH
//
// This file is part of the CppAidKit library.
//
// CppAidKit is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// CppAidKit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with CppAidKit. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <QString>
#include <ostream>
#endif

namespace aidkit::qt {

SRCTRL_EXPORT QChar operator ""_qc(char c);
SRCTRL_EXPORT QString operator ""_qs(const char *str, std::size_t len);

SRCTRL_EXPORT std::ostream &operator<<(std::ostream &output, const QString &qstring);

}

// Inline implementations kept out of the header per the .inl convention. Making these functions
// inline (rather than out-of-line in Strings.cpp) is also what lets header-based consumers keep
// #including this header when AidKit is built as a module: they get their own definitions instead of
// depending on the now module-attached symbols.
#include "Strings.inl"
