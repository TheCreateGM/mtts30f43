#!/bin/bash
#
# MTT S30 GPU Driver Build Script (Root Wrapper)
#
# This wrapper delegates to Driver/scripts/build.sh
# For full documentation see Driver/scripts/build.sh --help
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/Driver/scripts/build.sh" "$@"
