# tsf-mqtt

MQTT from a test suite, packaged as an external Test Environment (TE)
repository.

Library:

- `tapi_mqtt` — engine-side, built as a shared library: connect,
  publish, subscribe and collect; what a broker lets a stranger do; and
  a broker that is wrong on purpose.

## Why a broker is worth this attention

[tsf-cybersec](https://github.com/interpretica-io/tsf-cybersec) already
has an MQTT rule in `tapi_sniffrules`, and it watches traffic go past.
This is the other half: **being a client**. A rule over a capture can
say that a password crossed the wire in the clear. Only a client can
find out that the broker would have let it in without one.

A broker is the place every device in an installation talks through.
That is what makes one misconfigured line so expensive: a broker that
lets a stranger subscribe to `#` hands over the whole installation at
once — every reading, every command, every device that announces
itself. It is one line of configuration and it is the most common
serious finding there is.

## Usage

```yaml
repositories:
  - name: tsf_mqtt
    url: https://github.com/interpretica-io/tsf-mqtt.git
    ref: <tag>
    libs: [ tapi_mqtt ]
```

```
TE_EXT_REPO_USE([tsf_devtool], [], [tapi_devtool])
TE_EXT_REPO_USE([tsf_cybersec], [], [tapi_cybersec])
TE_EXT_REPO_USE([tsf_mqtt], [], [tapi_mqtt])
```

Then add `tapi_mqtt` to `te_libs` in the suite's `meson.build`.
Requires an **RPC** job factory, and `mosquitto-clients` on the agent —
plus `mosquitto` itself if the test stands up a broker of its own.

```c
tapi_mqtt_conn conn = tapi_mqtt_default_conn;
te_vec messages;

conn.host = "dut.example.net";
CHECK_RC(tapi_mqtt_publish(factory, &conn, "cmd/led", "on", 10000, NULL));
CHECK_RC(tapi_mqtt_collect(factory, &conn, "state/#", 5, 0, 10000,
                           &messages));
tapi_mqtt_messages_log("the device sent", &messages);
tapi_mqtt_messages_free(&messages);
```

## The exit codes, and the one that catches people out

Everything here rests on what the mosquitto clients exit with, and none
of it is obvious. Measured against mosquitto 2.0.18:

| situation | exit | what the client printed |
|---|---|---|
| connected and did the job | 0 | — |
| could not reach the broker | 1 | `Error: Connection refused` |
| the broker said no | 5 | `Connection Refused: not authorised.` |
| a subscription reached its `-W` deadline | **27** | **every message it received, then** `Timed out` |

The last one is the trap. A subscription with a deadline **always**
ends by timing out — that is how it ends — and it prints everything it
received *before* doing so. A caller that checks the status first and
returns would throw away exactly the data it asked for. So
`tapi_mqtt_collect()` reads the output whatever the status, and only
then decides whether anything went wrong.

It is also why exit 27 maps to *accepted*: a subscription that ran out
of time had connected, which is the question `tapi_mqtt_try_connect()`
is asking.

One asymmetry worth knowing: `-W` exists on `mosquitto_sub` and **not**
on `mosquitto_pub`, which rejects it as an unknown option.

## What the audit asks

`tapi_mqtt_audit()` connects as a stranger and finds out what it is
allowed to do.

| Finding | Severity | When |
|---|---|---|
| `mqtt.wildcard-subscribe` | critical | `#` returned messages to an unauthenticated client |
| `mqtt.anonymous-allowed` | high | it accepts a client with no credentials |
| `mqtt.no-tls` | high | it accepts a connection without TLS |
| `mqtt.publish-allowed` | high | a stranger could publish |
| `mqtt.known-credential` | high | a credential the test named still works |
| `mqtt.retained-readable` | medium | some of those were waiting before the subscription began |
| `mqtt.sys-readable` | medium | `$SYS` gave up the broker's own bookkeeping |
| `mqtt.wildcard-quiet` | info | `#` was accepted and nothing arrived |
| `mqtt.not-assessed` | info | it refused a stranger, so the rest is unknown |

Two of these exist to stop the report lying by silence.
`mqtt.wildcard-quiet` is raised when a subscription to `#` is accepted
and nothing comes — because that is not the same as being refused, and
an absence of findings would read as a clean broker. `mqtt.not-assessed`
is raised when the broker *did* refuse a stranger: good, but it means
nothing below was established, and a report that just stopped would
look like a pass.

### What it does to the broker

Reads, by default. It connects, subscribes and listens. The one check
that writes is off unless the test names a scratch topic, and it
publishes one short message there and **not retained** — a retained
probe would outlive the test, which is the one thing an audit must not
leave behind.

Credentials are never invented. `tapi_mqtt_policy::credentials` is
empty unless a test fills it, and it is meant for the handful a device
ships with and should not have kept — not for a word list.

## A broker that is wrong on purpose

`tapi_mqtt_broker` stands up a `mosquitto` on the agent configured
exactly as the test says, because a check that reports nothing looks
the same whether the broker is clean or the check is broken.

Three things about running one were measured and are built in:

- **`per_listener_settings true` is not decoration.** Without it
  `allow_anonymous` is a global, so a second configuration file — or a
  second listener — silently overrides the first. Measured: a listener
  written to require a password accepted anonymous clients because
  another file loaded later said anonymous was fine. That is also a
  real-world finding, not just a test-rig detail: a broker can look
  authenticated in its configuration and not be.
- **The password file must be readable by the broker's own user.**
  mosquitto drops privileges after reading its configuration, so a
  root-owned `0600` file leaves it logging
  `Error: Unable to open pwfile` and refusing every connection — which
  from outside looks exactly like a broker that is working correctly
  and rejecting you.
- **The startup banner goes to standard error.** So waiting for
  `mosquitto version ...` on standard output waits for ever on a broker
  that started perfectly. `tapi_mqtt_broker_start()` waits by
  connecting instead, which tests the thing the next line depends on
  and needs nothing extra.

## What was verified

Against mosquitto 2.0.18, not from documentation: the full exit-code
map above; a subscription to `#` on an open broker returning retained
messages including a device state; `$SYS/broker/version` answering;
publishing as a stranger; and the same sequence against an
authenticated listener refusing every step with exit 5.

What has **not** been run is the library itself against a broker
through a Test Agent — there is no suite using it yet. The command
shapes, the exit codes and the audit's logic were each verified by
replaying exactly what the code emits.
