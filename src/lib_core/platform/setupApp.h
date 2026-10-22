#ifndef SETUP_PLATFORM_H
#define SETUP_PLATFORM_H

#include <FilePath.h>
#include <Version.h>

// setupDefaultLocale lives in setupLocale.h (FilePath-free); included here so existing app/test
// bootstrap consumers keep getting all three setup functions from one header.
#include <setupLocale.h>

// `userDataDir` overrides the per-user data location (--user-data-dir); empty = the default.
Version setupAppDirectories(const FilePath &appPath, const FilePath &userDataDir = FilePath());
void setupAppEnvironment(int argc, char *argv[]);

#endif
