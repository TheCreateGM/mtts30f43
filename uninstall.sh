#!/bin/bash
#
# MTT S30 GPU Driver Uninstall Script (Root Wrapper)
#
# Usage: sudo ./uninstall.sh [options]
#
# For full documentation see Driver/scripts/uninstall.sh --help
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec sudo "$SCRIPT_DIR/Driver/scripts/uninstall.sh" "$@"
