#include "Catch2.hpp"

// This test suite verifies that std::__1::__hash_memory is callable and
// returns results consistent with the MurmurHash2-64 algorithm that libc++
// uses internally.  It is meaningful on macOS with Homebrew LLVM clang where
// the symbol is missing from Apple's system libc++.dylib and is provided
// instead by the libcxx_compat.cpp stub in cmake-toolchains/.

#if defined(__APPLE__) && defined(__clang__)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Reference MurmurHash2-64 — must match libcxx_compat.cpp exactly.
// We use this to verify the stub produces the correct hash values at runtime.
// ---------------------------------------------------------------------------
namespace
{

std::size_t murmur2_64_reference(const void* key, std::size_t len)
{
	const std::uint64_t m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;
	std::uint64_t h = len * m;
	const auto* data = static_cast<const std::uint8_t*>(key);
	const std::uint8_t* end = data + (len & ~static_cast<std::size_t>(7));
	while (data != end)
	{
		std::uint64_t k{};
		std::memcpy(&k, data, 8);
		k *= m;
		k ^= k >> r;
		k *= m;
		h ^= k;
		h *= m;
		data += 8;
	}
	switch (len & 7)
	{
	case 7: h ^= static_cast<std::uint64_t>(data[6]) << 48; [[fallthrough]];
	case 6: h ^= static_cast<std::uint64_t>(data[5]) << 40; [[fallthrough]];
	case 5: h ^= static_cast<std::uint64_t>(data[4]) << 32; [[fallthrough]];
	case 4: h ^= static_cast<std::uint64_t>(data[3]) << 24; [[fallthrough]];
	case 3: h ^= static_cast<std::uint64_t>(data[2]) << 16; [[fallthrough]];
	case 2: h ^= static_cast<std::uint64_t>(data[1]) << 8;  [[fallthrough]];
	case 1: h ^= static_cast<std::uint64_t>(data[0]);
		h *= m;
	}
	h ^= h >> r;
	h *= m;
	h ^= h >> r;
	return static_cast<std::size_t>(h);
}

}	// namespace

// ---------------------------------------------------------------------------
// Tests
//
// std::hash<std::string> calls __do_string_hash -> __hash_memory internally.
// If the stub symbol is missing, these tests fail at *link time* (not runtime),
// which is the primary safety net.  The runtime checks verify the stub returns
// values consistent with the MurmurHash2-64 reference.
// ---------------------------------------------------------------------------

TEST_CASE("LibcxxCompat: std::hash<std::string> links and runs", "[libcxx_compat]")
{
	// Link-time proof: if __hash_memory is missing, the test binary won't build.
	const std::hash<std::string> h{};
	CHECK(h("sourcetrail") == h("sourcetrail"));
	CHECK(h("sourcetrail") != h("Sourcetrail"));
	CHECK(h("") != h("x"));
}

TEST_CASE("LibcxxCompat: std::hash<std::string> matches MurmurHash2-64 reference", "[libcxx_compat]")
{
	// std::hash<std::string> on libc++ arm64 hashes via __hash_memory which is
	// MurmurHash2-64.  Verify the stub produces the right values.
	const std::hash<std::string> h{};
	for (const char* s : {"", "a", "hello", "the quick brown fox", "sourcetrail"})
	{
		const std::size_t expected = murmur2_64_reference(s, std::strlen(s));
		CHECK(h(std::string(s)) == expected);
	}
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
