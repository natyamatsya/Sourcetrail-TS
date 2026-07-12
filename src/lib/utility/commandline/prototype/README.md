# Glaze CLI parsing — proof of concept

`glaze_cli_poc.cpp` is a standalone POC (not wired into CMake) showing a tiny,
type-safe command-line parser built on **Glaze compile-time reflection** — and it
fixes the space-separated-argument bug in the app's current CLI11 usage.

## Why

`CommandLineParser::preparse` calls `m_app.parse(m_args)` (CLI11's
`std::vector<std::string>` overload) in a way that **drops the value of the first
option when a second option follows** — e.g. `sourcetrail --screenshot foo.png
--screenshot-delay 1500` fails with "`--screenshot: 1 required TEXT missing`". Only
the `--flag=value` form works. (See the diagnosis in the agent-control session.)

## What Glaze gives us

Glaze has no argv parser, but its reflection does the heavy lifting:

- `glz::meta<CliOptions>` maps struct fields → kebab-case flag names, so
  `glz::reflect<T>::keys` are the flags directly (`agent_instance` → `--agent-instance`).
- `glz::for_each<reflect<T>::size>` + `glz::get_member(obj, get<I>(reflect<T>::values))`
  iterate fields with names + typed references.
- `if constexpr` on each field type dispatches parsing (`std::string` assign,
  `std::from_chars` for arithmetic, presence for `bool`).

The result: ~90 lines, both `--flag value` and `--flag=value` work, with typed
values and clear errors (unknown option, bad number, missing value).

## Build / run (ad-hoc)

```sh
INC=.build/<preset>/vcpkg_installed/<triplet>/include
clang++ -std=c++23 -stdlib=libc++ -I"$INC" glaze_cli_poc.cpp -o glaze_cli_poc
./glaze_cli_poc      # -> ALL PASS
```

## Not a full CLI11 replacement (yet)

This covers flat `--key value` options. The app's `CommandLineParser` also has
**subcommands** (`config`/`index`/`merge`), `--help` generation, a positional
project-file argument, and validation — none of which this POC provides. Options for
adoption: (a) fix the CLI11 vector-parse call and keep CLI11; (b) use Glaze for the
top-level flags + keep CLI11 for subcommands; (c) grow this into a fuller Glaze CLI
(help text from reflection, positionals, subcommand dispatch). TBD.
