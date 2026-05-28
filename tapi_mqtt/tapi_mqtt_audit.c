/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief What a broker lets a stranger do
 */

#define TE_LGR_USER "TAPI MQTT"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_mqtt_audit.h"
#include "tapi_mqtt_internal.h"

/** How long a subscription listens when nothing says otherwise, s. */
#define MQTT_LISTEN_SECONDS 5

const tapi_mqtt_policy tapi_mqtt_default_policy = {
    .allow_anonymous = false,
    .allow_plaintext = false,
    .allow_wildcard = false,
    .allow_sys = false,
    .scratch_topic = NULL,
    .credentials = NULL,
    .n_credentials = 0,
    .listen_seconds = MQTT_LISTEN_SECONDS,
};

/** The broker, named the way a finding should name it. */
static void
mqtt_subject(const tapi_mqtt_conn *conn, te_string *dest)
{
    uint16_t port = conn->port;

    if (port == 0)
        port = conn->tls ? TAPI_MQTT_TLS_PORT : TAPI_MQTT_PORT;

    te_string_append(dest, "%s:%u", conn->host, port);
}

/** Can a stranger get in at all? */
static te_errno
mqtt_check_anonymous(tapi_job_factory_t *factory,
                     const tapi_mqtt_conn *conn,
                     const tapi_mqtt_policy *policy, const char *subject,
                     int timeout_ms, tapi_cybersec_report *report,
                     bool *anonymous)
{
    tapi_mqtt_conn anon = *conn;
    tapi_mqtt_result result;
    te_errno rc;

    anon.username = NULL;
    anon.password = NULL;

    rc = tapi_mqtt_try_connect(factory, &anon, timeout_ms, &result);
    if (rc != 0)
        return rc;

    if (result == TAPI_MQTT_UNREACHABLE)
    {
        ERROR("Nothing answered at %s", subject);
        return TE_RC(TE_TAPI, TE_ECONNREFUSED);
    }

    *anonymous = result == TAPI_MQTT_ACCEPTED;

    if (*anonymous && !policy->allow_anonymous)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_HIGH,
            "mqtt.anonymous-allowed", subject,
            "The broker accepts a client with no credentials. Everything "
            "below was found as that client.");
    }

    if (*anonymous && !conn->tls && !policy->allow_plaintext)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_HIGH,
            "mqtt.no-tls", subject,
            "The broker accepts a connection without TLS, so every "
            "message and every password on it crosses the network in "
            "the clear.");
    }

    return 0;
}

/** What does a subscription to everything return? */
static void
mqtt_check_wildcard(tapi_job_factory_t *factory,
                    const tapi_mqtt_conn *conn,
                    const tapi_mqtt_policy *policy, const char *subject,
                    int timeout_ms, tapi_cybersec_report *report)
{
    te_vec messages;
    unsigned int seconds = policy->listen_seconds != 0 ?
                           policy->listen_seconds : MQTT_LISTEN_SECONDS;
    size_t retained = 0;
    size_t i;

    if (tapi_mqtt_collect(factory, conn, "#", seconds, 0, timeout_ms,
                          &messages) != 0)
    {
        return;
    }

    tapi_mqtt_messages_log("a stranger could read", &messages);

    if (te_vec_size(&messages) == 0)
    {
        /*
         * Nothing arrived, which is not the same as not being allowed:
         * the subscription may have been accepted and the installation
         * simply quiet. Said, because a silent report here would read
         * as "nothing to see".
         */
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_INFO,
            "mqtt.wildcard-quiet", subject,
            "A subscription to '#' was accepted and nothing arrived in "
            "%u s. Whether that is a policy or an idle installation was "
            "not established.", seconds);
        tapi_mqtt_messages_free(&messages);
        return;
    }

    /*
     * Anything that arrives in the first moments of a subscription was
     * retained by the broker, which is worse than live traffic: it is
     * there for whoever connects next, and it is usually the state of
     * every device in the installation.
     */
    for (i = 0; i < te_vec_size(&messages); i++)
    {
        const tapi_mqtt_message *message = te_vec_get(&messages, i);

        if (strncmp(message->topic, "$SYS/", 5) != 0)
            retained++;
    }

    if (!policy->allow_wildcard)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_CRITICAL,
            "mqtt.wildcard-subscribe", subject,
            "A subscription to '#' returned %zu messages. That is every "
            "topic on the broker - every reading, every command, every "
            "device that announces itself - to a client that was not "
            "asked who it was.", te_vec_size(&messages));
    }

    if (retained != 0 && !policy->allow_wildcard)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_MEDIUM,
            "mqtt.retained-readable", subject,
            "%zu of those were waiting on the broker before the "
            "subscription began. A retained message is handed to "
            "whoever connects next, so it outlives the device that "
            "sent it.", retained);
    }

    tapi_mqtt_messages_free(&messages);
}

