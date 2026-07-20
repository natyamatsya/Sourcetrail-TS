#ifndef SETUP_PLATFORM_H
#define SETUP_PLATFORM_H

#include <FilePath.h>
#include <Version.h>

// setupDefaultLocale lives in setupLocale.h (FilePath-free); included here so existing app/test
// bootstrap consumers keep getting all three setup functions from one header.
#include <setupLocale.h>

Version setupAppDirectories(const FilePath &appPath);
void setupAppEnvironment(int argc, char *argv[]);

#endif
