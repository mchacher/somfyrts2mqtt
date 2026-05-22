#!/usr/bin/env python3
"""
mqtt-cli.py -- CLI for the somfyrts2mqtt bridge.

Speaks the Tasmota Shutter MQTT subset that the bridge exposes
(iter 014). One subcommand per supported verb, plus `watch` to
live-tail the telemetry topics.

Quickstart
----------
    pip install paho-mqtt
    chmod +x tools/mqtt-cli.py

    # one-shot via flags
    tools/mqtt-cli.py --broker 192.168.0.5 --root somfyrts2mqtt-AB12CD list

    # or set the env once
    export MQTT_BROKER=192.168.0.5
    export MQTT_ROOT=somfyrts2mqtt-AB12CD
    tools/mqtt-cli.py open kitchen
    tools/mqtt-cli.py open-dur kitchen 18.0
    tools/mqtt-cli.py close-dur kitchen 20.0
    tools/mqtt-cli.py position kitchen 50
    tools/mqtt-cli.py watch                  # Ctrl-C to stop

Env vars (alternatives to flags) :
    MQTT_BROKER  broker host (required)
    MQTT_PORT    broker port (default 1883)
    MQTT_USER    username (optional)
    MQTT_PASS    password (optional)
    MQTT_ROOT    bridge root topic, e.g. somfyrts2mqtt-AB12CD (required)
"""

import argparse
import json
import os
import sys
import time
from datetime import datetime

# `paho-mqtt` imported lazily inside `connect()` so that `--help` works
# even when the dep is not installed.
mqtt = None  # populated by connect()


# --- ANSI colours (only when stdout is a TTY) ----------------------------

_COLOR = sys.stdout.isatty()
def _c(code: str, s: str) -> str:
    return f"\033[{code}m{s}\033[0m" if _COLOR else s

def dim(s):    return _c("2", s)
def bold(s):   return _c("1", s)
def green(s):  return _c("32", s)
def yellow(s): return _c("33", s)
def cyan(s):   return _c("36", s)
def red(s):    return _c("31", s)


# --- CLI parser ----------------------------------------------------------

VERB_MAP = {
    "open":      "Open",
    "close":     "Close",
    "stop":      "Stop",
    "position":  "Position",
    "setpos":    "SetPosition",
    "open-dur":  "OpenDuration",
    "close-dur": "CloseDuration",
}


def parse_args():
    ap = argparse.ArgumentParser(
        prog="mqtt-cli.py",
        description="CLI for the somfyrts2mqtt bridge (Tasmota Shutter subset).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="See file docstring for env-var defaults and quickstart.",
    )
    ap.add_argument("--broker",   default=os.environ.get("MQTT_BROKER"),
                    help="MQTT broker host (default $MQTT_BROKER)")
    ap.add_argument("--port",     type=int,
                    default=int(os.environ.get("MQTT_PORT", "1883")),
                    help="MQTT broker port (default $MQTT_PORT or 1883)")
    ap.add_argument("--user",     default=os.environ.get("MQTT_USER"),
                    help="MQTT username (default $MQTT_USER)")
    ap.add_argument("--password", default=os.environ.get("MQTT_PASS"),
                    help="MQTT password (default $MQTT_PASS)")
    ap.add_argument("--root",     default=os.environ.get("MQTT_ROOT"),
                    help="Bridge root topic (default $MQTT_ROOT)")
    ap.add_argument("--quiet",    action="store_true",
                    help="Only print errors")

    sub = ap.add_subparsers(dest="cmd", required=True, metavar="COMMAND")

    for v in ("open", "close", "stop"):
        s = sub.add_parser(v, help=f"Send cmnd .../{VERB_MAP[v]}")
        s.add_argument("name", help="Remote name")

    for v in ("position", "setpos"):
        s = sub.add_parser(v, help=f"Send cmnd .../{VERB_MAP[v]} <0..100>")
        s.add_argument("name", help="Remote name")
        s.add_argument("value", type=int, help="Position percentage (0 closed, 100 open)")

    for v in ("open-dur", "close-dur"):
        s = sub.add_parser(v, help=f"Set the {VERB_MAP[v]} (seconds)")
        s.add_argument("name", help="Remote name")
        s.add_argument("seconds", type=float, help="Duration in seconds (e.g. 18.5)")

    w = sub.add_parser("watch", help="Live-tail tele/<root>/# and stat/<root>/#")
    w.add_argument("--name", default=None, help="Filter on a single remote name")
    w.add_argument("--timeout", type=int, default=0,
                   help="Stop after N seconds (0 = until Ctrl-C)")

    sub.add_parser("status", help="Read retained LWT and the latest SENSOR")
    sub.add_parser("list",   help="Tabular shutter overview from the next SENSOR")

    return ap.parse_args()


# --- MQTT plumbing -------------------------------------------------------

def connect(args):
    global mqtt
    if mqtt is None:
        try:
            import paho.mqtt.client as paho_mqtt
            mqtt = paho_mqtt
        except ImportError:
            sys.exit(red("Missing dependency : pip install paho-mqtt"))

    if not args.broker:
        sys.exit(red("Missing --broker (or MQTT_BROKER env var)"))
    if not args.root:
        sys.exit(red("Missing --root (or MQTT_ROOT env var)"))

    cli = mqtt.Client(client_id=f"mqtt-cli-{os.getpid()}")
    if args.user:
        cli.username_pw_set(args.user, args.password)
    try:
        cli.connect(args.broker, args.port, keepalive=15)
    except OSError as e:
        sys.exit(red(f"Connect failed : {e}"))
    cli.loop_start()
    return cli


