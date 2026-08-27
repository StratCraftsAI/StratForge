from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / "datas"
OUTPUT_DIR = ROOT / "output"


def ensure_output_dir(*parts: str) -> Path:
    path = OUTPUT_DIR.joinpath(*parts)
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def import_backtrader():
    try:
        import backtrader as bt
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "backtrader is not installed. Run ./bootstrap_env.sh from tools/golden_extract first."
        ) from exc

    return bt
