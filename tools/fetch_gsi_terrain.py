# =============================================================================
# fetch_gsi_terrain.py
#   国土地理院の標高タイル (DEM10B, 10 m メッシュ) をダウンロードし、
#   実在の地形を assets/terrain.obj (Blender 互換の OBJ) に変換する。
#
#   使い方:
#     python tools/fetch_gsi_terrain.py                       … 関ヶ原 (既定) 5 km 四方
#     python tools/fetch_gsi_terrain.py --lat 35.3646 --lon 136.4667 --size 5 --exaggeration 2.5
#
#   引数:
#     --lat / --lon      中心の緯度経度 (10 進数)
#     --size             取り出す範囲の一辺 [km]            (既定 5.0)
#     --exaggeration     高さの誇張倍率                        (既定 2.5)
#                        プログラム側で xz を 60 m に一様縮小するため、実寸の起伏
#                        (5 km に 300 m) では平らすぎる。2〜3 倍が見やすい
#     --grid             出力メッシュの 1 辺の頂点数          (既定 241)
#     --out              出力先                                (既定 assets/terrain.obj)
#     --emit-lat/--emit-lon  粒子の投入位置 (緯度経度)。指定すると assets/terrain.cfg に
#                        プログラム内座標 (emit_x, emit_z) を書き出す。省略時は自動 (北端の最低点)
#
#   座標系:
#     Blender 座標 (x: 東, y: 北, z: 上) で作り、Blender の OBJ エクスポート
#     (Forward -Z, Up Y) と同じ変換 (x, y, z) → (x, z, -y) で書き出す。
#     プログラムはさらに x を反転するので、画面では 北 = 奥 (上流)、東 = 右 になる。
#
#   出典: 国土地理院 標高タイル (https://maps.gsi.go.jp/development/ichiran.html)
#         利用時は「出典: 国土地理院」の表記が必要。
# =============================================================================
import argparse, math, os, sys, urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
CACHE = os.path.join(ROOT, 'tools', 'cache_gsi')
TILE = 256
ZOOM = 14          # DEM10B は z=14 まで (約 7.8 m/px @ 北緯 35°)


def lonlat_to_pixel(lon, lat, z):
    """経度緯度 → Web メルカトルのピクセル座標 (z ズームでの全体ピクセル系)"""
    n = 2 ** z * TILE
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - math.log(math.tan(math.radians(lat)) + 1.0 / math.cos(math.radians(lat))) / math.pi) / 2.0 * n
    return x, y


def meters_per_pixel(lat, z):
    """Web メルカトルは等角なので、この緯度ではピクセルは縦横同じ長さ [m]"""
    return 156543.03392 * math.cos(math.radians(lat)) / (2 ** z)


def fetch_tile(z, tx, ty):
    """標高タイル (CSV, 'e' = データなし) を取得して 256×256 の float リストにする。キャッシュあり"""
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f'dem_{z}_{tx}_{ty}.txt')
    if not os.path.exists(path):
        url = f'https://cyberjapandata.gsi.go.jp/xyz/dem/{z}/{tx}/{ty}.txt'
        req = urllib.request.Request(url, headers={'User-Agent': 'water_from_mountain/1.0'})
        try:
            data = urllib.request.urlopen(req, timeout=30).read().decode()
        except Exception as e:
            print(f'  tile {tx},{ty}: not available ({e}) → 0 m')
            data = '\n'.join(','.join(['e'] * TILE) for _ in range(TILE))
        open(path, 'w', encoding='utf-8').write(data)
        print(f'  downloaded {url}')
    rows = open(path, encoding='utf-8').read().strip().split('\n')
    grid = []
    for r in rows:
        grid.append([None if v == 'e' else float(v) for v in r.split(',')])
    return grid