def publish(cli, root, name, verb, payload="", quiet=False):
    topic = f"cmnd/{root}/{name}/{verb}"
    info = cli.publish(topic, payload, qos=0, retain=False)
    info.wait_for_publish(timeout=5)
    if info.rc != mqtt.MQTT_ERR_SUCCESS:
        sys.exit(red(f"Publish failed rc={info.rc}"))
    if not quiet:
        arrow = cyan("->")
        body = f" {dim(payload)}" if payload else ""
        print(f"{arrow} {topic}{body}")


# --- commands ------------------------------------------------------------

def _pretty_payload(raw: bytes) -> str:
    text = raw.decode("utf-8", errors="replace")
    try:
        return json.dumps(json.loads(text), separators=(",", ":"))
    except Exception:
        return text


def cmd_watch(cli, root, name_filter, timeout):
    print(f"Subscribing to {bold(f'tele/{root}/#')} and {bold(f'stat/{root}/#')}")
    print(dim("(Ctrl-C to stop)\n"))

    def on_message(_c, _u, msg):
        if name_filter:
            # Match either ".../<name>/..." or "...<name>" suffix in stat topics.
            if f"/{name_filter}/" not in msg.topic and not msg.topic.endswith(f"/{name_filter}"):
                # Allow SENSOR (aggregated) to pass when filtering by name.
                if not msg.topic.endswith("/SENSOR") and not msg.topic.endswith("/LWT"):
                    return
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        topic_col = yellow(msg.topic) if msg.topic.startswith(f"stat/") else green(msg.topic)
        print(f"{dim(ts)}  {topic_col}")
        print(f"           {_pretty_payload(msg.payload)}")

    cli.on_message = on_message
    cli.subscribe([(f"tele/{root}/#", 0), (f"stat/{root}/#", 0)])

    start = time.time()
    try:
        while True:
            time.sleep(0.2)
            if timeout and time.time() - start >= timeout:
                break
    except KeyboardInterrupt:
        print()


def cmd_status(cli, root):
    state = {"lwt": None, "sensor": None}

    def on_message(_c, _u, msg):
        if msg.topic.endswith("/LWT"):
            state["lwt"] = msg.payload.decode("utf-8", errors="replace")
        elif msg.topic.endswith("/SENSOR"):
            try:
                state["sensor"] = json.loads(msg.payload.decode("utf-8", errors="replace"))
            except Exception:
                state["sensor"] = msg.payload.decode("utf-8", errors="replace")

    cli.on_message = on_message
    cli.subscribe(f"tele/{root}/+")
    # Wait for retained messages to arrive. LWT is retained ; SENSOR is not.
    time.sleep(1.5)

    lwt = state["lwt"]
    if lwt == "Online":
        print(f"LWT    : {green(lwt)}")
    elif lwt:
        print(f"LWT    : {red(lwt)}")
    else:
        print(f"LWT    : {dim('(no retained message)')}")

    print("SENSOR :")
    if isinstance(state["sensor"], dict):
        for n, info in state["sensor"].items():
            print(f"  {bold(n)}  {info}")
    elif state["sensor"]:
        print(f"  {state['sensor']}")
    else:
        print(dim("  (no telemetry seen yet ; trigger a motion to populate SENSOR)"))


def cmd_list(cli, root):
    state = {"sensor": None}
    def on_message(_c, _u, msg):
        if msg.topic.endswith("/SENSOR"):
            try:
                state["sensor"] = json.loads(msg.payload.decode("utf-8", errors="replace"))
            except Exception:
                pass
    cli.on_message = on_message
    cli.subscribe(f"tele/{root}/SENSOR")
    # No retain on SENSOR -- wait briefly in case a motion or recent event
    # caused a publish. User can trigger one via any cmnd to populate.
    time.sleep(1.2)

    s = state["sensor"]
    if not s:
        print(dim("(no SENSOR seen ; send any cmnd to trigger a publish)"))
        return

    name_w = max(8, max(len(n) for n in s.keys()))
    print(f"{bold('Name'.ljust(name_w))}  {bold('Pos'):>5}  Dir  Target")
    print(dim("-" * (name_w + 22)))
    for name, info in s.items():
        pos    = info.get("Position", 0)
        dirn   = info.get("Direction", 0)
        target = info.get("Target", 0)
        arrow  = green("UP  ") if dirn == 1 else (red("DOWN") if dirn == -1 else dim(" -- "))
        print(f"{name.ljust(name_w)}  {pos:>4}%  {arrow}  {target:>4}%")


# --- main ----------------------------------------------------------------

def main():
    args = parse_args()
    cli = connect(args)
    try:
        verb = VERB_MAP.get(args.cmd)
        if verb in ("Open", "Close", "Stop"):
            publish(cli, args.root, args.name, verb, quiet=args.quiet)
        elif verb == "Position" or verb == "SetPosition":
            if not (0 <= args.value <= 100):
                sys.exit(red("value must be in 0..100"))
            publish(cli, args.root, args.name, verb, str(args.value), quiet=args.quiet)
        elif verb in ("OpenDuration", "CloseDuration"):
            if args.seconds < 0 or args.seconds > 3600:
                sys.exit(red("seconds must be in 0..3600"))
            publish(cli, args.root, args.name, verb, f"{args.seconds:.1f}", quiet=args.quiet)
        elif args.cmd == "watch":
            cmd_watch(cli, args.root, args.name, args.timeout)
        elif args.cmd == "status":
            cmd_status(cli, args.root)
        elif args.cmd == "list":
            cmd_list(cli, args.root)
        else:
            sys.exit(red(f"Unknown command : {args.cmd}"))
    finally:
        cli.loop_stop()
        cli.disconnect()


if __name__ == "__main__":
    main()
