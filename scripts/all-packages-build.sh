#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Dec 2024

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR"/bbb-build.sh release
"$SCRIPT_DIR"/bbb-build.sh devel
"$SCRIPT_DIR"/buildroot-build.sh
"$SCRIPT_DIR"/ubuntu-amd64-20.04-build.sh
