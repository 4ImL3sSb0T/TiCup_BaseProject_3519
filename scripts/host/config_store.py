"""Simple JSON config for last used COM port etc."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

_CONFIG_PATH = Path(__file__).resolve().parent / "host_config.json"

_DEFAULTS: dict[str, Any] = {
    "port": "",
    "baud": 115200,
    "poll_enabled": True,
    "poll_ms": 400,
}


def load_config() -> dict[str, Any]:
    data = dict(_DEFAULTS)
    if not _CONFIG_PATH.is_file():
        return data
    try:
        with _CONFIG_PATH.open("r", encoding="utf-8") as f:
            loaded = json.load(f)
        if isinstance(loaded, dict):
            data.update(loaded)
    except Exception:
        pass
    return data


def save_config(cfg: dict[str, Any]) -> None:
    try:
        with _CONFIG_PATH.open("w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2, ensure_ascii=False)
    except Exception:
        pass
