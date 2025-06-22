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
} SensorData;

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
    SensorData* shared_sensor_data;
} TextDisplayParams;

void* textdisplay_run(void* args);

#endif // TEXTDISPLAY_H
