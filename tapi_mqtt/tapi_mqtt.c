/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief MQTT from a test
 */

#define TE_LGR_USER "TAPI MQTT"

#include "te_config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_mqtt.h"
#include "tapi_mqtt_internal.h"

/** Arguments of a command, as a plain vector of strings. */
typedef struct mqtt_cmd_opt {
    /** Number of arguments. */
    size_t n_args;
    /** Arguments after argv[0]. */
    const char **args;
} mqtt_cmd_opt;

static const tapi_job_opt_bind mqtt_cmd_binds[] = TAPI_JOB_OPT_SET(
    TAPI_JOB_OPT_ARRAY_PTR(mqtt_cmd_opt, n_args, args,
        TAPI_JOB_OPT_CONTENT(TAPI_JOB_OPT_STRING, NULL, false))
);

const tapi_mqtt_conn tapi_mqtt_default_conn = {
    .host = NULL,
    .port = 0,
};

/* See description in tapi_mqtt.h */
const char *
tapi_mqtt_result2str(tapi_mqtt_result result)
{
    switch (result)
    {
        case TAPI_MQTT_ACCEPTED:
            return "accepted";
        case TAPI_MQTT_NOT_AUTHORISED:
            return "not authorised";
        case TAPI_MQTT_UNREACHABLE:
            return "unreachable";
        default:
            return "refused";
    }
}

/* See description in tapi_mqtt_internal.h */
void
tapi_mqtt_arg(te_vec *args, const char *fmt, ...)
{
    te_string built = TE_STRING_INIT;
    char *arg;
    va_list ap;

    va_start(ap, fmt);
    te_string_append_va(&built, fmt, ap);
    va_end(ap);

    arg = built.ptr;
    TE_VEC_APPEND(args, arg);
}

/* See description in tapi_mqtt_internal.h */
tapi_mqtt_result
tapi_mqtt_result_of(int exit_code)
{
    switch (exit_code)
    {
        case TAPI_MQTT_EXIT_OK:
        case TAPI_MQTT_EXIT_TIMEOUT:
            /*
             * A subscription that ran out of time had connected, which
             * is what this is about.
             */
            return TAPI_MQTT_ACCEPTED;

        case TAPI_MQTT_EXIT_NOAUTH:
            return TAPI_MQTT_NOT_AUTHORISED;

        case TAPI_MQTT_EXIT_NOCONN:
            return TAPI_MQTT_UNREACHABLE;

        default:
            return TAPI_MQTT_REFUSED;
    }
}

/* See description in tapi_mqtt_internal.h */
void
tapi_mqtt_conn_args(const tapi_mqtt_conn *conn, te_vec *args)
{
    uint16_t port = conn->port;
    size_t i;

    tapi_mqtt_arg(args, "-h");
    tapi_mqtt_arg(args, "%s", conn->host);

    if (port == 0)
        port = conn->tls ? TAPI_MQTT_TLS_PORT : TAPI_MQTT_PORT;

    tapi_mqtt_arg(args, "-p");
    tapi_mqtt_arg(args, "%u", port);

    if (conn->username != NULL)
    {
        tapi_mqtt_arg(args, "-u");
        tapi_mqtt_arg(args, "%s", conn->username);
    }
    if (conn->password != NULL)
    {
        /*
         * On the command line, where the agent's own process list can
         * see it. There is no way round it with these clients, and it
         * is worth knowing rather than worth hiding: the credentials a
         * test uses are test credentials.
         */
        tapi_mqtt_arg(args, "-P");
        tapi_mqtt_arg(args, "%s", conn->password);
    }
    if (conn->client_id != NULL)
    {
        tapi_mqtt_arg(args, "-i");
        tapi_mqtt_arg(args, "%s", conn->client_id);
    }

    if (conn->ca_file != NULL)
    {
        tapi_mqtt_arg(args, "--cafile");
        tapi_mqtt_arg(args, "%s", conn->ca_file);
    }
    if (conn->insecure)
        tapi_mqtt_arg(args, "--insecure");

    if (conn->version != 0)
    {
        tapi_mqtt_arg(args, "-V");
        tapi_mqtt_arg(args, "%u", conn->version);
    }

    for (i = 0; i < conn->n_extra_args; i++)
        tapi_mqtt_arg(args, "%s", conn->extra_args[i]);
}

