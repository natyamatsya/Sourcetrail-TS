# Sourcetrail Tools

## Legacy project files (`.srctrlprj`)

The former Python XML→TOML converter has been removed: it produced a TOML
shape (`source_paths = [...]` plural-direct arrays) that ConfigManager's
serializer could not round-trip. Legacy XML project files are now migrated
by the application itself — opening a `.srctrlprj` (GUI, recent projects,
file-open, or `Sourcetrail index`) converts it to `.srctrl.toml` in place
and retires the legacy file. See
`ProjectSettings::migrateLegacyProjectFile`.

## sqlpp23

Vendored `ddl2cpp` table generator — see `sqlpp23/ddl2cpp/`.
