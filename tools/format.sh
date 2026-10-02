#!/bin/bash
# Format the C sources with clang-format 18 and the repository's .clang-format.
#
#     format.sh           rewrite the files in place
#     format.sh --check   name the files that differ and exit 1; CI runs this
set -euo pipefail

cd "$(dirname "$0")/.."
clang_format=${CLANG_FORMAT:-clang-format-18}

# Tracked and new files alike; _build/ and other ignored paths are left out.
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- '*.c' '*.h')

if [ "${1:-}" = --check ]; then
    "$clang_format" --dry-run --Werror "${files[@]}"
else
    "$clang_format" -i "${files[@]}"
fi