def build(lat, lon, size_km, exaggeration, grid_n, out_path, emit_lat=None, emit_lon=None):
    mpp = meters_per_pixel(lat, ZOOM)
    half_px = size_km * 1000.0 / 2.0 / mpp
    cx, cy = lonlat_to_pixel(lon, lat, ZOOM)
    px0, py0 = cx - half_px, cy - half_px          # 北西の角 (ピクセル)
    px1, py1 = cx + half_px, cy + half_px          # 南東の角
    tx0, ty0 = int(px0 // TILE), int(py0 // TILE)
    tx1, ty1 = int(px1 // TILE), int(py1 // TILE)
    print(f'center ({lat}, {lon}), {size_km} km, {mpp:.2f} m/px, tiles x {tx0}..{tx1}, y {ty0}..{ty1}')

    # タイルを結合
    W = (tx1 - tx0 + 1) * TILE; H = (ty1 - ty0 + 1) * TILE
    mosaic = [[None] * W for _ in range(H)]
    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            g = fetch_tile(ZOOM, tx, ty)
            oy, ox = (ty - ty0) * TILE, (tx - tx0) * TILE
            for r in range(TILE):
                mosaic[oy + r][ox:ox + TILE] = g[r]

    # データなし ('e') を周囲の平均で埋める (海・データ欠損)
    valid = [v for row in mosaic for v in row if v is not None]
    fill = min(valid) if valid else 0.0
    for r in range(H):
        for c in range(W):
            if mosaic[r][c] is None:
                mosaic[r][c] = fill

    # 双線形補間でサンプリングする関数
    def sample(px, py):
        fx, fy = px - tx0 * TILE, py - ty0 * TILE
        fx = min(max(fx, 0.0), W - 1.001); fy = min(max(fy, 0.0), H - 1.001)
        ix, iy = int(fx), int(fy); tx_, ty_ = fx - ix, fy - iy
        a = mosaic[iy][ix]; b = mosaic[iy][ix + 1]; c = mosaic[iy + 1][ix]; d = mosaic[iy + 1][ix + 1]
        return (a * (1 - tx_) + b * tx_) * (1 - ty_) + (c * (1 - tx_) + d * tx_) * ty_

    # 出力格子 (Blender 座標: x 東 [m], y 北 [m], z 上 [m])
    size_m = size_km * 1000.0
    elev = [[0.0] * grid_n for _ in range(grid_n)]
    for j in range(grid_n):          # j: 南 → 北
        for i in range(grid_n):      # i: 西 → 東
            px = px0 + (px1 - px0) * i / (grid_n - 1)
            py = py1 - (py1 - py0) * j / (grid_n - 1)   # 画像は北が上なので反転
            elev[j][i] = sample(px, py)
    emin = min(min(r) for r in elev); emax = max(max(r) for r in elev)
    print(f'elevation {emin:.1f} .. {emax:.1f} m (relief {emax - emin:.1f} m)')

    # OBJ 出力 (Blender の Forward -Z / Up Y エクスポートと同じ変換)
    lines = [f'# GSI DEM10B terrain: center=({lat},{lon}) size={size_km}km exaggeration={exaggeration}',
             '# 出典: 国土地理院 標高タイル (DEM10B)', 'o GsiTerrain']
    h = size_m / (grid_n - 1)
    for j in range(grid_n):
        for i in range(grid_n):
            x = -size_m / 2 + h * i
            y = -size_m / 2 + h * j
            z = (elev[j][i] - emin) * exaggeration
            lines.append(f'v {x:.2f} {z:.2f} {-y:.2f}')
    tiles = size_km * 1.4     # テクスチャの繰り返し (約 700 m ごと)
    for j in range(grid_n):
        for i in range(grid_n):
            lines.append(f'vt {i / (grid_n - 1) * tiles:.4f} {j / (grid_n - 1) * tiles:.4f}')
    for j in range(grid_n):
        for i in range(grid_n):
            i0, i1 = max(i - 1, 0), min(i + 1, grid_n - 1)
            j0, j1 = max(j - 1, 0), min(j + 1, grid_n - 1)
            zx = (elev[j][i1] - elev[j][i0]) * exaggeration / (h * (i1 - i0))
            zy = (elev[j1][i] - elev[j0][i]) * exaggeration / (h * (j1 - j0))
            nx, ny, nz = -zx, -zy, 1.0
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            lines.append(f'vn {nx / l:.4f} {nz / l:.4f} {-ny / l:.4f}')
    lines.append('s 1')
    n = grid_n
    for j in range(grid_n - 1):
        for i in range(grid_n - 1):
            a = j * n + i + 1; b = a + 1; c = a + n + 1; d = a + n
            lines.append(f'f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c} {d}/{d}/{d}')
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

    # 投入位置 → プログラム内座標 (x: 東が -x, z: 北が -z。60 m に正規化)
    cfg_path = os.path.join(os.path.dirname(out_path), 'terrain.cfg')
    if emit_lat is not None and emit_lon is not None:
        ex, ey = lonlat_to_pixel(emit_lon, emit_lat, ZOOM)
        u = (ex - px0) / (px1 - px0)          # 0 (西) .. 1 (東)
        v = (ey - py0) / (py1 - py0)          # 0 (北) .. 1 (南)
        emit_x = -(u - 0.5) * 60.0            # プログラムは x を反転する (東 = -x)
        emit_z = (v - 0.5) * 60.0             # 北 = -z
        with open(cfg_path, 'w', encoding='utf-8') as f:
            f.write('# 粒子の投入位置 (tools/fetch_gsi_terrain.py が生成)\n')
            f.write(f'# emit lat/lon = ({emit_lat}, {emit_lon})\n')
            f.write(f'emit_x = {emit_x:.2f}\nemit_z = {emit_z:.2f}\n')
        print(f'emit point ({emit_lat}, {emit_lon}) -> program (x={emit_x:.1f}, z={emit_z:.1f}) written to terrain.cfg')
    elif os.path.exists(cfg_path):
        os.remove(cfg_path)
        print('removed old terrain.cfg (auto emitter)')

    # プログラム内でのスケール (xz 60 m に一様縮小) を表示
    s = 60.0 / size_m
    print(f'wrote {out_path}: {grid_n}x{grid_n} vertices')
    print(f'in-program scale {s:.4f}: relief {(emax - emin) * exaggeration * s:.1f} m over 60 m '
          f'(1 particle = {0.3 / s:.0f} m real)')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='国土地理院 標高タイルから地形 OBJ を生成')
    ap.add_argument('--lat', type=float, default=35.3646, help='中心の緯度 (既定: 関ヶ原駅)')
    ap.add_argument('--lon', type=float, default=136.4667, help='中心の経度')
    ap.add_argument('--size', type=float, default=5.0, help='一辺 [km]')
    ap.add_argument('--exaggeration', type=float, default=2.5, help='高さの誇張倍率')
    ap.add_argument('--grid', type=int, default=241, help='1 辺の頂点数')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets', 'terrain.obj'))
    ap.add_argument('--emit-lat', type=float, default=None, help='投入位置の緯度 (省略時は自動)')
    ap.add_argument('--emit-lon', type=float, default=None, help='投入位置の経度')
    a = ap.parse_args()
    build(a.lat, a.lon, a.size, a.exaggeration, a.grid, a.out, a.emit_lat, a.emit_lon)
