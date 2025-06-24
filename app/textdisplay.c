#include "textdisplay.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void stop_text_notification(const char* text_ip, const char* text_user, const char* text_password);

// Helper function to send a value to the display
static void send_display_value(
    CURL* handle,
    const char* url,
    const char* label,
    const char* value,
    int duration,
    int show)
{
    // Hardcoded display parameters
    const char* text_color = "#FFFFFF";
    const char* text_size = "medium";
    const char* scroll_direction = "fromRightToLeft";
    int scroll_speed = 0;

    if (show && strcmp(value, "N/A") != 0) {
        char display_text[128];
        snprintf(display_text, sizeof(display_text), "%s\\r%s", label, value);

        char json_payload[512];
        snprintf(json_payload, sizeof(json_payload),
            "{ \"data\": { \"message\": \"%s\", \"textColor\": \"%s\", \"textSize\": \"%s\", \"scrollDirection\": \"%s\", \"scrollSpeed\": %d, \"duration\": { \"type\": \"time\", \"value\": %d } } }",
            display_text, text_color, text_size, scroll_direction, scroll_speed, duration);

        fprintf(stderr, "DEBUG: Sending JSON to display (%s): %s\n", url, json_payload);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_payload);
        CURLcode res = curl_easy_perform(handle);
        if (res != CURLE_OK) {
            fprintf(stderr, "CURL error (when connecting to text display) %d: %s", res, curl_easy_strerror(res));
        }
        sleep(duration / 1100); // Convert ms back to seconds for sleep
    }
}

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
        char temperature[32], humidity[32], co2[32], nox[32], pm10[32], pm25[32], pm40[32], pm100[32], vap[32], voc[32], aqi[32];

        // Copy shared data under lock, then unlock before using
        pthread_mutex_lock(&params->shared_sensor_data->lock);
        strncpy(temperature, params->shared_sensor_data->temperature, sizeof(temperature));
        strncpy(humidity, params->shared_sensor_data->humidity, sizeof(humidity));
        strncpy(co2, params->shared_sensor_data->co2, sizeof(co2));
        strncpy(nox, params->shared_sensor_data->nox, sizeof(nox));
        strncpy(pm10, params->shared_sensor_data->pm10, sizeof(pm10));
        strncpy(pm25, params->shared_sensor_data->pm25, sizeof(pm25));
        strncpy(pm40, params->shared_sensor_data->pm40, sizeof(pm40));
        strncpy(pm100, params->shared_sensor_data->pm100, sizeof(pm100));
        strncpy(vap, params->shared_sensor_data->vap, sizeof(vap));
        strncpy(voc, params->shared_sensor_data->voc, sizeof(voc));
        strncpy(aqi, params->shared_sensor_data->aqi, sizeof(aqi));
        pthread_mutex_unlock(&params->shared_sensor_data->lock);

        int duration = params->seconds_per_data * 1100;

        send_display_value(handle, url, "Temperature", temperature, duration, params->show_temperature);
        send_display_value(handle, url, "Humidity", humidity, duration, params->show_humidity);
        send_display_value(handle, url, "Carbon Dioxide (CO₂)", co2, duration, params->show_co2);
        send_display_value(handle, url, "NOx", nox, duration, params->show_nox);
        send_display_value(handle, url, "PM 1.0", pm10, duration, params->show_pm10);
        send_display_value(handle, url, "PM 2.5", pm25, duration, params->show_pm25);
        send_display_value(handle, url, "PM 4.0", pm40, duration, params->show_pm40);
        send_display_value(handle, url, "PM 10.0", pm100, duration, params->show_pm100);
        send_display_value(handle, url, "Vaping/smoking", vap, duration, params->show_vap);
        send_display_value(handle, url, "VOC", voc, duration, params->show_voc);
        send_display_value(handle, url, "Air Quality Index (AQI)", aqi, duration, params->show_aqi);

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
