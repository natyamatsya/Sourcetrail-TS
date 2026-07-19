#ifndef CXX_MODULE_PREBUILD_RUNNER_H
#define CXX_MODULE_PREBUILD_RUNNER_H

class FilePath;

// Runs inside the sourcetrail_indexer subprocess (invoked with --prebuild-modules=<request>). Reads
// the request JSON { cacheDir, sourceFiles[], baseFlags[] }, scans the files for C++20 module
// provides/requires, builds their BMIs into the cache directory in dependency order, and writes
// manifest.json { interfaceUnits[] }. Returns a process exit code (0 = success).
//
// This is the heavy, parse-arbitrary-user-code half of the module prebuild. It lives in the indexer
// subprocess -- not the main app process -- so a crash/hang while scanning or building a BMI takes
// down only the worker, exactly like normal indexing. See context/DESIGN_MODULE_PREBUILD.md.
class CxxModulePrebuildRunner
{
public:
	static int run(const FilePath& requestPath);
};

#endif	  // CXX_MODULE_PREBUILD_RUNNER_H
