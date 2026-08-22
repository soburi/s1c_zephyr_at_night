#!/usr/bin/env python3

import argparse
import time

import zenoh


def parse_args():
    parser = argparse.ArgumentParser(description="Publish data to the CAN/Zenoh bridge")
    parser.add_argument("-k", "--key", default="can/028/tx", help="Zenoh key")
    parser.add_argument("-p", "--payload", default="toggle", help="text payload")
    parser.add_argument("--hex", dest="hex_payload", help="hex payload, e.g. '01 ff'")
    parser.add_argument("-n", "--count", type=int, default=1, help="number of messages")
    parser.add_argument("-i", "--interval", type=float, default=1.0,
                        help="interval in seconds when sending multiple messages")
    parser.add_argument("-e", "--endpoint", default="tcp/127.0.0.1:7447",
                        help="zenohd endpoint")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.count < 1:
        raise SystemExit("--count must be at least 1")

    try:
        payload = (bytes.fromhex(args.hex_payload) if args.hex_payload is not None
                   else args.payload.encode())
    except ValueError as error:
        raise SystemExit(f"invalid --hex payload: {error}") from error

    config = zenoh.Config.from_json5(f"""
    {{
        mode: "client",
        connect: {{ endpoints: ["{args.endpoint}"] }},
        scouting: {{ multicast: {{ enabled: false }} }}
    }}
    """)

    with zenoh.open(config) as session:
        for index in range(args.count):
            session.put(args.key, payload)
            print(f"TX key={args.key}, payload={payload.hex()}, length={len(payload)}",
                  flush=True)
            if index + 1 < args.count:
                time.sleep(args.interval)


if __name__ == "__main__":
    main()
