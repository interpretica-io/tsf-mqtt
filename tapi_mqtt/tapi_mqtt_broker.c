/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief A broker that is wrong on purpose
 */

#define TE_LGR_USER "TAPI MQTT BROKER"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "tapi_cfg_base.h"
#include "tapi_file.h"
#include "te_alloc.h"
#include "te_sleep.h"
#include "te_string.h"
#include "te_vector.h"

#include "tapi_mqtt_broker.h"
#include "tapi_mqtt_internal.h"

/** What mosquitto prints once it is listening. */
#define MQTT_BROKER_READY "mosquitto version"

/** How long to wait for it to listen, ms. */
#define MQTT_BROKER_READY_MS 10000

/** How often it is asked whether it is listening yet, ms. */
#define MQTT_BROKER_POLL_MS 250

/** How long to let it stop, ms. */
#define MQTT_BROKER_TERM_MS 5000

/** The user it accepts when it is not anonymous. */
#define MQTT_BROKER_USER "tapi"

/** That user's password. */
#define MQTT_BROKER_PASSWORD "tapi-broker-password"

struct tapi_mqtt_broker {
    /** The mosquitto job. */
    tapi_devtool_run run;
    /** Agent it runs on. */
    char *ta;
    /** Its configuration file, removed with the broker. */
    char *config;
    /** Its password file, if it has one. */
    char *pwfile;
    /** The user it accepts, or NULL. */
    char *username;
    /** That user's password, or NULL. */
    char *password;
};

const tapi_mqtt_broker_opt tapi_mqtt_broker_default_opt = {
    .port = 0,
    .defects = 0,
};

/** Put a file in the agent's temporary directory. */
static te_errno
mqtt_put_file(const char *ta, const char *suffix, const char *content,
              char **path)
{
    te_string generated = TE_STRING_INIT;
    char *tmp_dir = tapi_cfg_base_get_ta_dir(ta, TAPI_CFG_BASE_TA_DIR_TMP);
    te_errno rc;

    if (tmp_dir == NULL)
    {
        ERROR("Failed to get the temporary directory of TA %s", ta);
        return TE_RC(TE_TAPI, TE_EFAIL);
    }

    tapi_file_make_custom_pathname(&generated, tmp_dir, suffix);
    free(tmp_dir);

    rc = tapi_file_create_ta(ta, generated.ptr, "%s", content);
    if (rc != 0)
    {
        te_string_free(&generated);
        return rc;
    }

    *path = generated.ptr;

    return 0;
}

/** Poll the broker until it accepts a connection. */
static te_errno
mqtt_broker_wait(tapi_job_factory_t *factory, uint16_t port,
                 const tapi_mqtt_broker *broker, int timeout_ms)
{
    tapi_mqtt_conn conn = tapi_mqtt_default_conn;
    int waited_ms;

    conn.host = "127.0.0.1";
    conn.port = port;
    conn.username = broker->username;
    conn.password = broker->password;

    for (waited_ms = 0; waited_ms < timeout_ms;
         waited_ms += MQTT_BROKER_POLL_MS)
    {
        tapi_mqtt_result result;

        if (tapi_mqtt_try_connect(factory, &conn, MQTT_BROKER_POLL_MS * 8,
                                  &result) == 0 &&
            result != TAPI_MQTT_UNREACHABLE)
        {
            return 0;
        }

        te_motivated_msleep(MQTT_BROKER_POLL_MS,
                            "waiting for the broker to listen");
    }

    return TE_RC(TE_TAPI, TE_ETIMEDOUT);
}

