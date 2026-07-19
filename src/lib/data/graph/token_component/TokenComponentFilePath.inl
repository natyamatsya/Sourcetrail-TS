// Inline member definitions for TokenComponentFilePath.h (included at the end of that header). All
// members are inline because an out-of-line member of an exported class does not resolve for module
// importers.

#pragma once

inline TokenComponentFilePath::TokenComponentFilePath(const FilePath& path, bool complete)
	: m_path(path), m_complete(complete)
{
}

inline TokenComponentFilePath::~TokenComponentFilePath() = default;

inline std::shared_ptr<TokenComponent> TokenComponentFilePath::copy() const
{
	return std::make_shared<TokenComponentFilePath>(*this);
}

inline const FilePath& TokenComponentFilePath::getFilePath() const
{
	return m_path;
}

inline bool TokenComponentFilePath::isComplete() const
{
	return m_complete;
}
