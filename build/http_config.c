/*
#include <stdio.h>
#include <string.h>

#define CONFIG_FILE "/etc/opt/axis/com.sensor_to_text/config.txt"

#include <stdlib.h>

int read_config(char* ip, size_t ip_len,
                char* user, size_t user_len,
                char* pass, size_t pass_len) {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f)
        return -1;

    if (!fgets(ip, ip_len, f) ||
        !fgets(user, user_len, f) ||
        !fgets(pass, pass_len, f)) {
        fclose(f);
        return -2;
    }

    // Remove newlines
    ip[strcspn(ip, "\r\n")] = 0;
    user[strcspn(user, "\r\n")] = 0;
    pass[strcspn(pass, "\r\n")] = 0;

    fclose(f);
    return 0;
}

*/