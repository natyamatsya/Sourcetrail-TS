#include "LadybugConnection.h"

#include <unordered_map>
#include <utility>

// Kùzu (LadybugDB) public API. These headers live under
// submodules/ladybug/src/include, exposed via the External_lib_ladybug target.
#include <common/types/value/value.h>
#include <main/connection.h>
#include <main/database.h>
#include <main/query_result.h>

namespace ladybug
{
struct LadybugConnection::Impl
{
	std::unique_ptr<lbug::main::Database> database;
	std::unique_ptr<lbug::main::Connection> connection;
};

namespace
{
// Fold Kùzu's result-object error model into the expected value channel.
stdext::expected<void, Error> toResult(const std::unique_ptr<lbug::main::QueryResult>& result)
{
	if (!result)
	{
		return std::unexpected(Error{"Kùzu returned a null query result"});
	}
	if (!result->isSuccess())
	{
		return std::unexpected(result->getErrorMessage());
	}
	return {};
}

// One Param variant alternative -> an owned Kùzu Value.
std::unique_ptr<lbug::common::Value> toValue(const Param& param)
{
	return std::visit(
		[](const auto& value) {
			return std::make_unique<lbug::common::Value>(
				lbug::common::Value::createValue(value));
		},
		param);
}
}  // namespace

LadybugConnection::LadybugConnection(): m_impl(std::make_unique<Impl>()) {}

LadybugConnection::~LadybugConnection() = default;

stdext::expected<std::unique_ptr<LadybugConnection>, Error> LadybugConnection::open(
	const std::string& databaseDir) noexcept
{
	// The Database constructor and connect() throw; catch at this single
	// boundary and convert to the error channel so callers stay exception-free.
	try
	{
		auto self = std::unique_ptr<LadybugConnection>(new LadybugConnection());
		self->m_impl->database = std::make_unique<lbug::main::Database>(databaseDir);
		self->m_impl->connection =
			std::make_unique<lbug::main::Connection>(self->m_impl->database.get());
		return self;
	}
	catch (const std::exception& e)
	{
		return std::unexpected(Error{std::string{"failed to open Kùzu database: "} + e.what()});
	}
	catch (...)
	{
		return std::unexpected(Error{"failed to open Kùzu database: unknown error"});
	}
}

stdext::expected<void, Error> LadybugConnection::execute(const std::string& cypher) noexcept
{
	try
	{
		return toResult(m_impl->connection->query(cypher));
	}
	catch (const std::exception& e)
	{
		return std::unexpected(Error{e.what()});
	}
}

stdext::expected<void, Error> LadybugConnection::execute(
	const std::string& cypher, const Params& params) noexcept
{
	try
	{
		auto prepared = m_impl->connection->prepare(cypher);
		if (!prepared || !prepared->isSuccess())
		{
			return std::unexpected(Error{
				prepared ? prepared->getErrorMessage() : std::string{"prepare returned null"}});
		}

		std::unordered_map<std::string, std::unique_ptr<lbug::common::Value>> bound;
		for (const auto& [name, param]: params)
		{
			bound.emplace(name, toValue(param));
		}

		return toResult(m_impl->connection->executeWithParams(prepared.get(), std::move(bound)));
	}
	catch (const std::exception& e)
	{
		return std::unexpected(Error{e.what()});
	}
}

}  // namespace ladybug
