// POC: CLI parsing via Glaze compile-time reflection.
// Glaze has no argv parser, but reflect<T>::keys/values + glz::meta (kebab flag
// names) give us a tiny type-safe `--flag value` / `--flag=value` parser — and it
// fixes the space-separated bug the app's CLI11 usage has.
#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

struct CliOptions
{
	std::string screenshot;
	int screenshot_delay = 2000;
	std::string ui_snapshot;
	std::string ui_snapshot_format = "accessibility";
	std::string agent_instance;
	bool version = false;
};

// Map struct fields -> kebab-case CLI flag names (so reflect<T>::keys are the flags).
template <>
struct glz::meta<CliOptions>
{
	using T = CliOptions;
	static constexpr auto value = object(
		"screenshot", &T::screenshot,
		"screenshot-delay", &T::screenshot_delay,
		"ui-snapshot", &T::ui_snapshot,
		"ui-snapshot-format", &T::ui_snapshot_format,
		"agent-instance", &T::agent_instance,
		"version", &T::version);
};

template <class T>
std::optional<std::string> parse_args(T& opts, const std::vector<std::string>& args)
{
	constexpr auto N = glz::reflect<T>::size;
	for (std::size_t i = 0; i < args.size(); ++i)
	{
		std::string_view a = args[i];
		if (!a.starts_with("--"))
		{
			return "unexpected positional argument: " + std::string(a);
		}
		std::string_view name = a.substr(2);
		std::optional<std::string_view> inlineValue;
		if (const auto eq = name.find('='); eq != std::string_view::npos)
		{
			inlineValue = name.substr(eq + 1);
			name = name.substr(0, eq);
		}

		bool matched = false;
		std::optional<std::string> error;
		glz::for_each<N>([&]<auto I>() {
			if (matched)
			{
				return;
			}
			constexpr std::string_view key = glz::reflect<T>::keys[I];
			if (key != name)
			{
				return;
			}
			matched = true;
			decltype(auto) member = glz::get_member(opts, glz::get<I>(glz::reflect<T>::values));
			using M = std::remove_cvref_t<decltype(member)>;
			if constexpr (std::is_same_v<M, bool>)
			{
				member = true;	// presence flag
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
				if constexpr (std::is_same_v<M, std::string>)
				{
					member = std::string(value);
				}
				else if constexpr (std::is_arithmetic_v<M>)
				{
					const auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), member);
					if (ec != std::errc{})
					{
						error = "option --" + std::string(name) + ": invalid number '" + std::string(value) + "'";
					}
				}
			}
		});
		if (error)
		{
			return error;
		}
		if (!matched)
		{
			return "unknown option: --" + std::string(name);
		}
	}
	return std::nullopt;
}

static int failures = 0;
void expect(bool cond, const std::string& msg)
{
	std::cout << (cond ? "  ok   " : "  FAIL ") << msg << "\n";
	if (!cond) ++failures;
}

int main()
{
	{  // space-separated (the form CLI11 breaks on)
		CliOptions o;
		auto err = parse_args(o, {"--screenshot", "shot.png", "--screenshot-delay", "1500"});
		expect(!err, "space-separated parses without error");
		expect(o.screenshot == "shot.png", "screenshot == shot.png (got '" + o.screenshot + "')");
		expect(o.screenshot_delay == 1500, "screenshot_delay == 1500");
	}
	{  // = form
		CliOptions o;
		auto err = parse_args(o, {"--ui-snapshot=tree.fb", "--ui-snapshot-format=object", "--agent-instance=main-abc"});
		expect(!err, "= form parses");
		expect(o.ui_snapshot == "tree.fb" && o.ui_snapshot_format == "object" && o.agent_instance == "main-abc", "= values assigned");
	}
	{  // bool flag + defaults preserved
		CliOptions o;
		auto err = parse_args(o, {"--version"});
		expect(!err && o.version && o.screenshot_delay == 2000, "bool flag set; default delay preserved");
	}
	{  // errors
		CliOptions o;
		expect(parse_args(o, {"--nope"}).has_value(), "unknown option -> error");
		expect(parse_args(o, {"--screenshot-delay", "abc"}).has_value(), "bad number -> error");
		expect(parse_args(o, {"--screenshot"}).has_value(), "missing value -> error");
	}
	std::cout << (failures ? "\nFAILURES: " + std::to_string(failures) + "\n" : "\nALL PASS\n");
	return failures ? 1 : 0;
}
