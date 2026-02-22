#!/bin/sh
# Wrapper used by catch_discover_tests TEST_EXECUTOR to force LC_ALL=C,
# preventing locale-formatted numbers from producing invalid JSON output.
export LC_ALL=C
exec "$@"
