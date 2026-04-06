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

#define JSMN_STATIC
#include "jsmn.h"

// Sends a command to the mouse requesting it to report battery status on the specified interval.
static const int BATTERY_POLL_INTERVAL = 60000;

// Relative path under $HOME when FMBR_OUTPUT_FILE is unset.
static const char* BATTERY_OUTPUT_FILEPATH_DEFAULT = ".cache/finalmouse/battery";
#define MAX_THRESHOLDS 32

static volatile sig_atomic_t keep_running = 1;

typedef enum {
    OUTPUT_FORMAT_RAW,
    OUTPUT_FORMAT_TEXT,
    OUTPUT_FORMAT_JSON,
} output_format_t;

typedef struct {
    int percentage;
    const char* icon;
    const char* color;
    const char* class_name;
    const char* tooltip;
} battery_threshold_t;

typedef struct {
    int percentage;
    char icon[64];
    char color[64];
    char class_name[64];
    char tooltip[256];
    int has_tooltip;
} battery_threshold_override_t;

typedef struct {
    int has_format;
    output_format_t format;
    battery_threshold_override_t thresholds[MAX_THRESHOLDS];
    int threshold_count;
} battery_reporter_config_t;

static const battery_threshold_t DEFAULT_THRESHOLDS[] = {
    {100, "", "#30d158", "full", NULL},
    {80, "", "#32d74b", "high", NULL},
    {60, "", "#ffd60a", "medium", NULL},
    {40, "", "#ff9500", "low", NULL},
    {20, "", "#ff3b30", "critical", NULL},
};

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
    if (format && strcmp(format, "json") == 0) {
        return OUTPUT_FORMAT_JSON;
    }
    if (format && strcmp(format, "text") == 0) {
        return OUTPUT_FORMAT_TEXT;
    }

    return OUTPUT_FORMAT_RAW;
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

int token_streq(const char* json, const jsmntok_t* token, const char* value) {
    size_t value_len = strlen(value);
    size_t token_len = (size_t)(token->end - token->start);
    return token->type == JSMN_STRING && token_len == value_len && strncmp(json + token->start, value, value_len) == 0;
}

int token_to_string(const char* json, const jsmntok_t* token, char* dest, size_t dest_size) {
    size_t token_len = (size_t)(token->end - token->start);
    if (token->type != JSMN_STRING || token_len + 1 > dest_size) {
        return -1;
    }

    memcpy(dest, json + token->start, token_len);
    dest[token_len] = '\0';
    return 0;
}

int token_to_int(const char* json, const jsmntok_t* token, int* value) {
    char buffer[32];
    size_t token_len = (size_t)(token->end - token->start);
    if (token->type != JSMN_PRIMITIVE || token_len == 0 || token_len >= sizeof(buffer)) {
        return -1;
    }

    memcpy(buffer, json + token->start, token_len);
    buffer[token_len] = '\0';
    *value = atoi(buffer);
    return 0;
}

int skip_token(const jsmntok_t* tokens, int index) {
    int next = index + 1;
    if (tokens[index].type == JSMN_OBJECT) {
        for (int i = 0; i < tokens[index].size * 2; i++) {
            next = skip_token(tokens, next);
        }
    } else if (tokens[index].type == JSMN_ARRAY) {
        for (int i = 0; i < tokens[index].size; i++) {
            next = skip_token(tokens, next);
        }
    }
    return next;
}

