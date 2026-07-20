// Inline implementations for TextAccess.h. Included at the end of that header; not a standalone TU.

#pragma once

namespace text_access_detail
{
inline std::istream& safeGetline(std::istream& is, std::string& t)
{
	t.clear();

	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	std::istream::sentry se(is, true);
	std::streambuf* sb = is.rdbuf();

	while (true)
	{
		const int c = sb->sbumpc();
		switch (c)
		{
		case '\n':
			return is;
		case '\r':
			if (sb->sgetc() == '\n')
			{
				sb->sbumpc();
			}
			return is;
		case std::streambuf::traits_type::eof():
			// Also handle the case when the last line has no line ending
			if (t.empty())
			{
				is.setstate(std::ios::eofbit);
			}
			return is;
		default:
			t += (char)c;
		}
	}
}
}	 // namespace text_access_detail

inline std::shared_ptr<TextAccess> TextAccess::createFromFile(const FilePath& filePath)
{
	std::shared_ptr<TextAccess> result(new TextAccess());

	result->m_filePath = filePath;
	result->m_lines = readFile(filePath);

	return result;
}

inline std::shared_ptr<TextAccess> TextAccess::createFromString(
	const std::string& text, const FilePath& filePath)
{
	std::shared_ptr<TextAccess> result(new TextAccess());

	result->m_lines = splitStringByLines(text);
	result->m_filePath = filePath;

	return result;
}

inline std::shared_ptr<TextAccess> TextAccess::createFromLines(
	const std::vector<std::string>& lines, const FilePath& filePath)
{
	std::shared_ptr<TextAccess> result(new TextAccess());

	result->m_lines = lines;
	result->m_filePath = filePath;

	return result;
}

inline unsigned int TextAccess::getLineCount() const
{
	return static_cast<unsigned int>(m_lines.size());
}

inline bool TextAccess::isEmpty() const
{
	return m_lines.empty();
}

inline FilePath TextAccess::getFilePath() const
{
	return m_filePath;
}

inline std::string TextAccess::getLine(const unsigned int lineNumber) const
{
	if (!checkIndexInRange(lineNumber))
	{
		return "";
	}

	return m_lines[lineNumber - 1];	   // -1 to correct for use as index
}

inline std::vector<std::string> TextAccess::getLines(
	const unsigned int firstLineNumber, const unsigned int lastLineNumber)
{
	if (!checkIndexIntervalInRange(firstLineNumber, lastLineNumber))
	{
		return std::vector<std::string>();
	}

	std::vector<std::string>::iterator first = m_lines.begin() + firstLineNumber -
		1;	  // -1 to correct for use as index
	std::vector<std::string>::iterator last = m_lines.begin() + lastLineNumber;
	return std::vector<std::string>(first, last);
}

inline const std::vector<std::string>& TextAccess::getAllLines() const
{
	return m_lines;
}

inline std::string TextAccess::getText() const
{
	std::string result;

	for (unsigned int i = 0; i < m_lines.size(); i++)
	{
		result += m_lines[i];
	}

	return result;
}

inline std::vector<std::string> TextAccess::readFile(const FilePath& filePath)
{
	std::vector<std::string> result;

	try
	{
		std::ifstream srcFile;
		srcFile.open(filePath.str(), std::ios::binary | std::ios::in);

		if (srcFile.fail())
		{
			LOG_ERROR("Could not open file " + filePath.str());
			return result;
		}

		while (!srcFile.eof())
		{
			std::string line;
			text_access_detail::safeGetline(srcFile, line);
			result.push_back(line + '\n');
		}

		srcFile.close();
	}
	catch (std::exception& e)
	{
		LOG_ERROR_STREAM(
			<< "Exception thrown while reading file \"" << filePath.str() << "\": " << e.what());
		result.clear();
	}
	catch (...)
	{
		LOG_ERROR_STREAM(<< "Unknown exception thrown while reading file \"" << filePath.str() << "\"");
		result.clear();
	}

	if (!result.empty())
	{
		std::string last = result.back().substr(0, result.back().size() - 1);
		result.pop_back();
		if (!last.empty())
		{
			result.push_back(last);
		}
	}

	return result;
}

inline std::vector<std::string> TextAccess::splitStringByLines(const std::string& text)
{
	std::vector<std::string> result;
	size_t prevIndex = 0;
	size_t index = text.find('\n');

	while (index < text.length())
	{
		result.push_back(text.substr(prevIndex, index - prevIndex) + '\n');

		prevIndex = index + 1;
		index = text.find('\n', prevIndex);
	}

	if (prevIndex < text.length())
	{
		result.push_back(text.substr(prevIndex));
	}

	return result;
}

inline TextAccess::TextAccess(): m_filePath("") {}

inline bool TextAccess::checkIndexInRange(const unsigned int index) const
{
	if (index < 1)
	{
		LOG_WARNING_STREAM(<< "Line numbers start with one, is " << index);
		return false;
	}
	else if (index > m_lines.size())
	{
		LOG_WARNING_STREAM(
			<< "Tried to access index " << index << ". Maximum index is " << m_lines.size());
		return false;
	}

	return true;
}

inline bool TextAccess::checkIndexIntervalInRange(
	const unsigned int firstIndex, const unsigned int lastIndex) const
{
	if (!checkIndexInRange(firstIndex) || !checkIndexInRange(lastIndex))
	{
		return false;
	}
	else if (firstIndex > lastIndex)
	{
		LOG_WARNING_STREAM(
			<< "Index 'firstLine' has to be lower or equal index 'lastLine', is " << firstIndex
			<< " > " << lastIndex);
		return false;
	}

	return true;
}
