// Inline member definitions for NameElement.h (included at the end of that header). All members are
// inline because an out-of-line member of an exported class does not resolve for module importers.
// `utility::substrBeforeLast` comes from utilityString (srctrl.utility) -- #included in the header
// build, imported in the module build.

#pragma once

inline NameElement::Signature::Signature(std::string prefix, std::string postfix)
	: m_prefix(std::move(prefix))
	, m_postfix(std::move(postfix))
{
}

inline std::string NameElement::Signature::qualifyName(const std::string& name) const
{
	if (!isValid())
	{
		return name;
	}

	std::string qualifiedName = m_prefix;
	if (!name.empty())
	{
		if (!m_prefix.empty())
		{
			qualifiedName += " ";
		}
		qualifiedName += name;
	}
	qualifiedName += m_postfix;

	return qualifiedName;
}

inline bool NameElement::Signature::isValid() const
{
	return !m_prefix.empty() || !m_postfix.empty();
}

inline const std::string& NameElement::Signature::getPrefix() const
{
	return m_prefix;
}

inline const std::string& NameElement::Signature::getPostfix() const
{
	return m_postfix;
}

inline std::string NameElement::Signature::getParameterString() const
{
	if (!m_postfix.empty())
	{
		return utility::substrBeforeLast(m_postfix, ')') + ')';
	}

	return m_postfix;
}

inline NameElement::NameElement(std::string name)
	: m_name(std::move(name))
{
}

inline NameElement::NameElement(std::string name, std::string prefix, std::string postfix)
	: m_name(std::move(name))
	, m_signature(std::move(prefix), std::move(postfix))
{
}

inline const std::string& NameElement::getName() const
{
	return m_name;
}

inline std::string NameElement::getNameWithSignature() const
{
	return m_signature.qualifyName(m_name);
}

inline std::string NameElement::getNameWithSignatureParameters() const
{
	return m_name + m_signature.getParameterString();
}

inline bool NameElement::hasSignature() const
{
	return m_signature.isValid();
}

inline const NameElement::Signature& NameElement::getSignature() const
{
	return m_signature;
}

inline void NameElement::setSignature(std::string prefix, std::string postfix)
{
	m_signature = Signature(std::move(prefix), std::move(postfix));
}
