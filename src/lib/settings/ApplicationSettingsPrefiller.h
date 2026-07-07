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

	//! One-time cleanup of header search paths stored by older versions: an SDK
	//! usr/include entry breaks libc++ #include_next once -isysroot is injected.
	static bool migrateStaleSdkHeaderPaths(ApplicationSettings* settings);
};

#endif	  // APPLICATION_SETTINGS_PREFILLER_H
