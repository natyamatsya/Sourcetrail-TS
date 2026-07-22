// Inline implementations for FilePathFilter.h. Included at the end of that header; not a
// standalone TU.
//
// Only the std-only members are inline here. The QRegularExpression / QRegularExpressionMatch-using
// members (constructor, isMatching(std::string), convertFilterStringToRegex) live OUT-OF-LINE in
// FilePathFilter.cpp: those Qt types are d-pointers whose *Private is only forward-declared in Qt's
// headers, and defining these members inline would place them in the srctrl.file module interface,
// where MSVC eagerly instantiates them and hits the incomplete Private (C2027). See
// docs/technical_notes/cxx20-modules-migration/msvc-bringup-findings.md.

#pragma once

inline std::string FilePathFilter::str() const
{
	return m_filterString;
}

inline bool FilePathFilter::isMatching(const FilePath& filePath) const
{
	return isMatching(filePath.str());
}

inline bool FilePathFilter::operator<(const FilePathFilter& other) const
{
	return m_filterString.compare(other.m_filterString) < 0;
}
