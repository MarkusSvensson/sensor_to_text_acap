#ifndef TEXTDISPLAY_H
#define TEXTDISPLAY_H

#include <pthread.h>
#include <glib.h>

typedef struct {
    pthread_mutex_t lock;
    char temperature[32];
    char humidity[32];
    char co2[32];
    char nox[32];
    char pm10[32];
    char pm25[32];
    char pm40[32];
    char pm100[32];
    char vap[32];
    char voc[32];
    char aqi[32];
} SensorData;

typedef struct {
    const char* text_ip;
    const char* text_user;
    const char* text_password;
    gboolean show_temperature;
    gboolean show_humidity;
    gboolean show_co2;
    gboolean show_nox;
    gboolean show_pm10;
    gboolean show_pm25;
    gboolean show_pm40;
    gboolean show_pm100;
    gboolean show_vap;
    gboolean show_voc;
    gboolean show_aqi;
    int seconds_between_cycles;
    int seconds_per_data;
    SensorData* shared_sensor_data;
} TextDisplayParams;

void* textdisplay_run(void* args);

#endif // TEXTDISPLAY_H
