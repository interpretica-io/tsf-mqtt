/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief MQTT TAPI: internal helpers
 *
 * Internal to tsf-mqtt; not installed.
 */

#ifndef __TSF_TAPI_MQTT_INTERNAL_H__
#define __TSF_TAPI_MQTT_INTERNAL_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"
#include "tapi_job.h"

#include "tapi_devtool_run.h"

#include "tapi_mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What the mosquitto clients exit with.
 *
 * Measured against mosquitto 2.0.18, because none of it is obvious and
 * one of them is actively misleading.
 */
/** Connected, and did what it was asked. */
#define TAPI_MQTT_EXIT_OK 0
/** Could not reach the broker at all: "Error: Connection refused". */
#define TAPI_MQTT_EXIT_NOCONN 1
/** The broker said no: "Connection Refused: not authorised." */
#define TAPI_MQTT_EXIT_NOAUTH 5
/**
 * A subscription reached its @c -W deadline.
 *
 * Not a failure, and this is the one that catches people out: the
 * client prints every message it received and *then* exits 27. A
 * caller that reads the status first and stops there throws away the
 * data it just asked for.
 */
#define TAPI_MQTT_EXIT_TIMEOUT 27

/** Append one argument to a vector, taking ownership of it. */
extern void tapi_mqtt_arg(te_vec *args, const char *fmt, ...)
    TE_LIKE_PRINTF(2, 3);

/** Add the arguments that say how to reach the broker. */
extern void tapi_mqtt_conn_args(const tapi_mqtt_conn *conn, te_vec *args);

/**
 * Run a mosquitto client and hand back what it said.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  program      @c mosquitto_pub or @c mosquitto_sub.
 * @param[in]  args         Arguments after @c argv[0].
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] out          Standard output, or @c NULL.
 * @param[out] err          Standard error, or @c NULL.
 * @param[out] exit_code    Exit status, or @c NULL.
 *
 * @return Status code of running the client, not of the client.
 */
extern te_errno tapi_mqtt_cmd(tapi_job_factory_t *factory,
                              const char *program, const te_vec *args,
                              int timeout_ms, te_string *out,
                              te_string *err, int *exit_code);

/**
 * Start a program and leave it running.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  name         Name for log messages.
 * @param[in]  program      Program name or path.
 * @param[in]  args         Arguments after @c argv[0].
 * @param[out] run          Run handle.
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_spawn(tapi_job_factory_t *factory,
                                const char *name, const char *program,
                                const te_vec *args, tapi_devtool_run *run);

/** Turn a client's exit status into a broker's answer. */
extern tapi_mqtt_result tapi_mqtt_result_of(int exit_code);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_MQTT_INTERNAL_H__ */
