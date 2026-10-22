#include "Version.h"

#include <utility>

Version Version::s_version;

void Version::setApplicationVersion(const Version& version)
{
	s_version = version;
}

const Version& Version::getApplicationVersion()
{
	return s_version;
}

Version::Version(int major, int minor, int patch, std::string suffix)
	: m_majorNumber(major), m_minorNumber(minor), m_patchNumber(patch), m_suffix(std::move(suffix))
{
}

std::string Version::toDisplayString() const
{
	return std::to_string(m_majorNumber) + '.' + std::to_string(m_minorNumber) + '.' +
		std::to_string(m_patchNumber) + m_suffix;
}
