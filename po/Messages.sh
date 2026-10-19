#! /usr/bin/env bash
# Run with KDE's extract-messages tooling; output belongs to the translation catalog.
"${XGETTEXT:-xgettext}" src/*.cpp src/qml/*.qml -o "${podir:-po}/kllama.pot"
