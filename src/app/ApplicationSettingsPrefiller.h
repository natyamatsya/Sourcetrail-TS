#ifndef APPLICATION_SETTINGS_PREFILLER_H
#define APPLICATION_SETTINGS_PREFILLER_H

class ApplicationSettings;

class ApplicationSettingsPrefiller
{
public:
	static void prefillPaths(ApplicationSettings* settings);

private:
	static bool prefillCxxHeaderPaths(ApplicationSettings* settings);
	static bool prefillCxxFrameworkPaths(ApplicationSettings* settings);

	//! Cleanup of search paths stored by older versions on macOS: an SDK's
	//! usr/include or another toolchain's libc++ breaks libc++ #include_next once
	//! -isysroot is injected, and another SDK's frameworks do not match it.
	static bool migrateSysrootSuppliedPaths(ApplicationSettings* settings);
};

#endif	  // APPLICATION_SETTINGS_PREFILLER_H
