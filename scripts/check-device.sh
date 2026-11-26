#!/usr/bin/env bash
# Prints the device facts that matter for this project. Run in Termux.
prop() { command -v getprop >/dev/null 2>&1 && getprop "$1" || echo unknown; }
echo "Android version : $(prop ro.build.version.release)"
echo "SoC             : $(prop ro.soc.model)"
echo "GPU (EGL)       : $(prop ro.hardware.egl)"
echo "Architecture    : $(uname -m)"
awk '/MemTotal/ {printf "RAM             : %.1f GiB\n", $2/1048576}' /proc/meminfo
echo "Free storage    : $(df -h "$HOME" | awk 'NR==2 {print $4}')"