/* See description in tapi_mqtt_broker.h */
te_errno
tapi_mqtt_broker_start(tapi_job_factory_t *factory,
                       const tapi_mqtt_broker_opt *opt, int timeout_ms,
                       tapi_mqtt_broker **broker)
{
    const char *ta = tapi_job_factory_ta(factory);
    tapi_mqtt_broker *result;
    te_string config = TE_STRING_INIT;
    te_vec args = TE_VEC_INIT(char *);
    bool anonymous = (opt->defects & TAPI_MQTT_DEFECT_ANONYMOUS) != 0;
    te_errno rc;

    if (opt->port == 0)
    {
        ERROR("The broker needs a port to listen on");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    if (ta == NULL)
    {
        ERROR("Cannot determine the agent behind the job factory");
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    result = TE_ALLOC(sizeof(*result));
    result->run = (tapi_devtool_run)TAPI_DEVTOOL_RUN_INIT;
    result->ta = TE_STRDUP(ta);

    if (!anonymous)
    {
        te_string pw = TE_STRING_INIT;

        result->username = TE_STRDUP(opt->username != NULL ?
                                     opt->username : MQTT_BROKER_USER);
        result->password = TE_STRDUP(opt->password != NULL ?
                                     opt->password : MQTT_BROKER_PASSWORD);

        /*
         * Written in the clear, and then handed to mosquitto_passwd to
         * hash in place - which is how that tool works and the only
         * way to produce a file it will read.
         */
        te_string_append(&pw, "%s:%s\n", result->username,
                         result->password);
        rc = mqtt_put_file(ta, "-mqtt.pw", pw.ptr, &result->pwfile);
        te_string_free(&pw);

        if (rc != 0)
            goto fail;

        /*
         * Tightened before it is filled in. mosquitto_passwd already
         * warns that a world-readable password file will be refused by
         * a future version, and a file this library made is a file it
         * should not have to be warned about.
         */
        tapi_mqtt_arg(&args, "0600");
        tapi_mqtt_arg(&args, "%s", result->pwfile);
        (void)tapi_mqtt_cmd(factory, "chmod", &args, timeout_ms, NULL,
                            NULL, NULL);
        te_vec_deep_free(&args);
        args = (te_vec)TE_VEC_INIT(char *);

        tapi_mqtt_arg(&args, "-U");
        tapi_mqtt_arg(&args, "%s", result->pwfile);

        rc = tapi_mqtt_cmd(factory, "mosquitto_passwd", &args, timeout_ms,
                           NULL, NULL, NULL);
        te_vec_deep_free(&args);
        args = (te_vec)TE_VEC_INIT(char *);

        if (rc != 0)
            goto fail;
    }

    /*
     * per_listener_settings is not decoration. Without it
     * allow_anonymous is a global, so a second configuration file - or
     * a second listener in the same one - silently overrides the
     * first. Measured on mosquitto 2.0.18: a listener written to
     * require a password accepted anonymous clients because another
     * file loaded later said anonymous was fine.
     */
    te_string_append(&config,
        "per_listener_settings true\n"
        "listener %u 127.0.0.1\n"
        "allow_anonymous %s\n",
        opt->port, anonymous ? "true" : "false");

    if (!anonymous)
        te_string_append(&config, "password_file %s\n", result->pwfile);

    rc = mqtt_put_file(ta, "-mqtt.conf", config.ptr, &result->config);
    te_string_free(&config);

    if (rc != 0)
        goto fail;

    RING("Standing up a broker on port %u, %s", opt->port,
         anonymous ? "open to anyone" : "asking for a password");

    tapi_mqtt_arg(&args, "-c");
    tapi_mqtt_arg(&args, "%s", result->config);
    /* -v so that it says what it is doing, including why it will not. */
    tapi_mqtt_arg(&args, "-v");

    rc = tapi_mqtt_spawn(factory, "mosquitto",
                         opt->binary != NULL ? opt->binary : "mosquitto",
                         &args, &result->run);
    if (rc != 0)
        goto fail;

    /*
     * Wait by connecting, not by reading the banner.
     *
     * The obvious way does not work: measured on mosquitto 2.0.18,
     * "mosquitto version 2.0.18 starting" goes to standard *error*,
     * and the readiness helper reads standard output - so waiting for
     * it would wait for ever on a broker that started perfectly.
     *
     * Connecting is better than working around that anyway. It tests
     * the thing the next line depends on, it needs nothing that is not
     * already required, and a broker that is up but refusing is not a
     * broker that is ready.
     */
    rc = mqtt_broker_wait(factory, opt->port, result,
                          timeout_ms > 0 ? timeout_ms :
                              MQTT_BROKER_READY_MS);
    if (rc != 0)
    {
        tapi_devtool_output output;

        tapi_devtool_run_get_output(&result->run, &output);
        ERROR("The broker never started listening on port %u: %s",
              opt->port,
              output.err != NULL && output.err[0] != '\0' ?
                  output.err : "it said nothing");
        (void)tapi_devtool_run_stop(&result->run);
        goto fail;
    }

    te_vec_deep_free(&args);
    *broker = result;

    return 0;

fail:
    te_vec_deep_free(&args);
    tapi_devtool_run_fini(&result->run);
    if (result->config != NULL)
        (void)tapi_file_ta_unlink_fmt(ta, "%s", result->config);
    if (result->pwfile != NULL)
        (void)tapi_file_ta_unlink_fmt(ta, "%s", result->pwfile);
    free(result->ta);
    free(result->config);
    free(result->pwfile);
    free(result->username);
    free(result->password);
    free(result);

    return rc;
}

/* See description in tapi_mqtt_broker.h */
const char *
tapi_mqtt_broker_username(const tapi_mqtt_broker *broker)
{
    return broker->username;
}

/* See description in tapi_mqtt_broker.h */
const char *
tapi_mqtt_broker_password(const tapi_mqtt_broker *broker)
{
    return broker->password;
}

/* See description in tapi_mqtt_broker.h */
te_errno
tapi_mqtt_broker_stop(tapi_mqtt_broker *broker)
{
    te_errno rc;

    if (broker == NULL)
        return 0;

    rc = tapi_devtool_run_stop(&broker->run);
    if (rc == 0)
        (void)tapi_devtool_run_wait(&broker->run, MQTT_BROKER_TERM_MS);

    tapi_devtool_run_fini(&broker->run);

    (void)tapi_file_ta_unlink_fmt(broker->ta, "%s", broker->config);
    if (broker->pwfile != NULL)
        (void)tapi_file_ta_unlink_fmt(broker->ta, "%s", broker->pwfile);

    free(broker->ta);
    free(broker->config);
    free(broker->pwfile);
    free(broker->username);
    free(broker->password);
    free(broker);

    return rc;
}