/* See description in tapi_mqtt_internal.h */
te_errno
tapi_mqtt_cmd(tapi_job_factory_t *factory, const char *program,
              const te_vec *args, int timeout_ms, te_string *out,
              te_string *err, int *exit_code)
{
    mqtt_cmd_opt opt = {
        .n_args = te_vec_size(args),
        .args = te_vec_size(args) == 0 ? NULL :
                (const char **)te_vec_get((te_vec *)args, 0),
    };
    tapi_devtool_output output;
    tapi_devtool_run run = TAPI_DEVTOOL_RUN_INIT;
    te_errno rc;

    rc = tapi_devtool_run_init(&run, factory, "mqtt", program,
                               mqtt_cmd_binds, &opt, NULL);
    if (rc != 0)
        return rc;

    rc = tapi_devtool_run_start(&run);
    if (rc == 0)
        rc = tapi_devtool_run_wait(&run, timeout_ms);

    if (rc != 0)
    {
        tapi_devtool_run_fini(&run);
        return rc;
    }

    tapi_devtool_run_get_output(&run, &output);

    if (out != NULL && output.out != NULL)
        te_string_append(out, "%s", output.out);
    if (err != NULL && output.err != NULL)
        te_string_append(err, "%s", output.err);

    if (exit_code != NULL)
    {
        *exit_code = output.status.type == TAPI_JOB_STATUS_EXITED ?
                     output.status.value : -1;
    }

    return tapi_devtool_run_fini(&run);
}

/* See description in tapi_mqtt_internal.h */
te_errno
tapi_mqtt_spawn(tapi_job_factory_t *factory, const char *name,
                const char *program, const te_vec *args,
                tapi_devtool_run *run)
{
    mqtt_cmd_opt opt = {
        .n_args = te_vec_size(args),
        .args = te_vec_size(args) == 0 ? NULL :
                (const char **)te_vec_get((te_vec *)args, 0),
    };
    te_errno rc;

    *run = (tapi_devtool_run)TAPI_DEVTOOL_RUN_INIT;

    rc = tapi_devtool_run_init(run, factory, name, program, mqtt_cmd_binds,
                               &opt, NULL);
    if (rc != 0)
        return rc;

    /* Started and left running: the caller waits for it to be ready. */
    rc = tapi_devtool_run_start(run);
    if (rc != 0)
        tapi_devtool_run_fini(run);

    return rc;
}

/* See description in tapi_mqtt.h */
bool
tapi_mqtt_available(tapi_job_factory_t *factory, int timeout_ms)
{
    te_vec args = TE_VEC_INIT(char *);
    te_string out = TE_STRING_INIT;
    int code = 0;
    bool ok;

    tapi_mqtt_arg(&args, "--help");

    /*
     * --help exits 1 on mosquitto 2.0.18, so the status says nothing.
     * What it does print is its own version line, and that is what is
     * looked for.
     */
    ok = tapi_mqtt_cmd(factory, "mosquitto_pub", &args, timeout_ms, &out,
                       &out, &code) == 0 &&
         strstr(te_string_value(&out), "mosquitto_pub version") != NULL;

    if (ok)
        RING("The agent has an MQTT client");
    else
        RING("There is no mosquitto_pub on the agent");

    te_vec_deep_free(&args);
    te_string_free(&out);

    return ok;
}

