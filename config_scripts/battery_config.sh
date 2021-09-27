#!/bin/sh

BATTERY_DIRECTORY=$(find /sys/class/power_supply/ -mindepth 1 -maxdepth 1 -name "*bat*" | head -n 1)

echo "BATTERY_DIRECTORY='\"$BATTERY_DIRECTORY\"'"
