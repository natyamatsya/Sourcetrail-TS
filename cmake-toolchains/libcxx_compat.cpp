// Stub implementation of std::__1::__hash_memory for Homebrew LLVM clang on
// macOS 26+.  Apple's system libc++.dylib no longer exports this symbol, but
// LLVM 18+ libc++ headers reference it when _LIBCPP_AVAILABILITY_HAS_HASH_MEMORY
// is defined.  We provide it here using the same MurmurHash2 / CityHash
// algorithm that libc++ uses internally, compiled as a proper exported symbol.
//
// Must be compiled with the same LLVM clang and -stdlib=libc++ flags as the
// rest of the project so it lands in the same inline namespace std::__1.

#include <cstddef>
#include <cstdint>

// Reproduce the murmur2-or-cityhash logic from libc++
// <__functional/hash.h> for size_t == 8 (arm64).
namespace
{

inline std::size_t murmur2_64(const void* __key, std::size_t __len)
{
	const std::uint64_t __m = 0xc6a4a7935bd1e995ULL;
	const int __r = 47;
	std::uint64_t __h = __len * __m;
	const auto* __data = static_cast<const std::uint8_t*>(__key);
	const std::uint8_t* __end = __data + (__len & ~static_cast<std::size_t>(7));
	while (__data != __end)
	{
		std::uint64_t __k{};
		__builtin_memcpy(&__k, __data, 8);
		__k *= __m;
		__k ^= __k >> __r;
		__k *= __m;
		__h ^= __k;
		__h *= __m;
		__data += 8;
	}
	switch (__len & 7)
	{
	case 7: __h ^= static_cast<std::uint64_t>(__data[6]) << 48; [[fallthrough]];
	case 6: __h ^= static_cast<std::uint64_t>(__data[5]) << 40; [[fallthrough]];
	case 5: __h ^= static_cast<std::uint64_t>(__data[4]) << 32; [[fallthrough]];
	case 4: __h ^= static_cast<std::uint64_t>(__data[3]) << 24; [[fallthrough]];
	case 3: __h ^= static_cast<std::uint64_t>(__data[2]) << 16; [[fallthrough]];
	case 2: __h ^= static_cast<std::uint64_t>(__data[1]) << 8;  [[fallthrough]];
	case 1: __h ^= static_cast<std::uint64_t>(__data[0]);
		__h *= __m;
	}
	__h ^= __h >> __r;
	__h *= __m;
	__h ^= __h >> __r;
	return static_cast<std::size_t>(__h);
}

}	// namespace

namespace std
{
inline namespace __1
{

__attribute__((__visibility__("default"))) __attribute__((__pure__))
std::size_t __hash_memory(const void* __ptr, std::size_t __size) noexcept
{
	return murmur2_64(__ptr, __size);
}

}	// namespace __1
}	// namespace std
