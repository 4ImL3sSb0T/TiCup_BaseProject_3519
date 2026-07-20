"""Entry point for BaseProject host UI."""

from __future__ import annotations

import argparse
import sys


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="BaseProject_3519 serial host UI")
    parser.add_argument("--port", default="", help="Serial port, e.g. COM8")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default 921600)")
    parser.add_argument(
        "--self-check",
        action="store_true",
        help="Run protocol parser self-check and exit",
    )
    args = parser.parse_args(argv)

    if args.self_check:
        from host.protocol import self_check

        self_check()
        return 0

    # Matplotlib backend must be set before pyplot import inside UI
    import matplotlib

    matplotlib.use("TkAgg")

    from host.ui.app import HostApp

    app = HostApp(default_port=args.port, baud=args.baud)
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
