#include <curl/curl.h>
#include <jansson.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdarg.h>
#include <glib.h>
#include <string.h>
#include <math.h>
#include <axsdk/axparameter.h>
//#include <param.h>


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


static size_t stream_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total_size = size * nmemb;
    GString* buffer = (GString*)userdata;

    // Log raw received chunk
    gchar* raw_chunk = g_strndup(ptr, total_size);
    //fprintf(stderr, "Received raw chunk: '%s'\n", raw_chunk);
    g_free(raw_chunk);

    g_string_append_len(buffer, ptr, total_size);

    while (TRUE) {
        gchar* newline = strchr(buffer->str, '\n');
        if (!newline) break;

        size_t line_len = newline - buffer->str;
        gchar* line = g_strndup(buffer->str, line_len);
        g_string_erase(buffer, 0, line_len + 1);

        g_strstrip(line);
        if (*line) {
            //fprintf(stderr, "Processing line: '%s'\n", line);
            json_t* obj = parse_sensor_line(line);
            char* json_str = json_dumps(obj, JSON_COMPACT);
            fprintf(stderr, "Sensor JSON: %s\n", json_str);
            free(json_str);
            json_decref(obj);
        } else {
            fprintf(stderr, "Skipped empty/whitespace line.\n");
        }

        g_free(line);
    }

    return total_size;
}


int main(void) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("airquality", LOG_PID | LOG_CONS, LOG_USER);
    
    GError* error   = NULL;

    AXParameter* axp_handle = ax_parameter_new("sensor_to_text", &error);
    if (axp_handle == NULL)
        panic("%s", error->message);

    char* sensor_ip;
    if (!ax_parameter_get(axp_handle, "SensorIp", &sensor_ip, &error))
        panic("%s", error->message);

    char* sensor_user;
    if (!ax_parameter_get(axp_handle, "SensorUser", &sensor_user, &error))
        panic("%s", error->message);

    char* sensor_password;
    if (!ax_parameter_get(axp_handle, "SensorPassword", &sensor_password, &error))
        panic("%s", error->message);

    char* text_ip;
    if (!ax_parameter_get(axp_handle, "TextDisplayIp", &text_ip, &error))
        panic("%s", error->message);

    char* text_user;
    if (!ax_parameter_get(axp_handle, "TextDisplayUser", &text_user, &error))
        panic("%s", error->message);

    char* text_password;
    if (!ax_parameter_get(axp_handle, "TextDisplayPassword", &text_password, &error))
        panic("%s", error->message);

    char* show_temp_str;
    if (!ax_parameter_get(axp_handle, "ShowTemperature", &show_temp_str, &error))
        panic("%s", error->message);
    gboolean show_temperature = g_strcmp0(show_temp_str, "yes") == 0;

    char* show_hum_str;
    if (!ax_parameter_get(axp_handle, "ShowHumidity", &show_hum_str, &error))
        panic("%s", error->message);
    gboolean show_humidity = g_strcmp0(show_hum_str, "yes") == 0;

    char* show_co2_str;
    if (!ax_parameter_get(axp_handle, "ShowCO2", &show_co2_str, &error))
        panic("%s", error->message);
    gboolean show_co2 = g_strcmp0(show_co2_str, "yes") == 0;

    char* show_nox_str;
    if (!ax_parameter_get(axp_handle, "ShowNOx", &show_nox_str, &error))
        panic("%s", error->message);
    gboolean show_nox = g_strcmp0(show_nox_str, "yes") == 0;

    char* cycle_seconds_str;
    if (!ax_parameter_get(axp_handle, "SecondsBetweenCycles", &cycle_seconds_str, &error))
        panic("%s", error->message);
    int seconds_between_cycles = atoi(cycle_seconds_str);

    char* data_seconds_str;
    if (!ax_parameter_get(axp_handle, "SecondsPerData", &data_seconds_str, &error))
        panic("%s", error->message);
    int seconds_per_data = atoi(data_seconds_str);


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
    g_free(show_temp_str);
    g_free(show_hum_str);
    g_free(show_co2_str);
    g_free(show_nox_str);
    g_free(cycle_seconds_str);
    g_free(data_seconds_str);

    ax_parameter_free(axp_handle);

    closelog();
    return 0;
}
