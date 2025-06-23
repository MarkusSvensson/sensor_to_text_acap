#include "textdisplay.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void stop_text_notification(const char* text_ip, const char* text_user, const char* text_password) {
    CURL* handle = curl_easy_init();
    if (!handle) return;

    char url[256];
    snprintf(url, sizeof(url), "http://%s/config/rest/speaker-display-notification/v1/stop", text_ip);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_USERNAME, text_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, text_password);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "{\"data\": {}}");
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_slist_append(NULL, "Content-Type: application/json"));

    curl_easy_perform(handle);

    curl_easy_cleanup(handle);
}

void* textdisplay_run(void* args) {
    TextDisplayParams* params = (TextDisplayParams*)args;
    // Hardcoded display parameters
    const char* text_color = "#FFFFFF";
    const char* text_size = "medium";
    const char* scroll_direction = "fromRightToLeft";
    int scroll_speed = 0;

    CURL* handle = curl_easy_init();
    if (!handle) return NULL;

    char url[256];
    snprintf(url, sizeof(url), "https://%s/config/rest/speaker-display-notification/v1/simple", params->text_ip);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_USERNAME, params->text_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, params->text_password);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

    while (1) {
        char display_text[128];
        char temperature[32], humidity[32], co2[32], nox[32];

        // Copy shared data under lock, then unlock before using
        pthread_mutex_lock(&params->shared_sensor_data->lock);
        strncpy(temperature, params->shared_sensor_data->temperature, sizeof(temperature));
        strncpy(humidity, params->shared_sensor_data->humidity, sizeof(humidity));
        strncpy(co2, params->shared_sensor_data->co2, sizeof(co2));
        strncpy(nox, params->shared_sensor_data->nox, sizeof(nox));
        pthread_mutex_unlock(&params->shared_sensor_data->lock);
        //fprintf(stderr, "DEBUG: Using values from shared_sensor_data: Temperature='%s', Humidity='%s', CO2='%s', NOx='%s'\n", temperature, humidity, co2, nox);

        if (params->show_temperature && strcmp(temperature, "N/A") != 0) {
            snprintf(display_text, sizeof(display_text), "Temperature: %s", temperature);
            char json_payload[512];
            snprintf(json_payload, sizeof(json_payload),
                "{ \"data\": { \"message\": \"%s\", \"textColor\": \"%s\", \"textSize\": \"%s\", \"scrollDirection\": \"%s\", \"scrollSpeed\": %d, \"duration\": { \"type\": \"time\", \"value\": %d } } }",
                display_text, text_color, text_size, scroll_direction, scroll_speed, params->seconds_per_data);
            fprintf(stderr, "DEBUG: Sending JSON to display (%s): %s\n", url, json_payload);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {fprintf(stderr, "CURL error (when connecting to text display) %d: %s", res, curl_easy_strerror(res));}  
            sleep(params->seconds_per_data);
        }

        if (params->show_humidity && strcmp(humidity, "N/A") != 0) {
            snprintf(display_text, sizeof(display_text), "Humidity: %s", humidity);
            char json_payload[512];
            snprintf(json_payload, sizeof(json_payload),
                "{ \"data\": { \"message\": \"%s\", \"textColor\": \"%s\", \"textSize\": \"%s\", \"scrollDirection\": \"%s\", \"scrollSpeed\": %d, \"duration\": { \"type\": \"time\", \"value\": %d } } }",
                display_text, text_color, text_size, scroll_direction, scroll_speed, params->seconds_per_data);
            fprintf(stderr, "DEBUG: Sending JSON to display (%s): %s\n", url, json_payload);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {fprintf(stderr, "CURL error (when connecting to text display) %d: %s", res, curl_easy_strerror(res));}               
            sleep(params->seconds_per_data);
        }

        if (params->show_co2 && strcmp(co2, "N/A") != 0) {
            snprintf(display_text, sizeof(display_text), "CO2: %s", co2);
            char json_payload[512];
            snprintf(json_payload, sizeof(json_payload),
                "{ \"data\": { \"message\": \"%s\", \"textColor\": \"%s\", \"textSize\": \"%s\", \"scrollDirection\": \"%s\", \"scrollSpeed\": %d, \"duration\": { \"type\": \"time\", \"value\": %d } } }",
                display_text, text_color, text_size, scroll_direction, scroll_speed, params->seconds_per_data);
            fprintf(stderr, "DEBUG: Sending JSON to display (%s): %s\n", url, json_payload);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {fprintf(stderr, "CURL error (when connecting to text display) %d: %s", res, curl_easy_strerror(res));}
            sleep(params->seconds_per_data);
        }

        if (params->show_nox && strcmp(nox, "N/A") != 0) {
            snprintf(display_text, sizeof(display_text), "NOx: %s", nox);
            char json_payload[512];
            snprintf(json_payload, sizeof(json_payload),
                "{ \"data\": { \"message\": \"%s\", \"textColor\": \"%s\", \"textSize\": \"%s\", \"scrollDirection\": \"%s\", \"scrollSpeed\": %d, \"duration\": { \"type\": \"time\", \"value\": %d } } }",
                display_text, text_color, text_size, scroll_direction, scroll_speed, params->seconds_per_data);
            fprintf(stderr, "DEBUG: Sending JSON to display (%s): %s\n", url, json_payload);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {fprintf(stderr, "CURL error (when connecting to text display) %d: %s", res, curl_easy_strerror(res));} 
            sleep(params->seconds_per_data);
        }

        stop_text_notification(params->text_ip, params->text_user, params->text_password);
        if (strcmp(temperature, "N/A") == 0){
            sleep(5); // Wait for 5 seconds if no data is available
        } else {
            sleep(params->seconds_between_cycles);
        }
        
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return NULL;
}
