#include "radio.h"

#include <stddef.h>
#include <string.h>

static int find_station(const RadioStation* stations, int count, const char* url) {
    if (!url) return -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(stations[i].url, url) == 0) return i;
    }
    return -1;
}

int Radio_pickStation(const RadioStation* stations, int count, const char* url, bool next) {
    if (!stations || count <= 0) return -1;

    int current = find_station(stations, count, url);
    if (current < 0) return next ? 0 : count - 1;
    if (count == 1) return -1;
    return next ? (current + 1) % count : (current - 1 + count) % count;
}
