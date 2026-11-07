# -*- coding: utf-8 -*-
"""去掉 train/val 数据泄漏，按“同源图片”分组重新划分。

背景：Roboflow 原始数据对同一张照片做了增强（裁切、缩放、压缩等），
HF 副本重新随机划分时把同一原图的不同变体分到了 train 和 val 两边，
导致验证指标虚高。

做法：
1. 对全部图片分块计算 32x32 归一化互相关 (NCC，含水平翻转)，
   NCC>=0.8 即视为同源图片；
2. 并查集把同源图片聚成组；
3. 按组重新划分 train/val（约 9:1），同一组只进一边；
4. 输出到 datasets/gestures_clean，不破坏原始下载。

说明：不使用感知哈希预筛——裁切/缩放的变体哈希距离可能很大，
会漏检（实测漏掉 45 张 val）。全量两两计算由 numpy 分块矩阵乘完成。
"""
import hashlib
import random
import shutil
from collections import Counter, defaultdict
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "datasets" / "gestures"
DST = ROOT / "datasets" / "gestures_clean"
SEED = 42
VAL_RATIO = 0.1
NCC_THR = 0.80


def gray(path):
    return cv2.imdecode(np.fromfile(str(path), dtype=np.uint8), cv2.IMREAD_GRAYSCALE)


def features(path):
    g = gray(path)
    g32 = cv2.resize(g, (32, 32), interpolation=cv2.INTER_AREA).astype(np.float32)
    g32 = (g32 - g32.mean()) / (g32.std() + 1e-6)
    return g32


def label_classes(label_file):
    if not label_file.exists():
        raise FileNotFoundError(f"缺少标签：{label_file}")
    classes = set()
    for line in label_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            classes.add(int(line.split()[0]))
    return frozenset(classes)


def main():
    entries = []  # (image_path, label_path, split, classes)
    for split in ("train", "val"):
        for img in sorted((SRC / "images" / split).glob("*.jpg")):
            lab = SRC / "labels" / split / f"{img.stem}.txt"
            entries.append((img, lab, split, label_classes(lab)))

    n = len(entries)
    print(f"图片总数：{n}")

    g32 = np.zeros((n, 32, 32), np.float32)
    g32_flip = np.zeros((n, 32, 32), np.float32)
    for i, (img, *_rest) in enumerate(entries):
        g = features(img)
        g32[i] = g
        g32_flip[i] = g[:, ::-1]
        if (i + 1) % 300 == 0:
            print(f"  特征提取 {i + 1}/{n}")

    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    # 完全相同的文件直接合并
    md5_map = {}
    for i, (img, *_rest) in enumerate(entries):
        h = hashlib.md5(img.read_bytes()).hexdigest()
        if h in md5_map:
            union(i, md5_map[h])
        else:
            md5_map[h] = i

    # 全量两两 NCC：原图 / 水平翻转四种组合取最大（矩阵乘一次算一块）
    flat = g32.reshape(n, -1)
    flat_f = g32_flip.reshape(n, -1)
    merged_pairs = 0
    for s in range(0, n, 128):
        e = min(s + 128, n)
        sim = np.maximum.reduce([
            flat[s:e] @ flat.T,
            flat[s:e] @ flat_f.T,
            flat_f[s:e] @ flat.T,
            flat_f[s:e] @ flat_f.T,
        ]) / flat.shape[1]
        rows, cols = np.nonzero(sim >= NCC_THR)
        for r, c in zip(rows, cols):
            i, j = s + r, int(c)
            if i >= j:
                continue
            if find(i) != find(j):
                union(i, j)
                merged_pairs += 1
        if (s // 128 + 1) % 4 == 0:
            print(f"  相似对匹配 {e}/{n}")

    groups = defaultdict(list)
    for i in range(n):
        groups[find(i)].append(i)
    sizes = Counter(len(v) for v in groups.values())
    print(f"同源分组：{len(groups)} 组；多图组 {sum(1 for v in groups.values() if len(v) > 1)} 个；"
          f"合并对数 {merged_pairs}")
    print("最大组：", sorted((len(v) for v in groups.values()), reverse=True)[:10])

    # 按组分配划分，按类别分层
    rng = random.Random(SEED)
    by_class = defaultdict(list)
    for gid, idxs in groups.items():
        cls = entries[idxs[0]][3]
        by_class[cls].append(idxs)

    new_split = {}
    for cls, groups_of_cls in by_class.items():
        if cls == frozenset():  # 没有目标的图片全放 train
            for g in groups_of_cls:
                for i in g:
                    new_split[i] = "train"
            continue
        total = sum(len(g) for g in groups_of_cls)
        target_val = int(round(total * VAL_RATIO))
        rng.shuffle(groups_of_cls)
        groups_of_cls.sort(key=len)  # 先用小组填充，减少 overshoot
        val_count = 0
        val_groups, train_groups = [], []
        for g in groups_of_cls:
            if val_count < target_val:
                val_groups.append(g)
                val_count += len(g)
            else:
                train_groups.append(g)
        for g in val_groups:
            for i in g:
                new_split[i] = "val"
        for g in train_groups:
            for i in g:
                new_split[i] = "train"

    # 输出
    if DST.exists():
        shutil.rmtree(DST)
    for split in ("train", "val"):
        (DST / "images" / split).mkdir(parents=True, exist_ok=True)
        (DST / "labels" / split).mkdir(parents=True, exist_ok=True)

    stats = Counter()
    for i, (img, lab, old_split, classes) in enumerate(entries):
        split = new_split[i]
        shutil.copy2(img, DST / "images" / split / img.name)
        shutil.copy2(lab, DST / "labels" / split / lab.name)
        stats[(split, tuple(sorted(classes)))] += 1

    print("\n新划分统计：")
    for split in ("train", "val"):
        total = sum(v for (s, _), v in stats.items() if s == split)
        print(f"  {split}: {total}")
        for (s, cls), v in sorted(stats.items()):
            if s == split:
                print(f"    类别 {cls}: {v}")

    # 泄漏自检：同一组是否跨 split
    leak = 0
    for gid, idxs in groups.items():
        splits = {new_split[i] for i in idxs}
        if len(splits) > 1:
            leak += 1
    print(f"\n跨 split 的同源组：{leak}（应为 0）")
    print(f"输出目录：{DST}")


if __name__ == "__main__":
    main()
