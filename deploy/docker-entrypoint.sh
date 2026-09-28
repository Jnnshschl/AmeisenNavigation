#!/bin/sh
# server [config]   run the navmesh server (default, config: /etc/ameisennav/config.cfg + ANAV_* env)
# exporter [args]   run the exporter (see: exporter --help)
# anything else     executed as is
set -e

case "$1" in
    server)
        shift
        exec AmeisenNavigation.Server "${1:-/etc/ameisennav/config.cfg}"
        ;;
    exporter)
        shift
        exec AmeisenNavigation.Exporter "$@"
        ;;
    *)
        exec "$@"
        ;;
esac
