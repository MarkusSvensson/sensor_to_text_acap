#include <curl/curl.h>
#include <jansson.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdarg.h>
#include <glib.h>
#include <string.h>
#include <math.h>
#include <axsdk/axparameter.h>
#include <pthread.h>
#include <unistd.h>
#include "textdisplay.h"

static void parse_and_store_sensor_line(const char* line);

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) static void
panic(const char* format, ...) {
    va_list arg;
    va_start(arg, format);
    vsyslog(LOG_ERR, format, arg);
    vfprintf(stderr, format, arg);
    fprintf(stderr, "\n");
    va_end(arg);
    exit(1);
}

// Shared sensor data instance
static SensorData shared_sensor_data = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .temperature = "N/A",
    .humidity = "N/A",
    .co2 = "N/A",
    .nox = "N/A",
    .pm10 = "N/A",
    .pm25 = "N/A",
    .pm40 = "N/A",
    .pm100 = "N/A",
    .vap = "N/A",
    .voc = "N/A",
    .aqi = "N/A"
};

static size_t stream_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total_size = size * nmemb;
    GString* buffer = (GString*)userdata;

    g_string_append_len(buffer, ptr, total_size);

    while (TRUE) {
        gchar* newline = strchr(buffer->str, '\n');
        if (!newline) break;

        size_t line_len = newline - buffer->str;
        gchar* line = g_strndup(buffer->str, line_len);
        g_string_erase(buffer, 0, line_len + 1);

        g_strstrip(line);
        if (*line) {
            //fprintf(stderr, "DEBUG: Received line: '%s'\n", line);
            parse_and_store_sensor_line(line);

            pthread_mutex_lock(&shared_sensor_data.lock);
            fprintf(stderr, "DEBUG: shared_sensor_data: Temperature='%s', Humidity='%s', CO2='%s', NOx='%s'\n",
                shared_sensor_data.temperature,
                shared_sensor_data.humidity,
                shared_sensor_data.co2,
                shared_sensor_data.nox,
                shared_sensor_data.pm10,
                shared_sensor_data.pm25,
                shared_sensor_data.pm40,
                shared_sensor_data.pm100,
                shared_sensor_data.vap,
                shared_sensor_data.voc,
                shared_sensor_data.aqi);
            pthread_mutex_unlock(&shared_sensor_data.lock);
        }

        g_free(line);
    }

    return total_size;
}

static char* get_string_parameter(AXParameter* axp_handle, const char* param_name, GError** error) {
    char* param_value;
    if (!ax_parameter_get(axp_handle, param_name, &param_value, error)) {
        panic("%s", (*error)->message);
    }
    return param_value;
}

static gboolean get_boolean_parameter(AXParameter* axp_handle, const char* param_name, GError** error) {
    char* param_value = get_string_parameter(axp_handle, param_name, error);
    gboolean result = g_strcmp0(param_value, "yes") == 0;
    g_free(param_value);
    return result;
}

static int get_integer_parameter(AXParameter* axp_handle, const char* param_name, GError** error) {
    char* param_value = get_string_parameter(axp_handle, param_name, error);
    int result = atoi(param_value);
    g_free(param_value);
    return result;
}

static void parse_and_store_sensor_line(const char* line) {
    char *copy = g_strdup(line);
    char *token = strtok(copy, ",");

    pthread_mutex_lock(&shared_sensor_data.lock);

    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *k = g_strstrip(token);
            char *v = g_strstrip(eq + 1);

            // PM1.0 = 0.1, PM2.5 = 0.3, PM4.0 = 0.4, PM10.0 = 0.5, Temperature = 22.8, Humidity = 37.0, VOC = 52, NOx = 1, CO2 = 604, AQI = 3, Vaping = 0
            if (strcmp(k, "Temperature") == 0) {
                snprintf(shared_sensor_data.temperature, sizeof(shared_sensor_data.temperature), "%s°C", v);
            } else if (strcmp(k, "Humidity") == 0) {
                snprintf(shared_sensor_data.humidity, sizeof(shared_sensor_data.humidity), "%s%% RH", v);
            } else if (strcmp(k, "CO2") == 0) {
                snprintf(shared_sensor_data.co2, sizeof(shared_sensor_data.co2), "%s ppm", v);
            } else if (strcmp(k, "NOx") == 0) {
                snprintf(shared_sensor_data.nox, sizeof(shared_sensor_data.nox), "%s", v);
            } else if (strcmp(k, "PM1.0") == 0) {
                snprintf(shared_sensor_data.pm10, sizeof(shared_sensor_data.pm10), "%s µg/m³", v);
            } else if (strcmp(k, "PM2.5") == 0) {
                snprintf(shared_sensor_data.pm25, sizeof(shared_sensor_data.pm25), "%s µg/m³", v);
            } else if (strcmp(k, "PM4.0") == 0) {
                snprintf(shared_sensor_data.pm40, sizeof(shared_sensor_data.pm40), "%s µg/m³", v);
            } else if (strcmp(k, "PM10.0") == 0) {
                snprintf(shared_sensor_data.pm100, sizeof(shared_sensor_data.pm100), "%s µg/m³", v);
            } else if (strcmp(k, "Vaping") == 0) {
                if (strcmp(v, "0") == 0) {
                    snprintf(shared_sensor_data.vap, sizeof(shared_sensor_data.vap), "%s", "No");
                } else {
                    snprintf(shared_sensor_data.vap, sizeof(shared_sensor_data.vap), "%s", "Yes");
                }
            } else if (strcmp(k, "VOC") == 0) {
                snprintf(shared_sensor_data.voc, sizeof(shared_sensor_data.voc), "%s", v);
            } else if (strcmp(k, "AQI") == 0) {
                snprintf(shared_sensor_data.aqi, sizeof(shared_sensor_data.aqi), "%s", v);
            }
        }
        token = strtok(NULL, ",");
    }

    pthread_mutex_unlock(&shared_sensor_data.lock);
    g_free(copy);
}

