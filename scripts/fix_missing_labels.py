# -*- coding: utf-8 -*-
"""补齐缺失的标签文件：严格串行 + 间隔，避开 HF 镜像限流。"""
import time
from pathlib import Path

import requests

BASE = "https://hf-mirror.com/datasets/Xuban/rock-paper-scissors-roboflow/resolve/main/"
SRC = Path(__file__).resolve().parents[1] / "datasets" / "gestures"
HEADERS = {"User-Agent": "Mozilla/5.0"}


def missing_files():
    out = []
    for split in ("train", "val"):
        for img in sorted((SRC / "images" / split).glob("*.jpg")):
            lab = SRC / "labels" / split / f"{img.stem}.txt"
            if not lab.exists():
                out.append((split, img.stem))
    return out


def main():
    todo = missing_files()
    print(f"缺失标签：{len(todo)} 个", flush=True)
    for split, stem in todo:
        rel = f"labels/{split}/{stem}.txt"
        for attempt in range(8):
            try:
                r = requests.get(BASE + rel, headers=HEADERS, timeout=60)
                text = r.text
                if r.status_code == 200 and not text.lstrip().startswith("<!DOCTYPE"):
                    (SRC / "labels" / split / f"{stem}.txt").write_text(text, encoding="utf-8")
                    print("ok", rel, flush=True)
                    break
                print(f"retry {r.status_code} {rel}", flush=True)
                time.sleep(65)
            except Exception as e:
                print(f"error {e} {rel}", flush=True)
                time.sleep(30)
        time.sleep(1.2)
    left = missing_files()
    print(f"剩余缺失：{len(left)}", flush=True)
    raise SystemExit(1 if left else 0)


if __name__ == "__main__":
    main()
