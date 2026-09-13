# =============================================================================
# make_test_valley.py
#   Blender を使わずに、blender_export_valley.py と同じ「試作用の谷」を
#   同じ OBJ 形式 (Blender の Forward -Z / Up Y エクスポートと同じ座標変換) で書き出す。
#   Blender が手元にない環境で OBJ 読み込みの動作確認をするためのもの。
#
#   使い方:  python tools/make_test_valley.py   →  assets/terrain.obj
#
#   Blender のエクスポート (Forward -Z, Up Y) は
#     Blender (x, y, z)  →  OBJ (x, z, -y)
#   の変換をするので、ここでも同じ変換を適用する。
# =============================================================================
import math
import os
import sys

SIZE = 60.0
SEGMENTS = 120
UV_TILES = 7.0


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def rough_noise(x, y):
    n = 0.0
    n += 0.50 * math.sin(0.35 * x + 1.3) * math.sin(0.27 * y + 0.4)
    n += 0.30 * math.sin(0.71 * x + 2.1) * math.sin(0.63 * y + 1.9)
    n += 0.15 * math.sin(1.53 * x + 0.2) * math.sin(1.37 * y + 2.7)
    n += 0.05 * math.sin(3.10 * x + 1.1) * math.sin(2.90 * y + 0.9)
    return n


def valley_height(x, y):
    """blender_export_valley.py と同一 (Blender 座標: +y が上流)"""
    c = 5.0 * math.sin(y * 0.12) + 2.5 * math.sin(y * 0.30 + 0.8)
    d = abs(x - c)
    w = smoothstep(2.0, 9.0, d)
    base = 0.18 * y
    steps = 2.0 * smoothstep(-0.6, 0.6, y - 10.0) + 2.0 * smoothstep(-0.6, 0.6, y + 8.0)
    floor_u = 0.25 * min(d, 2.0) ** 2
    mountain = 10.0 * w
    rough = rough_noise(x, y) * (0.2 + 4.0 * w)
    return base + steps + floor_u + mountain + rough


def main(out_path):
    n = SEGMENTS + 1
    # Blender 座標で頂点を作る
    bx = [[-SIZE / 2 + SIZE * i / SEGMENTS for i in range(n)] for _ in range(n)]
    by = [[-SIZE / 2 + SIZE * j / SEGMENTS for i in range(n)] for j in range(n)]
    bz = [[valley_height(bx[j][i], by[j][i]) for i in range(n)] for j in range(n)]

    # 法線 (中心差分、Blender 座標: (-dz/dx, -dz/dy, 1))
    h = SIZE / SEGMENTS
    lines = ["# test valley terrain (Blender-compatible export: Forward -Z, Up Y)",
             f"o ValleyTerrain"]
    for j in range(n):
        for i in range(n):
            x, y, z = bx[j][i], by[j][i], bz[j][i]
            # Blender → OBJ: (x, y, z) → (x, z, -y)
            lines.append(f"v {x:.4f} {z:.4f} {-y:.4f}")
    for j in range(n):
        for i in range(n):
            lines.append(f"vt {(i / SEGMENTS) * UV_TILES:.4f} {(j / SEGMENTS) * UV_TILES:.4f}")
    for j in range(n):
        for i in range(n):
            zx = (bz[j][min(i + 1, n - 1)] - bz[j][max(i - 1, 0)]) / (h * (min(i + 1, n - 1) - max(i - 1, 0)))
            zy = (bz[min(j + 1, n - 1)][i] - bz[max(j - 1, 0)][i]) / (h * (min(j + 1, n - 1) - max(j - 1, 0)))
            nx, ny, nz = -zx, -zy, 1.0
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            nx, ny, nz = nx / l, ny / l, nz / l
            lines.append(f"vn {nx:.4f} {nz:.4f} {-ny:.4f}")   # 同じ軸変換
    lines.append("s 1")
    # 四角形面 (Blender と同じく quad のまま出力。ローダー側で三角形化される)
    for j in range(SEGMENTS):
        for i in range(SEGMENTS):
            a = j * n + i + 1
            b = a + 1
            c = a + n + 1
            d = a + n
            lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c} {d}/{d}/{d}")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="ascii") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", out_path, f"({n * n} vertices, {SEGMENTS * SEGMENTS} quads)")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "assets", "terrain.obj"))
