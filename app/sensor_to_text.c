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
#include <unistd.h>  // For sleep()


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

// Parse a line like: PM1.0 = 1.40, PM2.5 = 3.10, ...
static json_t* parse_sensor_line(const char* line) {
    //fprintf(stderr, "Parsing line: '%s'\n", line);
    json_t* root = json_object();
    int added_keys = 0;

    gchar** pairs = g_strsplit(line, ",", -1);
    for (int i = 0; pairs[i] != NULL; i++) {
        gchar* trimmed = g_strstrip(pairs[i]);
        //fprintf(stderr, "  Pair raw: '%s'\n", trimmed);

        gchar** kv = g_strsplit(trimmed, "=", 2);
        if (kv[0] && kv[1]) {
            const char* key = g_strstrip(kv[0]);
            const char* val = g_strstrip(kv[1]);

            //fprintf(stderr, "    Key: '%s', Value: '%s'\n", key, val);

            char* endptr = NULL;
            double d = strtod(val, &endptr);
            if (endptr && *endptr == '\0') {
                double rounded = floor(d * 100 + 0.5) / 100;  // Round to 2 decimal places
                json_object_set_new(root, key, json_real(rounded));
            } else {
                json_object_set_new(root, key, json_string(val));
            }
            added_keys++;
        } else {
            fprintf(stderr, "  Invalid key-value: '%s'\n", trimmed);
        }
        g_strfreev(kv);
    }
    g_strfreev(pairs);

    if (added_keys == 0)
        fprintf(stderr, "  No valid key-value pairs found in line.\n");

    return root;
}


// Structure for sensor data
typedef struct {
    pthread_mutex_t lock;  // Mutex for thread-safe access
    char temperature[32];
    char humidity[32];
    char co2[32];
    char nox[32];
} SensorData;

// Shared sensor data instance
static SensorData shared_sensor_data = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .temperature = "N/A",
    .humidity = "N/A",
    .co2 = "N/A",
    .nox = "N/A"
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
            json_t* obj = parse_sensor_line(line);

            pthread_mutex_lock(&shared_sensor_data.lock);
            const char* temperature = json_string_value(json_object_get(obj, "Temperature"));
            const char* humidity = json_string_value(json_object_get(obj, "Humidity"));
            const char* co2 = json_string_value(json_object_get(obj, "CO2"));
            const char* nox = json_string_value(json_object_get(obj, "NOx"));

            if (temperature) snprintf(shared_sensor_data.temperature, sizeof(shared_sensor_data.temperature), "%s°C", temperature);
            if (humidity) snprintf(shared_sensor_data.humidity, sizeof(shared_sensor_data.humidity), "%s%%", humidity);
            if (co2) snprintf(shared_sensor_data.co2, sizeof(shared_sensor_data.co2), "%sppm", co2);
            if (nox) snprintf(shared_sensor_data.nox, sizeof(shared_sensor_data.nox), "%sppb", nox);
            pthread_mutex_unlock(&shared_sensor_data.lock);

            json_decref(obj);
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

static void stop_text_notification(const char* text_ip, const char* text_user, const char* text_password) {
    CURL* handle = curl_easy_init();
    if (!handle) {
        panic("Failed to initialize CURL for stopping text notification");
    }

    gchar* url = g_strdup_printf("http://%s/config/rest/speaker-display-notification/v1/stop", text_ip);
    fprintf(stderr, "Stopping text notification at %s\n", url);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(handle, CURLOPT_USERNAME, text_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, text_password);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "{\"data\": {}}");
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_slist_append(NULL, "Content-Type: application/json"));

    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to stop text notification: %s\n", curl_easy_strerror(res));
    }

    g_free(url);
    curl_easy_cleanup(handle);
}

