#ifndef MACHINE_ARGS_H
#define MACHINE_ARGS_H

#include <expected>
#include <format>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

// glaze reflects argument structs; MSVC cannot reflect types in an anonymous namespace.
namespace machine_args
{
// A control that takes no arguments: anything in the object is an error.
struct None
{
};
}	 // namespace machine_args

// Reads the arguments of control `event` (a JSON object; "" for none) into `Args`.
// `shape`, when given, says what was expected.
template <typename Args>
std::expected<Args, std::string> parseArgs(std::string_view event, const std::string& args, std::string_view shape = {})
{
	const std::string text = args.empty() ? std::string("{}") : args;
	Args parsed {};
	if (const auto error = glz::read_json(parsed, text))
	{
		return std::unexpected(std::format(
			"{}: {}{}", event, glz::format_error(error, text), shape.empty() ? "" : std::format(" (expected {})", shape)));
	}
	return parsed;
}

#endif	  // MACHINE_ARGS_H
