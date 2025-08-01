#!/bin/bash
gcc -o finalmouse_battery_reporter finalmouse_battery_reporter.c -lhidapi-hidraw -lpthread
mv finalmouse_battery_reporter ~/.config/waybar/finalmouse_battery_reporter

