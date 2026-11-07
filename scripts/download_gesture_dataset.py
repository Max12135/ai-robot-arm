# -*- coding: utf-8 -*-
"""从 HF 镜像下载石头剪刀布手势检测数据集（YOLO 格式）。

数据来源：Xuban/rock-paper-scissors-roboflow
原始项目：https://universe.roboflow.com/rock-paper-scissors-2/rock-paper-scissors-bwev7
许可：CC BY 4.0

为什么不用 Roboflow 官方 zip：官方导出需要登录/API key；
这里用 HF 镜像逐文件下载，内容是同一套 YOLO 标注数据。
"""
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import requests

REPO = "Xuban/rock-paper-scissors-roboflow"
API = f"https://hf-mirror.com/api/datasets/{REPO}"
BASE = f"https://hf-mirror.com/datasets/{REPO}/resolve/main/"
DEST = Path(__file__).resolve().parents[1] / "datasets" / "gestures"
HEADERS = {"User-Agent": "Mozilla/5.0"}
WORKERS = 4   # 太高会触发 HF 镜像限流


def list_files():
    r = requests.get(API, headers=HEADERS, timeout=60)
    r.raise_for_status()
    names = [s["rfilename"] for s in r.json()["siblings"]]
    return [
        n for n in names
        if n.startswith(("images/", "labels/")) or n == "data.yaml"
    ]


def download_one(rel):
    out = DEST / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists() and out.stat().st_size > 0:
        return rel, "skip"

    last_err = None
    for attempt in range(4):
        try:
            with requests.get(BASE + rel, headers=HEADERS, timeout=60, stream=True) as r:
                r.raise_for_status()
                tmp = out.with_name(out.name + ".part")
                with open(tmp, "wb") as f:
                    for chunk in r.iter_content(65536):
                        if chunk:
                            f.write(chunk)
                tmp.replace(out)
            return rel, "ok"
        except Exception as e:
            last_err = e
            time.sleep(1.5 * (attempt + 1))
    return rel, f"FAIL {last_err}"


def main():
    files = list_files()
    print(f"待下载文件：{len(files)} 个")

    fails = []
    done = 0
    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        futures = {ex.submit(download_one, f): f for f in files}
        for fut in as_completed(futures):
            rel, status = fut.result()
            done += 1
            if status.startswith("FAIL"):
                fails.append((rel, status))
            if done % 200 == 0 or done == len(files):
                print(f"进度：{done}/{len(files)}")

    print(f"完成：{done - len(fails)} 成功，{len(fails)} 失败")
    for rel, status in fails[:20]:
        print(rel, status)


if __name__ == "__main__":
    main()
