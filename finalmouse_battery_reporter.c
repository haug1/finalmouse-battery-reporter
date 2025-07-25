#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <hidapi/hidapi.h>
#include <string.h>

static const int BATTERY_POLL_INTERVAL = 60000;

int voltage_to_percent(int millivolts) {
    float voltage = millivolts / 1000.0f;
    float voltageMap[] = {3.0, 3.62, 3.66, 3.74, 3.88, 4.17, 4.38};
    float percentMap[] = {0.2f, 5, 10, 25, 50, 75, 100};
    int length = sizeof(percentMap) / sizeof(float);

    if (voltage > 4.38) return 100;
    if (voltage < 3.0) return 0;

    for (int i = 0; i < length - 1; i++) {
        if (voltage >= voltageMap[i] && voltage <= voltageMap[i + 1]) {
            float progress = (voltage - voltageMap[i]) / (voltageMap[i + 1] - voltageMap[i]);
            float percent = percentMap[i] + (percentMap[i + 1] - percentMap[i]) * progress;
            return (int)(percent + 0.5f);
        }
    }

    return 100;
}

int write_battery_percent_to_file(int percent) {
    FILE *fp = fopen("/home/main/.cache/finalmouse-hid/battery", "w");
    if (!fp) {
        perror("Failed to open file for writing");
        return -1;
    }

    fprintf(fp, "%d\n", percent);
    fclose(fp);
    return 0;
}

int send_hid_packet(hid_device *handle, uint8_t b0, uint8_t b1, uint8_t b2) {
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

int trigger_battery_status(hid_device *handle) {
    send_hid_packet(handle, 4, 2, 149);
    usleep(10);
    send_hid_packet(handle, 4, 2, 224);
    send_hid_packet(handle, 2, 2, 138);
    return 0;
}

uint64_t current_millis() {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

hid_device* init_hid_device() {
    hid_device *handle = hid_open(0x361d, 0x0100, NULL);
    return handle;
}

int main() {
    if (hid_init()) return 1;
    hid_device* handle = init_hid_device();
    if (!handle) {
        fprintf(stderr, "Unable to open device\n");
        return 1;
    }
    printf("Listening for battery status reports...\n");

    uint64_t last_trigger = 0;
    unsigned char buf[65];
    int res;

    while (1) {
        uint64_t now = current_millis();
        if (now - last_trigger >= BATTERY_POLL_INTERVAL) {
            trigger_battery_status(handle);
            last_trigger = now;
        }

        res = hid_read_timeout(handle, buf, sizeof(buf), 500);
        if (res > 0) {
            if (buf[0] == 5 && buf[1] == 4 && buf[2] == 5) {
                int mv = (buf[5] << 8) | buf[4];
                int pct = voltage_to_percent(mv);
                write_battery_percent_to_file(pct);
                printf("%d%%\n", pct);
            }
        } else if (res < 0) {
            fprintf(stderr, "Read error\n");
            do {
                handle = init_hid_device();
                if(!handle) {
                    fprintf(stderr, "Unable to open HID device, attempt again in 10 sec\n");
                }
                sleep(10);
            } while(!handle);
        }

        usleep(1000);
    }

    hid_close(handle);
    hid_exit();
    return 0;
}
