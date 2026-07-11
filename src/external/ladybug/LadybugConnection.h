#ifndef LADYBUG_CONNECTION_H
#define LADYBUG_CONNECTION_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>

#include <stdext/expected>

namespace ladybug
{
// A thin, genuinely exception-free adapter over the Kùzu (LadybugDB) C++ API.
//
// Kùzu's own API mixes two error models: the Database constructor throws, while
// Connection::query() returns a result object you must probe with isSuccess() /
// getErrorMessage(). This adapter folds *both* into a single expected value
// channel (see ADR-0001), so callers never see an exception or a sentinel.
//
// Every public method is `noexcept`: it catches all exceptions at the boundary
// and reports failures through the expected error channel. The noexcept
// specifier is load-bearing — stdexec inspects it to route failures through the
// value channel rather than the exception channel.
//
// The Kùzu headers stay behind a pimpl, so the rest of the codebase links this
// without needing the lbug/ include paths.

using Error = std::string;

// A Cypher bind value. Kept as a std::variant so the header stays free of any
// Kùzu type; the .cpp converts each alternative to an lbug::common::Value.
using Param = std::variant<std::int64_t, double, bool, std::string>;
using Params = std::map<std::string, Param>;

class LadybugConnection
{
public:
	// Opens (creating if absent) a Kùzu database at `databaseDir` and connects.
	static stdext::expected<std::unique_ptr<LadybugConnection>, Error> open(
		const std::string& databaseDir) noexcept;

	~LadybugConnection();
	LadybugConnection(const LadybugConnection&) = delete;
	LadybugConnection& operator=(const LadybugConnection&) = delete;

	// Runs a Cypher statement (DDL or a write) whose result rows are ignored.
	stdext::expected<void, Error> execute(const std::string& cypher) noexcept;

	// Runs a parameterized Cypher statement. Parameter names in the map must
	// match the $names used in the query (without the leading '$').
	stdext::expected<void, Error> execute(
		const std::string& cypher, const Params& params) noexcept;

private:
	LadybugConnection();

	struct Impl;  // owns lbug::main::Database + Connection; defined in the .cpp
	std::unique_ptr<Impl> m_impl;
};

}  // namespace ladybug

#endif	// LADYBUG_CONNECTION_H