int read_file(const char* path, char** contents, size_t* contents_len) {
    FILE* fp = fopen(path, "rb");
    long file_size;
    char* buffer;

    if (!fp) {
        perror("Failed to open config file");
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    if (fread(buffer, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(buffer);
        fclose(fp);
        return -1;
    }
    buffer[file_size] = '\0';
    fclose(fp);

    *contents = buffer;
    *contents_len = (size_t)file_size;
    return 0;
}

int parse_threshold_object(
    const char* json,
    const jsmntok_t* tokens,
    int object_index,
    battery_threshold_override_t* threshold
) {
    int index = object_index + 1;

    threshold->percentage = 0;
    threshold->icon[0] = '\0';
    threshold->color[0] = '\0';
    threshold->class_name[0] = '\0';
    threshold->tooltip[0] = '\0';
    threshold->has_tooltip = 0;

    if (tokens[object_index].type != JSMN_OBJECT) {
        return -1;
    }

    for (int i = 0; i < tokens[object_index].size; i++) {
        const jsmntok_t* key = &tokens[index];
        const jsmntok_t* value = &tokens[index + 1];

        if (token_streq(json, key, "percentage")) {
            if (token_to_int(json, value, &threshold->percentage) != 0) {
                return -1;
            }
        } else if (token_streq(json, key, "icon")) {
            if (token_to_string(json, value, threshold->icon, sizeof(threshold->icon)) != 0) {
                return -1;
            }
        } else if (token_streq(json, key, "color")) {
            if (token_to_string(json, value, threshold->color, sizeof(threshold->color)) != 0) {
                return -1;
            }
        } else if (token_streq(json, key, "class")) {
            if (token_to_string(json, value, threshold->class_name, sizeof(threshold->class_name)) != 0) {
                return -1;
            }
        } else if (token_streq(json, key, "tooltip")) {
            if (token_to_string(json, value, threshold->tooltip, sizeof(threshold->tooltip)) != 0) {
                return -1;
            }
            threshold->has_tooltip = 1;
        }

        index = skip_token(tokens, index + 1);
    }

    return threshold->icon[0] != '\0' && threshold->color[0] != '\0' && threshold->class_name[0] != '\0' ? 0 : -1;
}

int load_reporter_config(battery_reporter_config_t* config) {
    const char* path = getenv("FMBR_CONFIG_FILE");
    char* json = NULL;
    size_t json_len = 0;
    jsmn_parser parser;
    jsmntok_t tokens[256];
    int token_count;

    memset(config, 0, sizeof(*config));
    if (!path || path[0] == '\0') {
        return 0;
    }

    if (read_file(path, &json, &json_len) != 0) {
        return -1;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_len, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 1 || tokens[0].type != JSMN_OBJECT) {
        fprintf(stderr, "Failed to parse config JSON from %s\n", path);
        free(json);
        return -1;
    }

    int index = 1;
    for (int i = 0; i < tokens[0].size; i++) {
        const jsmntok_t* key = &tokens[index];
        const jsmntok_t* value = &tokens[index + 1];

        if (token_streq(json, key, "format")) {
            char format[16];
            if (token_to_string(json, value, format, sizeof(format)) != 0) {
                free(json);
                return -1;
            }
            if (strcmp(format, "json") == 0) {
                config->format = OUTPUT_FORMAT_JSON;
                config->has_format = 1;
            } else if (strcmp(format, "text") == 0) {
                config->format = OUTPUT_FORMAT_TEXT;
                config->has_format = 1;
            } else if (strcmp(format, "raw") == 0) {
                config->format = OUTPUT_FORMAT_RAW;
                config->has_format = 1;
            }
        } else if (token_streq(json, key, "thresholds")) {
            if (value->type != JSMN_ARRAY || value->size > MAX_THRESHOLDS) {
                free(json);
                return -1;
            }

            int threshold_index = index + 2;
            config->threshold_count = value->size;
            for (int j = 0; j < value->size; j++) {
                if (parse_threshold_object(json, tokens, threshold_index, &config->thresholds[j]) != 0) {
                    free(json);
                    return -1;
                }
                threshold_index = skip_token(tokens, threshold_index);
            }
        }

        index = skip_token(tokens, index + 1);
    }

    free(json);
    return 0;
}

const battery_threshold_t* resolve_default_threshold(int percent) {
    const battery_threshold_t* selected = &DEFAULT_THRESHOLDS[sizeof(DEFAULT_THRESHOLDS) / sizeof(DEFAULT_THRESHOLDS[0]) - 1];
    for (size_t i = 0; i < sizeof(DEFAULT_THRESHOLDS) / sizeof(DEFAULT_THRESHOLDS[0]); i++) {
        if (percent >= DEFAULT_THRESHOLDS[i].percentage) {
            return &DEFAULT_THRESHOLDS[i];
        }
        if (DEFAULT_THRESHOLDS[i].percentage < selected->percentage) {
            selected = &DEFAULT_THRESHOLDS[i];
        }
    }

    return selected;
}

const battery_threshold_override_t* resolve_threshold_override(
    int percent,
    const battery_threshold_override_t* thresholds,
    int threshold_count
) {
    if (threshold_count <= 0) {
        return NULL;
    }

    const battery_threshold_override_t* selected = &thresholds[0];
    const battery_threshold_override_t* best_match = NULL;
    for (int i = 0; i < threshold_count; i++) {
        if (thresholds[i].percentage < selected->percentage) {
            selected = &thresholds[i];
        }
        if (percent >= thresholds[i].percentage) {
            if (!best_match || thresholds[i].percentage > best_match->percentage) {
                best_match = &thresholds[i];
            }
        }
    }

    return best_match ? best_match : selected;
}

void threshold_to_status(
    int percent,
    const battery_threshold_override_t* override,
    const battery_threshold_t* fallback,
    char* text,
    size_t text_size,
    char* tooltip,
    size_t tooltip_size,
    const char** color,
    const char** class_name
) {
    const char* resolved_class = override ? override->class_name : fallback->class_name;
    const char* resolved_color = override ? override->color : fallback->color;

    snprintf(text, text_size, "%d%%", percent);
    if (override && override->has_tooltip) {
        snprintf(tooltip, tooltip_size, "%s", override->tooltip);
    } else if (fallback->tooltip && fallback->tooltip[0] != '\0') {
        snprintf(tooltip, tooltip_size, "%s", fallback->tooltip);
    } else {
        snprintf(tooltip, tooltip_size, "%d%%", percent);
    }

    *color = resolved_color;
    *class_name = resolved_class;
}

void json_escape_string(const char* input, char* output, size_t output_size) {
    size_t out = 0;
    for (size_t i = 0; input[i] != '\0' && out + 1 < output_size; i++) {
        const char* replacement = NULL;
        switch (input[i]) {
            case '\\':
                replacement = "\\\\";
                break;
            case '"':
                replacement = "\\\"";
                break;
            case '\n':
                replacement = "\\n";
                break;
            case '\r':
                replacement = "\\r";
                break;
            case '\t':
                replacement = "\\t";
                break;
            default:
                break;
        }

        if (replacement) {
            size_t replacement_len = strlen(replacement);
            if (out + replacement_len >= output_size) {
                break;
            }
            memcpy(output + out, replacement, replacement_len);
            out += replacement_len;
        } else {
            output[out++] = input[i];
        }
    }
    output[out] = '\0';
}

void battery_percent_to_output_raw(int percent, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d\n", percent);
}

void battery_percent_to_output_text(
    int percent,
    const battery_threshold_override_t* override,
    const battery_threshold_t* fallback,
    char* buffer,
    size_t buffer_size
) {
    char text[256];
    char tooltip[256];
    const char* color = NULL;
    const char* class_name = NULL;
    const char* icon = override ? override->icon : fallback->icon;

    threshold_to_status(percent, override, fallback, text, sizeof(text), tooltip, sizeof(tooltip), &color, &class_name);
    snprintf(buffer, buffer_size, "%s %s\n%s\n%s\n", text, icon, tooltip, class_name);
}

void battery_percent_to_output_json(
    int percent,
    const battery_threshold_override_t* override,
    const battery_threshold_t* fallback,
    char* buffer,
    size_t buffer_size
) {
    char text[256];
    char tooltip[256];
    char text_json[512];
    char tooltip_json[512];
    char icon_json[128];
    char color_json[128];
    char class_json[128];
    const char* color = NULL;
    const char* class_name = NULL;
    const char* icon = override ? override->icon : fallback->icon;

    threshold_to_status(percent, override, fallback, text, sizeof(text), tooltip, sizeof(tooltip), &color, &class_name);
    json_escape_string(text, text_json, sizeof(text_json));
    json_escape_string(tooltip, tooltip_json, sizeof(tooltip_json));
    json_escape_string(icon, icon_json, sizeof(icon_json));
    json_escape_string(color, color_json, sizeof(color_json));
    json_escape_string(class_name, class_json, sizeof(class_json));

    snprintf(
        buffer,
        buffer_size,
        "{\"text\":\"%s\",\"tooltip\":\"%s\",\"class\":\"%s\",\"percentage\":%d,\"icon\":\"%s\",\"color\":\"%s\"}\n",
        text_json,
        tooltip_json,
        class_json,
        percent,
        icon_json,
        color_json
    );
}

int write_battery_percent_to_file(int percent) {
    const char* output_path = resolved_output_filepath();
    battery_reporter_config_t config;
    if (load_reporter_config(&config) != 0) {
        return -1;
    }

    if (ensure_parent_dir_exists(output_path) != 0) {
        fprintf(stderr, "Failed to ensure output directory for %s\n", output_path);
        return -1;
    }

    FILE* fp = fopen(output_path, "w");
    if (!fp) {
        perror("Failed to open file for writing");
        return -1;
    }

    char output[1024];
    output_format_t format = config.has_format ? config.format : resolved_output_format();
    const battery_threshold_t* fallback = resolve_default_threshold(percent);
    const battery_threshold_override_t* override = resolve_threshold_override(percent, config.thresholds, config.threshold_count);

    if (format == OUTPUT_FORMAT_RAW) {
        battery_percent_to_output_raw(percent, output, sizeof(output));
    } else if (format == OUTPUT_FORMAT_TEXT) {
        battery_percent_to_output_text(percent, override, fallback, output, sizeof(output));
    } else {
        battery_percent_to_output_json(percent, override, fallback, output, sizeof(output));
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