/** Is the broker's own bookkeeping readable? */
static void
mqtt_check_sys(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
               const tapi_mqtt_policy *policy, const char *subject,
               int timeout_ms, tapi_cybersec_report *report)
{
    te_vec messages;

    if (policy->allow_sys)
        return;

    if (tapi_mqtt_collect(factory, conn, "$SYS/#", 3, 0, timeout_ms,
                          &messages) != 0)
    {
        return;
    }

    if (te_vec_size(&messages) != 0)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_MEDIUM,
            "mqtt.sys-readable", subject,
            "$SYS is readable: %zu topics of the broker's own "
            "bookkeeping - its version, how many clients are connected, "
            "how long it has been up.", te_vec_size(&messages));

        tapi_mqtt_messages_log("$SYS gave up", &messages);
    }

    tapi_mqtt_messages_free(&messages);
}

/** Can a stranger write? */
static void
mqtt_check_publish(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
                   const tapi_mqtt_policy *policy, const char *subject,
                   int timeout_ms, tapi_cybersec_report *report)
{
    tapi_mqtt_result result;

    if (policy->scratch_topic == NULL)
        return;

    /*
     * One short message, to the topic the test named and nowhere else.
     * Not retained: a retained probe would outlive the test, which is
     * the one thing an audit must not leave behind.
     */
    if (tapi_mqtt_publish(factory, conn, policy->scratch_topic,
                          "tapi-mqtt-audit", timeout_ms, &result) != 0)
    {
        return;
    }

    if (result == TAPI_MQTT_ACCEPTED)
    {
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_HIGH,
            "mqtt.publish-allowed", subject,
            "A client that was not asked who it was published to '%s'. "
            "On a broker that carries commands, writing is worth more "
            "than reading.", policy->scratch_topic);
    }
}

/** Do any of the credentials the test handed over still work? */
static void
mqtt_check_credentials(tapi_job_factory_t *factory,
                       const tapi_mqtt_conn *conn,
                       const tapi_mqtt_policy *policy, const char *subject,
                       int timeout_ms, tapi_cybersec_report *report)
{
    size_t i;

    for (i = 0; i < policy->n_credentials; i++)
    {
        tapi_mqtt_conn attempt = *conn;
        tapi_mqtt_result result;

        attempt.username = policy->credentials[i].username;
        attempt.password = policy->credentials[i].password;

        if (tapi_mqtt_try_connect(factory, &attempt, timeout_ms,
                                  &result) != 0)
        {
            continue;
        }

        if (result == TAPI_MQTT_ACCEPTED)
        {
            /*
             * The subject is the user name, so two that work are two
             * findings and the verdict names which. The password is
             * not in it: a finding has to be matchable tomorrow and
             * readable in a report that may be passed on.
             */
            tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_HIGH,
                "mqtt.known-credential", policy->credentials[i].username,
                "The broker at %s accepted the credentials for '%s' that "
                "this test was told to check for.", subject,
                policy->credentials[i].username);
        }
    }
}

/* See description in tapi_mqtt_audit.h */
te_errno
tapi_mqtt_audit(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
                const tapi_mqtt_policy *policy, int timeout_ms,
                tapi_cybersec_report *report)
{
    te_string subject = TE_STRING_INIT;
    bool anonymous = false;
    te_errno rc;

    if (policy == NULL)
        policy = &tapi_mqtt_default_policy;

    if (conn->host == NULL)
    {
        ERROR("There is no broker to audit");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    mqtt_subject(conn, &subject);

    rc = mqtt_check_anonymous(factory, conn, policy,
                              te_string_value(&subject), timeout_ms,
                              report, &anonymous);
    if (rc != 0)
    {
        te_string_free(&subject);
        return rc;
    }

    if (!anonymous && conn->username == NULL)
    {
        /*
         * The broker asked who we were and we had nothing to say, so
         * everything else here would have been refused for that reason
         * and not for a good one. Reported rather than left as an
         * absence of findings, which would read as a clean broker.
         */
        tapi_cybersec_report_add(report, TAPI_CYBERSEC_SEV_INFO,
            "mqtt.not-assessed", te_string_value(&subject),
            "The broker refused a client with no credentials, so what it "
            "allows an authorised one to do was not established. Give "
            "the audit a user name to find that out.");
        te_string_free(&subject);
        return 0;
    }

    mqtt_check_wildcard(factory, conn, policy, te_string_value(&subject),
                        timeout_ms, report);
    mqtt_check_sys(factory, conn, policy, te_string_value(&subject),
                   timeout_ms, report);
    mqtt_check_publish(factory, conn, policy, te_string_value(&subject),
                       timeout_ms, report);
    mqtt_check_credentials(factory, conn, policy,
                           te_string_value(&subject), timeout_ms, report);

    te_string_free(&subject);

    return 0;
}
