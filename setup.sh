#!/bin/bash
# Convenience wrapper — equivalent to 'make setup && make'
set -e
cd "$(dirname "$0")"
make setup
make
echo ""
echo "Done. Run 'make run' to start Ruby on Bare Metal."
