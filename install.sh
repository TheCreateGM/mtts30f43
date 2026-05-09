#!/bin/bash
#
# MTT S30 GPU Driver Install Script (Root Wrapper)
#
# Usage: sudo ./install.sh [--no-dkms]
#
# For full documentation see Driver/scripts/install.sh --help
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec sudo "$SCRIPT_DIR/Driver/scripts/install.sh" "$@"
