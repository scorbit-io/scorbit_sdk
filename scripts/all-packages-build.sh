#!/bin/bash

# Dilshod Mukhtarov <dilshodm@gmail.com>, Dec 2024

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR"/u12-build.sh
"$SCRIPT_DIR"/u20-build.sh
"$SCRIPT_DIR"/u20-python-build.sh
