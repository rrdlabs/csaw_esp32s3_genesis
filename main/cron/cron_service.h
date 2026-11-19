#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Schedule types */
typedef enum {
    CRON_KIND_EVERY = 0,   /* Recurring interval in seconds */
    CRON_KIND_AT    = 1,   /* One-shot at unix timestamp */
} cron_kind_t;

/* A single cron job */
typedef struct {
    char id[9];            /* 8-char hex ID + null */
    char name[32];
    bool enabled;
    cron_kind_t kind;
    uint32_t interval_s;   /* For EVERY: interval in seconds */
    int64_t at_epoch;      /* For AT: unix timestamp */
    char message[256];     /* Message to inject into inbound queue */
    char channel[16];      /* Reply channel (default "system") */
    char chat_id[96];      /* Reply chat_id/open_id (default "cron") */
    int64_t last_run;      /* Last run epoch */
    int64_t next_run;      /* Next run epoch */
    bool delete_after_run; /* Remove job after firing (for AT jobs) */
} cron_job_t;

/**
 * Initialize the cron service. Loads jobs from SPIFFS.
 */
esp_err_t cron_service_init(void);

/**
 * Start the cron timer. Call after WiFi is connected and time is synced.
 */
esp_err_t cron_service_start(void);

/**
 * Stop the cron timer.
 */
void cron_service_stop(void);

/**
 * Run due-job processing immediately. Used by the RTC alarm/timer service so
 * a hardware alarm can fire a cron task at the realized time.
 */
esp_err_t cron_service_process_due_now(void);

/**
 * Return the earliest future next_run across enabled jobs. Used to program
 * the PCF85063A alarm; returns ESP_ERR_NOT_FOUND when no job is pending.
 */
esp_err_t cron_service_next_due_time(time_t *out);

/**
 * Add a new cron job.
 * @param job  Pointer to job struct (id will be generated)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if max jobs reached
 */
esp_err_t cron_add_job(cron_job_t *job);

/**
 * Remove a cron job by ID.
 * @param job_id  8-char job ID
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t cron_remove_job(const char *job_id);

/**
 * List all cron jobs.
 * @param jobs      Output array of job pointers
 * @param count     Output: number of jobs
 */
void cron_list_jobs(const cron_job_t **jobs, int *count);
