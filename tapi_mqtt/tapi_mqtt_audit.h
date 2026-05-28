/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief What a broker lets a stranger do
 *
 * @defgroup tapi_mqtt_audit Broker security posture
 * @ingroup tapi_mqtt
 * @{
 *
 * A broker is the place every device in an installation talks through.
 * That is what makes one misconfigured line so expensive: a broker
 * that lets a stranger subscribe to @c "#" hands over the whole
 * installation at once - every sensor reading, every command, every
 * device that announces itself - and it is the most common serious
 * finding there is.
 *
 * @code
 * tapi_mqtt_policy policy = tapi_mqtt_default_policy;
 * tapi_cybersec_report report;
 *
 * tapi_cybersec_report_init(&report);
 * policy.scratch_topic = "tapi/selftest";
 * CHECK_RC(tapi_mqtt_audit(factory, &conn, &policy, 30000, &report));
 * tapi_cybersec_report_log(&report);
 * @endcode
 *
 * @section tapi_mqtt_audit_manners What it does to the broker
 *
 * Reads, by default. It connects, it subscribes, and it listens. The
 * one thing that writes is the publish check, it is off unless the
 * test names a topic for it, and it publishes one short message there
 * and nowhere else.
 *
 * Credentials are never invented. tapi_mqtt_policy::credentials is
 * empty unless a test fills it, and it is meant for the handful a
 * device ships with and should not have kept - not for a word list.
 */

#ifndef __TSF_TAPI_MQTT_AUDIT_H__
#define __TSF_TAPI_MQTT_AUDIT_H__

#include "te_defs.h"
#include "te_errno.h"

#include "tapi_cybersec.h"
#include "tapi_mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One pair to try. */
typedef struct tapi_mqtt_credential {
    /** User name. */
    const char *username;
    /** Password. */
    const char *password;
} tapi_mqtt_credential;

/** What the broker is expected to be. */
typedef struct tapi_mqtt_policy {
    /** A client with no credentials may connect. */
    bool allow_anonymous;
    /** The broker may accept a connection without TLS. */
    bool allow_plaintext;
    /** A client may subscribe to everything. */
    bool allow_wildcard;
    /** @c $SYS may be readable. */
    bool allow_sys;
    /**
     * Topic to publish one message to, or @c NULL to not publish.
     *
     * Off unless a test names one, because this is the only check here
     * that writes anything. Name a topic the installation does not
     * use.
     */
    const char *scratch_topic;
    /**
     * Credentials to try, or @c NULL.
     *
     * For the handful a device ships with and should not have kept.
     * Nothing is built in.
     */
    const tapi_mqtt_credential *credentials;
    /** Number of @a credentials. */
    size_t n_credentials;
    /** How long to listen when checking what a subscription returns. */
    unsigned int listen_seconds;
} tapi_mqtt_policy;

/** The default: nothing unauthenticated is allowed, nothing is published. */
extern const tapi_mqtt_policy tapi_mqtt_default_policy;

/**
 * Find out what the broker lets a stranger do.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  conn         How to reach the broker. Leave
 *                          @a username empty to ask as a stranger,
 *                          which is the question this is for.
 * @param[in]  policy       What is expected, or @c NULL for the
 *                          default.
 * @param[in]  timeout_ms   Timeout for each step, ms.
 * @param[out] report       Report to append findings to.
 *
 * @return Status code of asking, not the verdict.
 * @retval TE_ECONNREFUSED  Nothing answered at all.
 */
extern te_errno tapi_mqtt_audit(tapi_job_factory_t *factory,
                                const tapi_mqtt_conn *conn,
                                const tapi_mqtt_policy *policy,
                                int timeout_ms,
                                tapi_cybersec_report *report);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_MQTT_AUDIT_H__ */

/**@} <!-- END tapi_mqtt_audit --> */
