# Sourcetrail Tools

## Project files

Project files are TOML (`.srctrl.toml`) only. Support for the legacy XML
`.srctrlprj` format has been removed entirely — there is no importer or
migration path. (ConfigManager still parses XML because the shipped color
schemes use it; that is unrelated to project files.)

## sqlpp23

Vendored `ddl2cpp` table generator — see `sqlpp23/ddl2cpp/`.
