#!/bin/bash
source .env
OUTPUT_FILE=$OUTPUT_PATH/finalmouse_battery_reporter
gcc -o finalmouse_battery_reporter src/main.c -lhidapi-hidraw -lpthread && \
mv finalmouse_battery_reporter $OUTPUT_FILE && \
echo "Successfully built binary to $OUTPUT_FILE"

