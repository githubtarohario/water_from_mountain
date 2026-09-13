# =============================================================================
# blender_export_valley.py
#   Blender 内で実行して「試作用の谷」メッシュを作り、assets/terrain.obj に書き出すスクリプト。
#
#   使い方:
#     1. Blender を起動し、[Scripting] ワークスペースでこのファイルを開いて [Run Script]
#        または コマンドライン:  blender -b -P tools/blender_export_valley.py
#     2. assets/terrain.obj が生成されるので、MountainParticles.exe を起動すると
#        自動的にこの地形が読み込まれる (タイトルバーに「地形: OBJ」と表示)
#
#   自作の地形を使う場合:
#     ・任意のメッシュを選択し、OBJ エクスポートで「Forward: -Z, Up: Y」を指定して
#       assets/terrain.obj に保存するだけでよい (このスクリプトは不要)
#     ・Blender の上面図で「上 (+Y)」が上流 (奥)、「下 (-Y)」が下流 (手前) になる
#     ・大きさは自動で 60 m 四方にフィットされる (高さも同じ倍率で拡縮)
#     ・水は上流端 (Blender の +Y 端から 2.5 m 内側) の最も低い点に投入される
#
#   座標系: Blender は Z が上・右手系。OBJ 出力時に Y を上に変換し、
#           プログラム側で x を反転して DirectX の左手系に合わせる。
# =============================================================================
import bpy
import bmesh
import math
import os

# ---- 地形パラメータ ----
SIZE = 60.0          # 一辺の長さ [m]
SEGMENTS = 120       # 分割数 (頂点は 121 x 121)
UV_TILES = 7.0       # テクスチャの繰り返し回数


def smoothstep(e0, e1, x):
    """e0 以下で 0、e1 以上で 1、その間を滑らかにつなぐ"""
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def rough_noise(x, y):
    """正弦波の重ね合わせによる簡易ノイズ (-1～1 程度)"""
    n = 0.0
    n += 0.50 * math.sin(0.35 * x + 1.3) * math.sin(0.27 * y + 0.4)
    n += 0.30 * math.sin(0.71 * x + 2.1) * math.sin(0.63 * y + 1.9)
    n += 0.15 * math.sin(1.53 * x + 0.2) * math.sin(1.37 * y + 2.7)
    n += 0.05 * math.sin(3.10 * x + 1.1) * math.sin(2.90 * y + 0.9)
    return n


def valley_height(x, y):
    """
    Blender 座標 (x: 左右, y: 奥行き=+y が上流) に対する高さ z を返す。
    手続き生成版と区別できるよう、蛇行の形を変え、途中に 2 段の落差 (滝) を入れている。
    """
    c = 5.0 * math.sin(y * 0.12) + 2.5 * math.sin(y * 0.30 + 0.8)   # 谷の中心線
    d = abs(x - c)
    w = smoothstep(2.0, 9.0, d)                                        # 0: 谷底, 1: 山
    base = 0.18 * y                                                    # 上流 (+y) が高い
    steps = 2.0 * smoothstep(-0.6, 0.6, y - 10.0) + 2.0 * smoothstep(-0.6, 0.6, y + 8.0)  # 2 段の滝
    floor_u = 0.25 * min(d, 2.0) ** 2                                  # U 字断面
    mountain = 10.0 * w
    rough = rough_noise(x, y) * (0.2 + 4.0 * w)
    return base + steps + floor_u + mountain + rough


def build_mesh():
    """谷の高さ関数から Blender メッシュオブジェクトを作る"""
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")

    verts = []
    for j in range(SEGMENTS + 1):
        row = []
        for i in range(SEGMENTS + 1):
            x = -SIZE / 2 + SIZE * i / SEGMENTS
            y = -SIZE / 2 + SIZE * j / SEGMENTS
            row.append(bm.verts.new((x, y, valley_height(x, y))))
        verts.append(row)
    bm.verts.ensure_lookup_table()

    for j in range(SEGMENTS):
        for i in range(SEGMENTS):
            face = bm.faces.new((verts[j][i], verts[j][i + 1], verts[j + 1][i + 1], verts[j + 1][i]))
            for loop in face.loops:
                vx, vy, _ = loop.vert.co
                loop[uv_layer].uv = ((vx + SIZE / 2) / SIZE * UV_TILES, (vy + SIZE / 2) / SIZE * UV_TILES)

    bm.normal_update()
    mesh = bpy.data.meshes.new("ValleyTerrain")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new("ValleyTerrain", mesh)
    bpy.context.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj


def export_obj(obj, filepath):
    """Blender のバージョン差を吸収して OBJ を書き出す (Forward -Z, Up Y)"""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if hasattr(bpy.ops.wm, "obj_export"):        # Blender 3.3 以降
        bpy.ops.wm.obj_export(filepath=filepath, export_selected_objects=True,
                              forward_axis="NEGATIVE_Z", up_axis="Y",
                              export_materials=False, export_normals=True, export_uv=True)
    else:                                          # Blender 2.8 ～ 3.2
        bpy.ops.export_scene.obj(filepath=filepath, use_selection=True,
                                 axis_forward="-Z", axis_up="Y",
                                 use_materials=False, use_normals=True, use_uvs=True)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
    out_path = os.path.normpath(os.path.join(here, "..", "assets", "terrain.obj"))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    terrain = build_mesh()
    export_obj(terrain, out_path)
    print("exported:", out_path)
