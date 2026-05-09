#!/bin/bash
#
# MTT S30 GPU Driver Test Script (Root Wrapper)
#
# For full documentation see Driver/scripts/test.sh --help
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/Driver/scripts/test.sh" "$@"
