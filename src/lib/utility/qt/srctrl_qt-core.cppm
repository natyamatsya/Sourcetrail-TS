// `srctrl.qt:core` partition -- a C++20-module wrapper over the QtCore value types the non-GUI first-party
// code uses (Qt 6.11 ships no modules of its own). Mirrors Qt's own module split: `:core` = QtCore.
//
// The point of wrapping Qt behind an `import` (rather than #including it in each module's GMF): it confines
// Qt's textual pull of libc++ to *this* TU, so a consumer that `import srctrl.qt` + `import std` no longer
// hits the "Qt drags std into the global module fragment" conflict that blocks import std. Verified: the two
// imports coexist and QString<->std::string interop works across the boundary.
//
// Qt *macros* (QStringLiteral, Q_DECLARE_METATYPE, Q_OBJECT, ...) can't cross an `import`; a consumer that
// needs one keeps a minimal textual Qt include for just that macro (an include-only seam, like sqlpp23's
// name-tag). The value TYPES + free functions below are re-exported via `export using`.

module;

#include <QByteArray>
#include <QChar>
#include <QRegularExpression>
#include <QString>
#include <QtEnvironmentVariables>

export module srctrl.qt:core;

export
{
	using ::QByteArray;
	using ::QChar;
	using ::QRegularExpression;
	using ::QRegularExpressionMatch;
	using ::QString;

	// QtCore free functions (environment access) used by FilePath.
	using ::qEnvironmentVariable;
	using ::qgetenv;
}
