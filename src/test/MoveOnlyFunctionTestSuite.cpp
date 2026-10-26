#include "Catch2.hpp"

#include <memory>
#include <utility>

#include "MoveOnlyFunction.h"

// std::move_only_function where the standard library has it, the stand-in on
// libc++: the same calls behave the same with either.

TEST_CASE("move-only function holds a callable that cannot be copied")
{
	auto value = std::make_unique<int>(41);
	execution::MoveOnlyFunction<int(int)> add([value = std::move(value)](int n) { return *value + n; });

	REQUIRE(static_cast<bool>(add));
	REQUIRE(add(1) == 42);
}

TEST_CASE("move-only function is empty when default-built or built from nullptr")
{
	execution::MoveOnlyFunction<void()> none;
	execution::MoveOnlyFunction<void()> null(nullptr);

	REQUIRE_FALSE(static_cast<bool>(none));
	REQUIRE_FALSE(static_cast<bool>(null));
}

TEST_CASE("move-only function moves its callable, and calls change its state")
{
	execution::MoveOnlyFunction<int()> count([n = 0]() mutable { return ++n; });
	REQUIRE(count() == 1);

	execution::MoveOnlyFunction<int()> moved = std::move(count);
	REQUIRE(moved() == 2);
}