static void display_sensor_data(const char* text_ip, const char* text_user, const char* text_password, 
                                gboolean show_temperature, gboolean show_humidity, gboolean show_co2, gboolean show_nox, 
                                int seconds_between_cycles, int seconds_per_data) {
    CURL* handle = curl_easy_init();
    if (!handle) {
        panic("Failed to initialize CURL for text display");
    }

    gchar* url = g_strdup_printf("http://%s/axis-cgi/display/text.cgi", text_ip);
    fprintf(stderr, "Connecting to text display at %s with user '%s'\n", url, text_user);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(handle, CURLOPT_USERNAME, text_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, text_password);
    curl_easy_setopt(handle, CURLOPT_POST, 1L);

    while (TRUE) {
        char display_text[128];

        pthread_mutex_lock(&shared_sensor_data.lock);
        if (show_temperature) {
            snprintf(display_text, sizeof(display_text), "Temperature: %s", shared_sensor_data.temperature);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, display_text);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {
                fprintf(stderr, "Failed to display temperature: %s\n", curl_easy_strerror(res));
            }
            sleep(seconds_per_data);
        }

        if (show_humidity) {
            snprintf(display_text, sizeof(display_text), "Humidity: %s", shared_sensor_data.humidity);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, display_text);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {
                fprintf(stderr, "Failed to display humidity: %s\n", curl_easy_strerror(res));
            }
            sleep(seconds_per_data);
        }

        if (show_co2) {
            snprintf(display_text, sizeof(display_text), "CO2: %s", shared_sensor_data.co2);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, display_text);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {
                fprintf(stderr, "Failed to display CO2: %s\n", curl_easy_strerror(res));
            }
            sleep(seconds_per_data);
        }

        if (show_nox) {
            snprintf(display_text, sizeof(display_text), "NOx: %s", shared_sensor_data.nox);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, display_text);
            CURLcode res = curl_easy_perform(handle);
            if (res != CURLE_OK) {
                fprintf(stderr, "Failed to display NOx: %s\n", curl_easy_strerror(res));
            }
            sleep(seconds_per_data);
        }
        pthread_mutex_unlock(&shared_sensor_data.lock);

        stop_text_notification(text_ip, text_user, text_password);
        sleep(seconds_between_cycles);
    }

    g_free(url);
    curl_easy_cleanup(handle);
}

// Structure to pass parameters to the thread
typedef struct {
    const char* text_ip;
    const char* text_user;
    const char* text_password;
    gboolean show_temperature;
    gboolean show_humidity;
    gboolean show_co2;
    gboolean show_nox;
    int seconds_between_cycles;
    int seconds_per_data;
} DisplayParams;

void* display_sensor_data_thread(void* args) {
    DisplayParams* params = (DisplayParams*)args;

    display_sensor_data(params->text_ip, params->text_user, params->text_password,
                        params->show_temperature, params->show_humidity, params->show_co2, params->show_nox,
                        params->seconds_between_cycles, params->seconds_per_data);

    return NULL;
}

int main(void) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("airquality", LOG_PID | LOG_CONS, LOG_USER);
    
    GError* error   = NULL;

    AXParameter* axp_handle = ax_parameter_new("sensor_to_text", &error);
    if (axp_handle == NULL)
        panic("%s", error->message);

    char* sensor_ip = get_string_parameter(axp_handle, "Sensor_Ip", &error);
    char* sensor_user = get_string_parameter(axp_handle, "Sensor_User", &error);
    char* sensor_password = get_string_parameter(axp_handle, "Sensor_Password", &error);

    char* text_ip = get_string_parameter(axp_handle, "Text_Display_Ip", &error);
    char* text_user = get_string_parameter(axp_handle, "Text_Display_User", &error);
    char* text_password = get_string_parameter(axp_handle, "Text_Display_Password", &error);

    gboolean show_temperature = get_boolean_parameter(axp_handle, "Show_Temperature", &error);
    gboolean show_humidity = get_boolean_parameter(axp_handle, "Show_Humidity", &error);
    gboolean show_co2 = get_boolean_parameter(axp_handle, "Show_CO2", &error);
    gboolean show_nox = get_boolean_parameter(axp_handle, "Show_NOx", &error);

    int seconds_between_cycles = get_integer_parameter(axp_handle, "Seconds_Between_Cycles", &error);
    int seconds_per_data = get_integer_parameter(axp_handle, "Seconds_Per_Data", &error);

    // Prepare parameters for the display thread
    DisplayParams params = {
        .text_ip = text_ip,
        .text_user = text_user,
        .text_password = text_password,
        .show_temperature = show_temperature,
        .show_humidity = show_humidity,
        .show_co2 = show_co2,
        .show_nox = show_nox,
        .seconds_between_cycles = seconds_between_cycles,
        .seconds_per_data = seconds_per_data
    };

    // Create a thread for display_sensor_data
    pthread_t display_thread;
    if (pthread_create(&display_thread, NULL, display_sensor_data_thread, &params) != 0) {
        panic("Failed to create display thread");
    }

    // Main thread continues to read sensor data
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* handle = curl_easy_init();
    if (!handle)
        panic("Failed to initialize CURL");

    GString* stream_buffer = g_string_new(NULL);
    gchar* url = g_strdup_printf("http://%s/axis-cgi/airquality/metadata.cgi", sensor_ip);

    fprintf(stderr, "Connecting to %s with user '%s'\n", url, sensor_user);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(handle, CURLOPT_USERNAME, sensor_user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, sensor_password);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, stream_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, stream_buffer);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);  // No timeout
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(handle);
    if (res != CURLE_OK)
        panic("CURL error %d: %s", res, curl_easy_strerror(res));

    long response_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
    fprintf(stderr, "HTTP response code: %ld\n", response_code);

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
