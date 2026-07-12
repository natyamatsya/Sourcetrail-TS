#ifndef GLAZE_CLI_H
#define GLAZE_CLI_H

// A tiny command-line parser built on Glaze compile-time reflection, replacing
// CLI11. An options struct describes its fields via `glz::meta` (long flag names)
// and optional static members:
//   * shorts:       array<pair<char, string_view>>  short alias -> long name
//   * positionals:  array<string_view>              field names, in order (last
//                                                    may be a vector = variadic)
//   * descriptions: array<pair<string_view,string_view>>  long name -> help text
//
// Field type => CLI shape: bool = presence flag; std::optional<T> / T (string,
// integral) = value option; std::vector<std::string> = repeatable + comma-split;
// std::optional<bool> = value option (--x true/false). Both `--opt value` and
// `--opt=value` (and `-s value` / `-s=value`) are accepted — the space-separated
// form CLI11's vector-parse dropped.

#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

namespace glzcli
{
struct ParseResult
{
	std::optional<std::string> error;
	std::vector<std::string> extras;	// unrecognized args, when allow_extras
	bool help = false;					// --help / -h / help was requested
};

namespace detail
{
template <class T>
struct is_optional : std::false_type
{
};
template <class T>
struct is_optional<std::optional<T>> : std::true_type
{
	using value_type = T;
};

template <class T>
constexpr bool is_string_vector = std::is_same_v<T, std::vector<std::string>>;

template <class Spec>
constexpr bool has_shorts = requires { Spec::shorts; };
template <class Spec>
constexpr bool has_positionals = requires { Spec::positionals; };
template <class Spec>
constexpr bool has_descriptions = requires { Spec::descriptions; };

inline void appendSplit(std::vector<std::string>& out, std::string_view value)
{
	std::size_t start = 0;
	while (start <= value.size())
	{
		const std::size_t comma = value.find(',', start);
		const std::string_view part = value.substr(
			start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
		if (!part.empty())
		{
			out.emplace_back(part);
		}
		if (comma == std::string_view::npos)
		{
			break;
		}
		start = comma + 1;
	}
}

// Assign a CLI string value into a typed field. Returns an error message on failure.
template <class M>
std::optional<std::string> assign(M& member, std::string_view value, std::string_view name)
{
	if constexpr (std::is_same_v<M, std::string>)
	{
		member = std::string(value);
	}
	else if constexpr (is_string_vector<M>)
	{
		appendSplit(member, value);
	}
	else if constexpr (is_optional<M>::value)
	{
		typename is_optional<M>::value_type inner{};
		if (auto e = assign(inner, value, name))
		{
			return e;
		}
		member = std::move(inner);
	}
	else if constexpr (std::is_same_v<M, bool>)
	{
		member = (value == "true" || value == "1" || value == "on");
	}
	else if constexpr (std::is_integral_v<M>)
	{
		const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), member);
		if (ec != std::errc{} || ptr != value.data() + value.size())
		{
			return "option --" + std::string(name) + ": invalid number '" + std::string(value) + "'";
		}
	}
	else
	{
		static_assert(sizeof(M) == 0, "unsupported CLI option type");
	}
	return std::nullopt;
}

// Resolve a short alias char to its long name (empty if none / not declared).
template <class Spec>
std::string_view longForShort(char c)
{
	if constexpr (has_shorts<Spec>)
	{
		for (const auto& [ch, name]: Spec::shorts)
		{
			if (ch == c)
			{
				return name;
			}
		}
	}
	return {};
}
}	 // namespace detail

