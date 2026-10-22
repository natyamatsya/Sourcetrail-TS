#ifndef VERSION_H
#define VERSION_H

#include <string>

class Version
{
public:
	static void setApplicationVersion(const Version& version);
	static const Version& getApplicationVersion();

	Version() = default;
	// suffix: "" for a release, "-rc.1" for a pre-release tag, "+g<hash>" for any other commit.
	Version(int major, int minor, int patch, std::string suffix = {});

	std::string toDisplayString() const;

private:
	static Version s_version;

	int m_majorNumber = 0;
	int m_minorNumber = 0;
	int m_patchNumber = 0;
	std::string m_suffix;
};

#endif	  // VERSION_H
