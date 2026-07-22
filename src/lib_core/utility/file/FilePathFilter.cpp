// Classic seam for FilePathFilter's Qt-using members.
//
// QRegularExpression and QRegularExpressionMatch are d-pointer types whose *Private is only
// forward-declared in Qt's headers (defined out-of-line in QtCore). Defining the members that use
// them inline in FilePathFilter.inl would place those bodies in the srctrl.file MODULE INTERFACE, and
// MSVC reifies inline module-purview members eagerly -- instantiating e.g.
// QExplicitlySharedDataPointer<QRegularExpressionMatchPrivate>::~dtor against the incomplete Private
// (error C2027). Compiled here as a plain classic TU (never an importer), these reference QtCore's
// out-of-line symbols instead -- exactly as every classic Qt user does. Under the global-module
// attachment model the definitions are ordinary-mangled, so they also link for importer callers of
// srctrl.file. Dual-build safe and a no-op for the clang build.
//
// See docs/technical_notes/cxx20-modules-migration/msvc-bringup-findings.md.

#include "FilePathFilter.h"

#include <QRegularExpression>
#include <QString>

FilePathFilter::FilePathFilter(const std::string& filterString)
	: m_filterString(filterString), m_filterRegex(convertFilterStringToRegex(filterString))
{
}

bool FilePathFilter::isMatching(const std::string& fileStr) const
{
	return m_filterRegex.match(QString::fromStdString(fileStr)).hasMatch();
}

QRegularExpression FilePathFilter::convertFilterStringToRegex(const std::string& filterString)
{
	QString filter = QString::fromStdString(filterString);

	// Normalize path separators to forward slash
	filter.replace('\\', '/');

	// Build the regex pattern character by character
	QString pattern;
	int i = 0;
	while (i < filter.size())
	{
		QChar c = filter[i];
		if (c == '*')
		{
			if (i + 1 < filter.size() && filter[i + 1] == '*')
			{
				pattern += QString(".*");
				i += 2;
			}
			else
			{
				pattern += QString("[^\\\\/]*");
				i++;
			}
		}
		else if (c == '/')
		{
			pattern += QString("[\\\\/]");
			i++;
		}
		else
		{
			pattern += QRegularExpression::escape(QString(c));
			i++;
		}
	}

	// Member prepend/append rather than the free operator+: the module build sees QString via
	// `export using` from srctrl.qt, which re-exports the type but not Qt's free operators.
	pattern.prepend(QString("\\A"));
	pattern.append(QString("\\z"));
	return QRegularExpression(pattern);
}