// Parse `args` (no program name) into `spec`. Unknown options: error, or collected
// into result.extras when allow_extras.
template <class Spec>
ParseResult parse(Spec& spec, std::span<const std::string> args, bool allow_extras = false)
{
	using namespace detail;
	constexpr auto N = glz::reflect<Spec>::size;
	ParseResult result;
	std::size_t positional = 0;

	for (std::size_t i = 0; i < args.size(); ++i)
	{
		std::string_view arg = args[i];

		if (arg == "--help" || arg == "-h" || arg == "help")
		{
			result.help = true;
			return result;
		}

		// --- positional ---
		if (arg.size() < 2 || arg[0] != '-' || arg == "-")
		{
			bool placed = false;
			if constexpr (has_positionals<Spec>)
			{
				if (positional < Spec::positionals.size())
				{
					const std::string_view field = Spec::positionals[positional];
					glz::for_each<N>([&]<auto I>() {
						if (placed || glz::reflect<Spec>::keys[I] != field)
						{
							return;
						}
						decltype(auto) member = glz::get_member(spec, glz::get<I>(glz::reflect<Spec>::values));
						using M = std::remove_cvref_t<decltype(member)>;
						(void)assign(member, arg, field);
						placed = true;
						if constexpr (!is_string_vector<M>)	// variadic vectors keep consuming
						{
							++positional;
						}
					});
				}
			}
			if (!placed)
			{
				if (allow_extras)
				{
					result.extras.emplace_back(arg);
				}
				else
				{
					result.error = "unexpected argument: " + std::string(arg);
					return result;
				}
			}
			continue;
		}

		// --- option: --long[=v] or -s[=v] ---
		std::string_view name;
		std::optional<std::string_view> inlineValue;
		if (arg.starts_with("--"))
		{
			name = arg.substr(2);
			if (const auto eq = name.find('='); eq != std::string_view::npos)
			{
				inlineValue = name.substr(eq + 1);
				name = name.substr(0, eq);
			}
		}
		else	// short -s
		{
			std::string_view body = arg.substr(1);
			name = longForShort<Spec>(body[0]);
			if (body.size() > 1)
			{
				inlineValue = body[1] == '=' ? body.substr(2) : body.substr(1);
			}
		}

		bool matched = false;
		std::optional<std::string> error;
		glz::for_each<N>([&]<auto I>() {
			if (matched || glz::reflect<Spec>::keys[I] != name)
			{
				return;
			}
			matched = true;
			decltype(auto) member = glz::get_member(spec, glz::get<I>(glz::reflect<Spec>::values));
			using M = std::remove_cvref_t<decltype(member)>;
			if constexpr (std::is_same_v<M, bool>)	// presence flag
			{
				member = true;
			}
			else
			{
				std::string_view value;
				if (inlineValue)
				{
					value = *inlineValue;
				}
				else if (i + 1 < args.size())
				{
					value = args[++i];
				}
				else
				{
					error = "option --" + std::string(name) + " requires a value";
					return;
				}
				error = assign(member, value, name);
			}
		});

		if (error)
		{
			result.error = error;
			return result;
		}
		if (!matched)
		{
			if (allow_extras)
			{
				result.extras.emplace_back(arg);
				if (inlineValue == std::nullopt && i + 1 < args.size() &&
					!std::string_view(args[i + 1]).starts_with("-"))
				{
					result.extras.emplace_back(args[++i]);	// keep the option's value too
				}
			}
			else
			{
				result.error = "unknown option: --" + std::string(name);
				return result;
			}
		}
	}
	return result;
}

// Render `--long, -s   description` lines from the spec's reflection + metadata.
template <class Spec>
std::string help()
{
	using namespace detail;
	constexpr auto N = glz::reflect<Spec>::size;
	std::string out;
	glz::for_each<N>([&]<auto I>() {
		constexpr std::string_view key = glz::reflect<Spec>::keys[I];
		std::string flags = "  --" + std::string(key);
		if constexpr (has_shorts<Spec>)
		{
			for (const auto& [ch, name]: Spec::shorts)
			{
				if (name == key)
				{
					flags += ", -";
					flags += ch;
				}
			}
		}
		std::string desc;
		if constexpr (has_descriptions<Spec>)
		{
			for (const auto& [n, d]: Spec::descriptions)
			{
				if (n == key)
				{
					desc = d;
				}
			}
		}
		if (flags.size() < 34)
		{
			flags += std::string(34 - flags.size(), ' ');
		}
		else
		{
			flags += "\n" + std::string(34, ' ');
		}
		out += flags + desc + "\n";
	});
	return out;
}
}	 // namespace glzcli

#endif	  // GLAZE_CLI_H
