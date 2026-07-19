// Inline member definitions for NameHierarchy.h (included at the end of that header). All members are
// inline because an out-of-line member of an exported class does not resolve for module importers.
// The serialize/deserialize delimiters are namespace-scope inline constexpr string_views. The main
// function helpers come from utilityMainFunction (srctrl.utility) and LOG_ERROR from logging -- both
// #included in the header build, imported in the module build.

#pragma once

inline constexpr std::string_view META_DELIMITER      = "\tm";
inline constexpr std::string_view NAME_DELIMITER      = "\tn";
inline constexpr std::string_view PART_DELIMITER      = "\ts";
inline constexpr std::string_view SIGNATURE_DELIMITER = "\tp";

inline std::string NameHierarchy::serialize(const NameHierarchy &nameHierarchy)
{
	return serializeRange(nameHierarchy, 0, nameHierarchy.size());
}

inline std::string NameHierarchy::serializeRange(const NameHierarchy &nameHierarchy, size_t first, size_t last)
{
	std::stringstream ss;
	ss << nameHierarchy.getDelimiter();
	ss << META_DELIMITER;
	for (size_t i = first; i < last && i < nameHierarchy.size(); i++)
	{
		if (i > 0)
		{
			ss << NAME_DELIMITER;
		}

		ss << nameHierarchy[i].getName() << PART_DELIMITER;
		ss << nameHierarchy[i].getSignature().getPrefix();
		ss << SIGNATURE_DELIMITER;
		ss << nameHierarchy[i].getSignature().getPostfix();
	}
	return ss.str();
}

// NameHierarchy::deserialize stays out-of-line in NameHierarchy.cpp: it needs LOG_ERROR (logging.h)
// and utilityMainFunction (which forward-declares NameHierarchy, so it can't be in the module's GMF).
// It's therefore an include-only member (not reachable via `import`), like the logging/Qt seams.


inline NameHierarchy::NameHierarchy(std::string delimiter)
	: m_delimiter(std::move(delimiter))
{
}

inline NameHierarchy::NameHierarchy(std::string name, std::string delimiter)
	: m_delimiter(std::move(delimiter))
{
	push(std::move(name));
}

inline NameHierarchy::NameHierarchy(const NameDelimiterType delimiterType)
	: NameHierarchy(nameDelimiterTypeToString(delimiterType))
{
}

inline NameHierarchy::NameHierarchy(std::string name, const NameDelimiterType delimiterType)
	: NameHierarchy(name, nameDelimiterTypeToString(delimiterType))
{
}

inline void NameHierarchy::setDelimiter(std::string delimiter)
{
	m_delimiter = std::move(delimiter);
}

inline const std::string &NameHierarchy::getDelimiter() const
{
	return m_delimiter;
}

inline void NameHierarchy::push(NameElement element)
{
	m_elements.emplace_back(std::move(element));
}

inline void NameHierarchy::push(std::string name)
{
	m_elements.emplace_back(std::move(name));
}

inline void NameHierarchy::pop()
{
	m_elements.pop_back();
}

inline NameElement &NameHierarchy::back()
{
	return m_elements.back();
}

inline const NameElement &NameHierarchy::back() const
{
	return m_elements.back();
}

inline const NameElement &NameHierarchy::operator[](size_t pos) const
{
	return m_elements[pos];
}

inline NameHierarchy NameHierarchy::getRange(size_t first, size_t last) const
{
	NameHierarchy hierarchy(m_delimiter);

	for (size_t i = first; i < last; i++)
	{
		hierarchy.push(m_elements[i]);
	}

	return hierarchy;
}

inline size_t NameHierarchy::size() const
{
	return m_elements.size();
}

inline std::string NameHierarchy::getQualifiedName() const
{
	std::stringstream ss;
	for (size_t i = 0; i < m_elements.size(); i++)
	{
		if (i > 0)
		{
			ss << m_delimiter;
		}
		ss << m_elements[i].getName();
	}
	return ss.str();
}

inline std::string NameHierarchy::getQualifiedNameWithSignature() const
{
	std::string name = getQualifiedName();
	if (m_elements.size() != 0)
	{
		name = m_elements.back().getSignature().qualifyName(name); // todo: use separator for signature!
	}
	return name;
}

inline std::string NameHierarchy::getRawName() const
{
	if (m_elements.size() != 0)
	{
		return m_elements.back().getName();
	}
	return "";
}

inline std::string NameHierarchy::getRawNameWithSignature() const
{
	if (m_elements.size() != 0)
	{
		return m_elements.back().getNameWithSignature();
	}
	return "";
}

inline std::string NameHierarchy::getRawNameWithSignatureParameters() const
{
	if (m_elements.size() != 0)
	{
		return m_elements.back().getNameWithSignatureParameters();
	}
	return "";
}

inline bool NameHierarchy::hasSignature() const
{
	if (m_elements.size() != 0)
	{
		return m_elements.back().hasSignature();
	}

	return false;
}

inline NameElement::Signature NameHierarchy::getSignature() const
{
	if (m_elements.size() != 0)
	{
		return m_elements.back().getSignature(); // todo: use separator for signature!
	}

	return NameElement::Signature();
}