/** Publish, retained or not. */
static te_errno
mqtt_publish(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
             const char *topic, const char *payload, bool retain,
             int timeout_ms, tapi_mqtt_result *result)
{
    te_vec args = TE_VEC_INIT(char *);
    te_string err = TE_STRING_INIT;
    int code = 0;
    te_errno rc;

    if (conn->host == NULL)
    {
        ERROR("There is no broker to publish to");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    tapi_mqtt_conn_args(conn, &args);
    tapi_mqtt_arg(&args, "-t");
    tapi_mqtt_arg(&args, "%s", topic);
    tapi_mqtt_arg(&args, "-m");
    tapi_mqtt_arg(&args, "%s", payload != NULL ? payload : "");
    if (retain)
        tapi_mqtt_arg(&args, "-r");

    rc = tapi_mqtt_cmd(factory, "mosquitto_pub", &args,
                       timeout_ms > 0 ? timeout_ms : TAPI_MQTT_TIMEOUT_MS,
                       NULL, &err, &code);

    if (rc == 0)
    {
        tapi_mqtt_result answer = tapi_mqtt_result_of(code);

        if (answer != TAPI_MQTT_ACCEPTED)
        {
            RING("The broker did not take a publish on %s: %s (%s)", topic,
                 tapi_mqtt_result2str(answer), te_string_value(&err));
        }

        if (result != NULL)
            *result = answer;
    }

    te_vec_deep_free(&args);
    te_string_free(&err);

    return rc;
}

/* See description in tapi_mqtt.h */
te_errno
tapi_mqtt_publish(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
                  const char *topic, const char *payload, int timeout_ms,
                  tapi_mqtt_result *result)
{
    return mqtt_publish(factory, conn, topic, payload, false, timeout_ms,
                        result);
}

/* See description in tapi_mqtt.h */
te_errno
tapi_mqtt_publish_retained(tapi_job_factory_t *factory,
                           const tapi_mqtt_conn *conn, const char *topic,
                           const char *payload, int timeout_ms,
                           tapi_mqtt_result *result)
{
    return mqtt_publish(factory, conn, topic, payload, true, timeout_ms,
                        result);
}

/* See description in tapi_mqtt.h */
te_errno
tapi_mqtt_try_connect(tapi_job_factory_t *factory,
                      const tapi_mqtt_conn *conn, int timeout_ms,
                      tapi_mqtt_result *result)
{
    te_vec args = TE_VEC_INIT(char *);
    te_string err = TE_STRING_INIT;
    int code = 0;
    te_errno rc;

    *result = TAPI_MQTT_UNREACHABLE;

    if (conn->host == NULL)
    {
        ERROR("There is no broker to connect to");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    /*
     * A subscription that listens for no time at all: it connects,
     * subscribes to a topic nothing uses and leaves. Nothing is
     * published and nothing is read, so the only thing this can
     * disturb is a broker that counts connections.
     */
    tapi_mqtt_conn_args(conn, &args);
    tapi_mqtt_arg(&args, "-t");
    tapi_mqtt_arg(&args, "$SYS/tapi-mqtt-probe/none");
    tapi_mqtt_arg(&args, "-W");
    tapi_mqtt_arg(&args, "1");

    rc = tapi_mqtt_cmd(factory, "mosquitto_sub", &args,
                       timeout_ms > 0 ? timeout_ms : TAPI_MQTT_TIMEOUT_MS,
                       NULL, &err, &code);

    if (rc == 0)
    {
        *result = tapi_mqtt_result_of(code);
        RING("%s:%u answered '%s' to a connection %s credentials",
             conn->host, conn->port != 0 ? conn->port :
                 (conn->tls ? TAPI_MQTT_TLS_PORT : TAPI_MQTT_PORT),
             tapi_mqtt_result2str(*result),
             conn->username != NULL ? "with" : "without");

        if (*result == TAPI_MQTT_REFUSED && err.len != 0)
            RING("  it said: %s", te_string_value(&err));
    }

    te_vec_deep_free(&args);
    te_string_free(&err);

    return rc;
}

/**
 * Read what mosquitto_sub -v printed.
 *
 * One message per line, the topic, a space, and the payload. A payload
 * with a newline in it therefore arrives as several lines, and the
 * ones after the first are read as messages with no payload - which is
 * visible in the log rather than silently wrong, and is the price of
 * a format that has no framing.
 */
static void
mqtt_parse_messages(const char *text, te_vec *messages)
{
    const char *line = text;

    while (line != NULL && *line != '\0')
    {
        const char *end = strchr(line, '\n');
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
        const char *space = memchr(line, ' ', len);
        tapi_mqtt_message message;

        if (len == 0)
        {
            line = end != NULL ? end + 1 : NULL;
            continue;
        }

        /* The client's own complaints go to stderr, not here. */
        memset(&message, 0, sizeof(message));

        if (space != NULL)
        {
            size_t topic_len = (size_t)(space - line);

            message.topic = TE_ALLOC(topic_len + 1);
            memcpy(message.topic, line, topic_len);

            message.payload = TE_ALLOC(len - topic_len);
            memcpy(message.payload, space + 1, len - topic_len - 1);
        }
        else
        {
            message.topic = TE_ALLOC(len + 1);
            memcpy(message.topic, line, len);
            message.payload = TE_STRDUP("");
        }

        TE_VEC_APPEND(messages, message);

        line = end != NULL ? end + 1 : NULL;
    }
}

/* See description in tapi_mqtt.h */
te_errno
tapi_mqtt_collect(tapi_job_factory_t *factory, const tapi_mqtt_conn *conn,
                  const char *topic, unsigned int seconds,
                  unsigned int count, int timeout_ms, te_vec *messages)
{
    te_vec args = TE_VEC_INIT(char *);
    te_string out = TE_STRING_INIT;
    te_string err = TE_STRING_INIT;
    int code = 0;
    te_errno rc;

    *messages = (te_vec)TE_VEC_INIT(tapi_mqtt_message);

    if (conn->host == NULL)
    {
        ERROR("There is no broker to subscribe to");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    if (seconds == 0)
        seconds = 5;

    tapi_mqtt_conn_args(conn, &args);
    tapi_mqtt_arg(&args, "-t");
    tapi_mqtt_arg(&args, "%s", topic);
    /* -v puts the topic in front of the payload; without it they are lost. */
    tapi_mqtt_arg(&args, "-v");
    tapi_mqtt_arg(&args, "-W");
    tapi_mqtt_arg(&args, "%u", seconds);
    if (count != 0)
    {
        tapi_mqtt_arg(&args, "-C");
        tapi_mqtt_arg(&args, "%u", count);
    }

    rc = tapi_mqtt_cmd(factory, "mosquitto_sub", &args,
                       timeout_ms > 0 ? timeout_ms : TAPI_MQTT_TIMEOUT_MS,
                       &out, &err, &code);
    if (rc != 0)
        goto out;

    /*
     * The output is read whatever the status, and this is the whole
     * reason the exit codes are written down in the internal header.
     * A subscription with -W ends by timing out - exit 27 - after
     * printing everything it received. Checking the status first and
     * returning would throw away exactly the messages the caller asked
     * for.
     */
    mqtt_parse_messages(te_string_value(&out), messages);

    if (code != TAPI_MQTT_EXIT_OK && code != TAPI_MQTT_EXIT_TIMEOUT)
    {
        tapi_mqtt_result answer = tapi_mqtt_result_of(code);

        if (te_vec_size(messages) == 0)
        {
            ERROR("Could not subscribe to %s: %s (%s)", topic,
                  tapi_mqtt_result2str(answer), te_string_value(&err));
            rc = answer == TAPI_MQTT_NOT_AUTHORISED ?
                 TE_RC(TE_TAPI, TE_EACCES) :
                 TE_RC(TE_TAPI, TE_ECONNREFUSED);
        }
    }

out:
    te_vec_deep_free(&args);
    te_string_free(&out);
    te_string_free(&err);

    if (rc != 0)
        tapi_mqtt_messages_free(messages);

    return rc;
}

/* See description in tapi_mqtt.h */
const tapi_mqtt_message *
tapi_mqtt_message_find(const te_vec *messages, const char *topic)
{
    size_t i;

    for (i = 0; i < te_vec_size(messages); i++)
    {
        const tapi_mqtt_message *message =
            te_vec_get((te_vec *)messages, i);

        if (strcmp(message->topic, topic) == 0)
            return message;
    }

    return NULL;
}

/* See description in tapi_mqtt.h */
void
tapi_mqtt_messages_log(const char *what, const te_vec *messages)
{
    size_t i;

    RING("%zu messages %s", te_vec_size(messages), what);

    for (i = 0; i < te_vec_size(messages); i++)
    {
        const tapi_mqtt_message *message =
            te_vec_get((te_vec *)messages, i);

        RING("  %s = %s", message->topic, message->payload);
    }
}

/* See description in tapi_mqtt.h */
void
tapi_mqtt_messages_free(te_vec *messages)
{
    size_t i;

    for (i = 0; i < te_vec_size(messages); i++)
    {
        tapi_mqtt_message *message = te_vec_get(messages, i);

        free(message->topic);
        free(message->payload);
    }

    te_vec_free(messages);
}