int main(void) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("airquality", LOG_PID | LOG_CONS, LOG_USER);

    GError* error   = NULL;

    AXParameter* axp_handle = ax_parameter_new("sensor_to_text", &error);
    if (axp_handle == NULL)
        panic("%s", error->message);

    char* sensor_ip = get_string_parameter(axp_handle, "SensorIp", &error);
    char* sensor_user = get_string_parameter(axp_handle, "SensorUser", &error);
    char* sensor_password = get_string_parameter(axp_handle, "SensorPassword", &error);

    char* text_ip = get_string_parameter(axp_handle, "TextDisplayIp", &error);
    char* text_user = get_string_parameter(axp_handle, "TextDisplayUser", &error);
    char* text_password = get_string_parameter(axp_handle, "TextDisplayPassword", &error);

    gboolean show_temperature = get_boolean_parameter(axp_handle, "ShowTemperature", &error);
    gboolean show_humidity = get_boolean_parameter(axp_handle, "ShowHumidity", &error);
    gboolean show_co2 = get_boolean_parameter(axp_handle, "ShowCO2", &error);
    gboolean show_nox = get_boolean_parameter(axp_handle, "ShowNOX", &error);
    gboolean show_pm10 = get_boolean_parameter(axp_handle, "ShowPM10", &error);
    gboolean show_pm25 = get_boolean_parameter(axp_handle, "ShowPM25", &error);
    gboolean show_pm40 = get_boolean_parameter(axp_handle, "ShowPM40", &error);
    gboolean show_pm100 = get_boolean_parameter(axp_handle, "ShowPM100", &error);
    gboolean show_vap = get_boolean_parameter(axp_handle, "ShowVapingSmoking", &error);
    gboolean show_voc = get_boolean_parameter(axp_handle, "ShowVOC", &error);
    gboolean show_aqi = get_boolean_parameter(axp_handle, "ShowAQI", &error);

    int seconds_between_cycles = get_integer_parameter(axp_handle, "SecondsBetweenCycles", &error);
    int seconds_per_data = get_integer_parameter(axp_handle, "SecondsPerData", &error);

    // Prepare parameters for the display thread
    TextDisplayParams params = {
        .text_ip = text_ip,
        .text_user = text_user,
        .text_password = text_password,
        .show_temperature = show_temperature,
        .show_humidity = show_humidity,
        .show_co2 = show_co2,
        .show_nox = show_nox,
        .show_pm10 = show_pm10,
        .show_pm25 = show_pm25,
        .show_pm40 = show_pm40,
        .show_pm100 = show_pm100,
        .show_vap = show_vap,
        .show_voc = show_voc,
        .show_aqi = show_aqi,
        .seconds_between_cycles = seconds_between_cycles,
        .seconds_per_data = seconds_per_data,
        .shared_sensor_data = &shared_sensor_data
    };

    // Create a thread for textdisplay_run
    pthread_t display_thread;
    if (pthread_create(&display_thread, NULL, textdisplay_run, &params) != 0) {
        panic("Failed to create display thread");
    }

    // Main thread continues to read sensor data
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* handle = curl_easy_init();
    if (!handle)
        panic("Failed to initialize CURL");

    GString* stream_buffer = g_string_new(NULL);
    gchar* url = g_strdup_printf("https://%s/axis-cgi/airquality/metadata.cgi", sensor_ip);

    fprintf(stderr, "Connecting to %s with user '%s:'%s'\n", url, sensor_user, sensor_password);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_USERNAME, sensor_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, sensor_password);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, stream_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, stream_buffer);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);  // No timeout
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);


    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK)
        panic("CURL error (when connecting to sensor) %d: %s", res, curl_easy_strerror(res));

    long response_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    fprintf(stderr, "HTTP response code (when connecting to sensor): %ld\n", response_code);

    curl_easy_cleanup(handle);
    curl_global_cleanup();
    g_string_free(stream_buffer, TRUE);
    g_free(url);

    g_free(sensor_ip);
    g_free(sensor_user);
    g_free(sensor_password);
    g_free(text_ip);
    g_free(text_user);
    g_free(text_password);

    ax_parameter_free(axp_handle);

    // Wait for the display thread to finish (if needed)
    pthread_join(display_thread, NULL);

    closelog();
    return 0;
}
