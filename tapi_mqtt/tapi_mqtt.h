/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief MQTT from a test
 *
 * @defgroup tapi_mqtt MQTT (tapi_mqtt)
 * @{
 *
 * Speak MQTT to a broker: connect, publish, subscribe and collect what
 * arrives.
 *
 * @section tapi_mqtt_why What this is for
 *
 * @ref tapi_sniffrules already has an MQTT rule, and it watches
 * traffic go past. This is the other half: being a client. A rule over
 * a capture can say that a password crossed the wire in the clear; only
 * a client can find out that the broker would have let it in without
 * one.
 *
 * MQTT is worth this attention because of what it is used for. A
 * broker is the place every device in an installation talks through,
 * so a broker that lets a stranger subscribe to @c "#" hands over the
 * whole installation at once - every sensor reading, every command,
 * every device that announces itself. That is one line of
 * configuration and it is the most common serious finding there is.
 *
 * @code
 * tapi_mqtt_conn conn = tapi_mqtt_default_conn;
 * te_vec messages;
 *
 * conn.host = "dut.example.net";
 * CHECK_RC(tapi_mqtt_publish(factory, &conn, "cmd/led", "on", NULL));
 * CHECK_RC(tapi_mqtt_collect(factory, &conn, "state/#", 5, 0, 10000,
 *                            &messages));
 * tapi_mqtt_messages_free(&messages);
 * @endcode
 *
 * @section tapi_mqtt_scope What it does not do
 *
 * It talks to the broker the suite's own configuration names. It
 * publishes only where the caller told it to, and it never invents a
 * credential: tapi_mqtt_audit() will try a list of credentials if a
 * test hands it one, and ships with none.
 *
 * @note It drives @c mosquitto_pub and @c mosquitto_sub on the agent.
 */

#ifndef __TSF_TAPI_MQTT_H__
#define __TSF_TAPI_MQTT_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default timeout for one MQTT operation, ms. */
#define TAPI_MQTT_TIMEOUT_MS 30000

/** The port a broker listens on without TLS. */
#define TAPI_MQTT_PORT 1883

/** The port a broker listens on with TLS. */
#define TAPI_MQTT_TLS_PORT 8883

/** How a broker answered an attempt to connect. */
typedef enum tapi_mqtt_result {
    /** It let the client in. */
    TAPI_MQTT_ACCEPTED = 0,
    /** It refused the credentials, or the lack of them. */
    TAPI_MQTT_NOT_AUTHORISED,
    /** Nothing answered on that address and port at all. */
    TAPI_MQTT_UNREACHABLE,
    /** It answered with something else; the log has it. */
    TAPI_MQTT_REFUSED,
} tapi_mqtt_result;

/** How to reach a broker. */
typedef struct tapi_mqtt_conn {
    /** Host to connect to. Required. */
    const char *host;
    /** Port; @c 0 means @ref TAPI_MQTT_PORT, or the TLS one with @a tls. */
    uint16_t port;
    /** User name, or @c NULL to connect anonymously. */
    const char *username;
    /** Password, or @c NULL. */
    const char *password;
    /**
     * Client identifier, or @c NULL to let the client invent one.
     *
     * Worth setting deliberately in one case: two clients with the
     * same identifier cannot both be connected, and the second one in
     * takes the first one's place. A test can use that to find out
     * whether a broker lets a stranger evict a device.
     */
    const char *client_id;
    /** Connect over TLS. */
    bool tls;
    /** CA certificate on the agent to verify the broker with. */
    const char *ca_file;
    /**
     * Accept a broker certificate that does not verify.
     *
     * Off by default, deliberately. A test that turns this on to make
     * a connection work has found a defect and hidden it; report it
     * with @ref tapi_tls instead.
     */
    bool insecure;
    /** MQTT version to ask for: @c 5, @c 311 or @c 31; @c 0 for the default. */
    unsigned int version;
    /** Extra client arguments, for what this does not wrap. */
    const char **extra_args;
    /** Number of @a extra_args. */
    size_t n_extra_args;
} tapi_mqtt_conn;

/** Defaults: plain MQTT on the standard port, no credentials. */
extern const tapi_mqtt_conn tapi_mqtt_default_conn;

/** One message that arrived. */
typedef struct tapi_mqtt_message {
    /** The topic it came on. */
    char *topic;
    /** Its payload, as text. */
    char *payload;
} tapi_mqtt_message;

/**
 * Is there an MQTT client on the agent?
 *
 * @param factory       Job factory.
 * @param timeout_ms    Timeout, ms.
 *
 * @return @c true when @c mosquitto_pub answers.
 */
extern bool tapi_mqtt_available(tapi_job_factory_t *factory, int timeout_ms);

/**
 * Try to connect, and report how the broker answered.
 *
 * Nothing is published and nothing is subscribed to. This is the
 * question "would you let me in", which is the one an audit asks.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  conn         How to reach the broker.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] result       How it answered.
 *
 * @return Status code of asking, not the answer.
 */
extern te_errno tapi_mqtt_try_connect(tapi_job_factory_t *factory,
                                      const tapi_mqtt_conn *conn,
                                      int timeout_ms,
                                      tapi_mqtt_result *result);

/**
 * Publish one message.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  conn         How to reach the broker.
 * @param[in]  topic        Topic to publish on.
 * @param[in]  payload      What to send.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] result       How the broker answered, or @c NULL.
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_publish(tapi_job_factory_t *factory,
                                  const tapi_mqtt_conn *conn,
                                  const char *topic, const char *payload,
                                  int timeout_ms,
                                  tapi_mqtt_result *result);

/**
 * Publish a message the broker will keep.
 *
 * A retained message is handed to everyone who subscribes afterwards,
 * which is what makes it worth a separate function: it is the one
 * publish that outlives the test.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  conn         How to reach the broker.
 * @param[in]  topic        Topic to publish on.
 * @param[in]  payload      What to send; an empty string clears it.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] result       How the broker answered, or @c NULL.
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_publish_retained(tapi_job_factory_t *factory,
                                           const tapi_mqtt_conn *conn,
                                           const char *topic,
                                           const char *payload,
                                           int timeout_ms,
                                           tapi_mqtt_result *result);

/**
 * Subscribe and collect what arrives.
 *
 * Ends when @p count messages have arrived or @p seconds have passed,
 * whichever is first. Messages that had been retained arrive
 * immediately, before anything new.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  conn         How to reach the broker.
 * @param[in]  topic        Topic filter; @c "#" is everything.
 * @param[in]  seconds      How long to listen.
 * @param[in]  count        Stop after this many; @c 0 for no limit.
 * @param[in]  timeout_ms   Timeout for the job, ms. Give it more than
 *                          @p seconds.
 * @param[out] messages     Vector of #tapi_mqtt_message; release with
 *                          tapi_mqtt_messages_free().
 *
 * @return Status code.
 */
extern te_errno tapi_mqtt_collect(tapi_job_factory_t *factory,
                                  const tapi_mqtt_conn *conn,
                                  const char *topic, unsigned int seconds,
                                  unsigned int count, int timeout_ms,
                                  te_vec *messages);

/**
 * Find a message by topic.
 *
 * @param messages      Vector of #tapi_mqtt_message.
 * @param topic         Exact topic.
 *
 * @return The message, or @c NULL. It belongs to @p messages.
 */
extern const tapi_mqtt_message *tapi_mqtt_message_find(
                                    const te_vec *messages,
                                    const char *topic);

/**
 * Write collected messages into the log.
 *
 * @param what          A word for the log.
 * @param messages      Vector of #tapi_mqtt_message.
 */
extern void tapi_mqtt_messages_log(const char *what, const te_vec *messages);

/**
 * Release collected messages.
 *
 * @param messages      Vector of #tapi_mqtt_message.
 */
extern void tapi_mqtt_messages_free(te_vec *messages);

/**
 * Spell out a result.
 *
 * @param result        Result.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_mqtt_result2str(tapi_mqtt_result result);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_MQTT_H__ */

/**@} <!-- END tapi_mqtt --> */
