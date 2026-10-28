"""Print SEGGER RTT channel 0 from an OpenOCD RTT server.

Usage:  python tools/rtt-view.py [--port 9090]
Stop with Ctrl+C.
"""

import argparse
import socket
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9090)
    args = parser.parse_args()

    try:
        with socket.create_connection((args.host, args.port)) as sock:
            while True:
                data = sock.recv(4096)
                if not data:
                    break
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    except ConnectionRefusedError:
        print(
            f"No RTT server on {args.host}:{args.port}. Start OpenOCD first.",
            file=sys.stderr,
        )
        return 1
    except KeyboardInterrupt:
        pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
