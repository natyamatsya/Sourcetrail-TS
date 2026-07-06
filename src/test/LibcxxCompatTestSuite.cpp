#include "Catch2.hpp"

// This test suite verifies that std::__1::__hash_memory resolves (link-time
// safety net) and that std::hash<std::string> is self-consistent through real
// container use.  On macOS with Homebrew LLVM clang the symbol comes either
// from Apple's system libc++.dylib (current macOS) or, when the dylib lacks
// it, from the libcxx_compat.cpp stub injected by cmake-toolchains/ (the
// toolchain probes at configure time and injects only when needed).
//
// Deliberately NOT tested: the concrete hash algorithm.  __hash_memory is
// out-of-line ABI exactly so libc++ may change it between versions (murmur2,
// rapidhash, ...).  Pinning the algorithm made this suite fail — and worse,
// when both the stub and the dylib provided (different) implementations, the
// process had two std::hash<std::string> variants and unordered containers
// crashed.  The container tests below are the canary for that split-brain.

#if defined(__APPLE__) && defined(__clang__)

#include <string>
#include <unordered_map>
#include <unordered_set>

TEST_CASE("LibcxxCompat: std::hash<std::string> links and runs", "[libcxx_compat]")
{
	// Link-time proof: if __hash_memory is missing, the test binary won't build.
	const std::hash<std::string> h{};
	CHECK(h("sourcetrail") == h("sourcetrail"));
	CHECK(h("sourcetrail") != h("Sourcetrail"));
	CHECK(h("") != h("x"));
}

TEST_CASE("LibcxxCompat: unordered_map with string keys works", "[libcxx_compat]")
{
	// Exercises __hash_memory through a real container use-case.
	std::unordered_map<std::string, int> m;
	m["foo"] = 1;
	m["bar"] = 2;
	m["baz"] = 3;
	REQUIRE(m.size() == 3);
	CHECK(m.at("foo") == 1);
	CHECK(m.at("bar") == 2);
	CHECK(m.find("qux") == m.end());
}

TEST_CASE("LibcxxCompat: unordered_set with string keys works", "[libcxx_compat]")
{
	std::unordered_set<std::string> s{"alpha", "beta", "gamma"};
	CHECK(s.count("alpha") == 1);
	CHECK(s.count("delta") == 0);
}

#endif	// defined(__APPLE__) && defined(__clang__)
