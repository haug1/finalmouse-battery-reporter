#include <errno.h>
#include <hidapi/hidapi.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

// Sends a command to the mouse requesting it to report battery status on the specified interval.
static const int BATTERY_POLL_INTERVAL = 60000;

// Relative path under $HOME when FMBR_OUTPUT_FILE is unset.
static const char* BATTERY_OUTPUT_FILEPATH_DEFAULT = ".cache/finalmouse/battery";

static volatile sig_atomic_t keep_running = 1;

typedef enum {
    OUTPUT_FORMAT_TEXT,
    OUTPUT_FORMAT_JSON,
    OUTPUT_FORMAT_RAW,
} output_format_t;

typedef struct {
    const char* unicode_icon;
    const char* icon_name;
    const char* color;
    const char* level;
} battery_output_style_t;

const char* resolved_output_filepath() {
    const char* override = getenv("FMBR_OUTPUT_FILE");
    if (override && override[0] != '\0') {
        return override;
    }

    static char resolved[PATH_MAX];
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(resolved, sizeof(resolved), "%s/%s", home, BATTERY_OUTPUT_FILEPATH_DEFAULT);
    } else {
        snprintf(resolved, sizeof(resolved), "/tmp/finalmouse-battery");
    }
    return resolved;
}

int ensure_parent_dir_exists(const char* filepath) {
    char path[PATH_MAX];
    size_t len = strlen(filepath);
    if (len == 0 || len >= sizeof(path)) {
        return -1;
    }

    snprintf(path, sizeof(path), "%s", filepath);
    char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        return 0;
    }
    *last_slash = '\0';

    for (char* p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

void handle_signal(int signum) {
    (void)signum;
    keep_running = 0;
}

output_format_t resolved_output_format() {
    const char* format = getenv("FMBR_OUTPUT_FORMAT");
    if (format && strcmp(format, "text") == 0) {
        return OUTPUT_FORMAT_TEXT;
    }
    if (format && strcmp(format, "raw") == 0) {
        return OUTPUT_FORMAT_RAW;
    }

    return OUTPUT_FORMAT_JSON;
}

int voltage_to_percent(int millivolts) {
    float voltage = millivolts / 1000.0f;
    float voltage_map[] = {3.0, 3.62, 3.66, 3.74, 3.88, 4.17, 4.38};
    float percent_map[] = {0.2f, 5, 10, 25, 50, 75, 100};
    int length = sizeof(percent_map) / sizeof(float);

    if (voltage > 4.38) return 100;
    if (voltage < 3.0) return 0;

    for (int i = 0; i < length - 1; i++) {
        if (voltage >= voltage_map[i] && voltage <= voltage_map[i + 1]) {
            float progress = (voltage - voltage_map[i]) / (voltage_map[i + 1] - voltage_map[i]);
            float percent = percent_map[i] + (percent_map[i + 1] - percent_map[i]) * progress;
            return (int)(percent + 0.5f);
        }
    }

    return 100;
}

battery_output_style_t battery_percent_to_style(int percent) {
    if (percent <= 5) {
        return (battery_output_style_t){
            .unicode_icon = "",
            .icon_name = "battery",
            .color = "#ff3b30",
            .level = "critical",
        };
    } else if (percent <= 30) {
        return (battery_output_style_t){
            .unicode_icon = "",
            .icon_name = "battery-1",
            .color = "#ff9500",
            .level = "low",
        };
    } else if (percent <= 60) {
        return (battery_output_style_t){
            .unicode_icon = "",
            .icon_name = "battery-2",
            .color = "#ffd60a",
            .level = "medium",
        };
    } else if (percent <= 80) {
        return (battery_output_style_t){
            .unicode_icon = "",
            .icon_name = "battery-3",
            .color = "#32d74b",
            .level = "high",
        };
    } else {
        return (battery_output_style_t){
            .unicode_icon = "",
            .icon_name = "battery-4",
            .color = "#30d158",
            .level = "full",
        };
    }
}

void battery_percent_to_output_text(int percent, char* buffer, size_t buffer_size) {
    battery_output_style_t style = battery_percent_to_style(percent);
    snprintf(buffer, buffer_size, "%d%% %s\n\n%s", percent, style.unicode_icon, style.level);
}

void battery_percent_to_output_json(int percent, char* buffer, size_t buffer_size, int use_icon_names) {
    battery_output_style_t style = battery_percent_to_style(percent);
    snprintf(
        buffer,
        buffer_size,
        "{\"text\":\"%d%%\",\"color\":\"%s\",\"icon\":\"%s\"}\n",
        percent,
        style.color,
        use_icon_names ? style.icon_name : style.unicode_icon
    );
}

void battery_percent_to_output_raw(int percent, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d\n", percent);
}

int write_battery_percent_to_file(int percent) {
    const char* output_path = resolved_output_filepath();
    const char* icon_mode = getenv("FMBR_ICON_FORMAT");
    int use_icon_names = !icon_mode || strcmp(icon_mode, "unicode") != 0;

    if (ensure_parent_dir_exists(output_path) != 0) {
        fprintf(stderr, "Failed to ensure output directory for %s\n", output_path);
        return -1;
    }

    FILE* fp = fopen(output_path, "w");
    if (!fp) {
        perror("Failed to open file for writing");
        return -1;
    }

    char output[160];
    output_format_t format = resolved_output_format();
    if (format == OUTPUT_FORMAT_JSON) {
        battery_percent_to_output_json(percent, output, sizeof(output), use_icon_names);
    } else if (format == OUTPUT_FORMAT_RAW) {
        battery_percent_to_output_raw(percent, output, sizeof(output));
    } else {
        battery_percent_to_output_text(percent, output, sizeof(output));
    }

    if (fprintf(fp, "%s", output) < 0) {
        perror("Failed to write to file");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int send_hid_packet(hid_device* handle, uint8_t b0, uint8_t b1, uint8_t b2) {
    unsigned char packet[65] = {0};
    packet[0] = 0x00;
    packet[1] = b0;
    packet[2] = b1;
    packet[3] = b2;

    int res = hid_write(handle, packet, sizeof(packet));
    if (res < 0) {
        fprintf(stderr, "HID write failed: %ls\n", hid_error(handle));
        return -1;
    }

    return 0;
}

int trigger_battery_status(hid_device* handle) {
    send_hid_packet(handle, 4, 2, 149);
    usleep(10);
    send_hid_packet(handle, 4, 2, 224);
    send_hid_packet(handle, 2, 2, 138);
    return 0;
}

uint64_t current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

hid_device* init_hid_device() {
    return hid_open(0x361d, 0x0100, NULL);
}

int main() {
    if (hid_init()) {
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    hid_device* handle = init_hid_device();
    if (!handle) {
        fprintf(stderr, "Unable to open device\n");
        return 1;
    }

    fprintf(stderr, "finalmouse_battery_reporter: listening for battery status reports\n");

    uint64_t last_trigger = 0;
    unsigned char buf[65];

    while (keep_running) {
        uint64_t now = current_millis();
        if (now - last_trigger >= BATTERY_POLL_INTERVAL) {
            trigger_battery_status(handle);
            last_trigger = now;
        }

        int res = hid_read_timeout(handle, buf, sizeof(buf), 500);
        if (res > 0) {
            if (buf[0] == 5 && buf[1] == 4 && buf[2] == 5) {
                int mv = (buf[5] << 8) | buf[4];
                int pct = voltage_to_percent(mv);
                write_battery_percent_to_file(pct);
            }
        } else if (res < 0) {
            fprintf(stderr, "Read error\n");
            do {
                if (!keep_running) {
                    break;
                }

                handle = init_hid_device();
                if (!handle) {
                    fprintf(stderr, "Unable to open HID device, attempt again in 10 sec\n");
                }
                sleep(10);
            } while (!handle);
        }

        usleep(1000);
    }

    hid_close(handle);
    hid_exit();
    return 0;
}
