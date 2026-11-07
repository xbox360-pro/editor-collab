#!/bin/sh
cd "$(dirname "$0")"
exec python3 collab_server.py "$@"
