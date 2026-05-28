/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief A broker that is wrong on purpose
 *
 * @defgroup tapi_mqtt_broker A broker with chosen defects
 * @ingroup tapi_mqtt
 * @{
 *
 * A @c mosquitto on the agent configured exactly as the test says.
 * A check that reports nothing looks the same whether the broker is
 * clean or the check is broken, and this is what tells the two apart.
 *
 * @code
 * tapi_mqtt_broker_opt opt = tapi_mqtt_broker_default_opt;
 * tapi_mqtt_broker *broker = NULL;
 *
 * opt.port = 11883;
 * opt.defects = TAPI_MQTT_DEFECT_ANONYMOUS;
 *
 * CHECK_RC(tapi_mqtt_broker_start(factory, &opt, 10000, &broker));
 * ... audit it ...
 * CHECK_RC(tapi_mqtt_broker_stop(broker));
 * @endcode
 */

#ifndef __TSF_TAPI_MQTT_BROKER_H__
#define __TSF_TAPI_MQTT_BROKER_H__

#include "te_defs.h"
#include "te_errno.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Let anyone in without credentials. */
#define TAPI_MQTT_DEFECT_ANONYMOUS (1u << 0)
/** Leave a retained message on a topic, waiting for whoever connects. */
#define TAPI_MQTT_DEFECT_RETAINED  (1u << 1)

/** Everything this broker can be wrong about. */
#define TAPI_MQTT_DEFECT_ALL \
    (TAPI_MQTT_DEFECT_ANONYMOUS | TAPI_MQTT_DEFECT_RETAINED)

/** How to stand the broker up. */
typedef struct tapi_mqtt_broker_opt {
    /** Port to listen on. Mandatory. */
    uint16_t port;
    /** Which defects to have; @c 0 for one that asks who you are. */
    unsigned int defects;
    /** The @c mosquitto binary; @c NULL means @c mosquitto. */
    const char *binary;
    /** User name to accept when it is not anonymous; @c NULL for one. */
    const char *username;
    /** Its password; @c NULL for one. */
    const char *password;
} tapi_mqtt_broker_opt;

/** Defaults: authenticated, no retained messages. */
extern const tapi_mqtt_broker_opt tapi_mqtt_broker_default_opt;

/** A running broker. */
typedef struct tapi_mqtt_broker tapi_mqtt_broker;

/**
 * Start the broker and wait until it is listening.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  opt          How to stand it up.
 * @param[in]  timeout_ms   How long to give it, ms.
 * @param[out] broker       Handle; release with
 *                          tapi_mqtt_broker_stop().
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_broker_start(tapi_job_factory_t *factory,
                                       const tapi_mqtt_broker_opt *opt,
                                       int timeout_ms,
                                       tapi_mqtt_broker **broker);

/**
 * The user name the broker accepts, or @c NULL when it is anonymous.
 *
 * @param broker        Handle.
 *
 * @return The name.
 */
extern const char *tapi_mqtt_broker_username(
                                const tapi_mqtt_broker *broker);

/**
 * Its password.
 *
 * @param broker        Handle.
 *
 * @return The password.
 */
extern const char *tapi_mqtt_broker_password(
                                const tapi_mqtt_broker *broker);

/**
 * Stop the broker and release the handle.
 *
 * Safe on @c NULL and safe from a cleanup section.
 *
 * @param broker        Handle.
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_broker_stop(tapi_mqtt_broker *broker);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_MQTT_BROKER_H__ */

/**@} <!-- END tapi_mqtt_broker --> */
