# =============================================================================
# build_flowchart.py
#   main 関数 (wWinMain) から始まる処理の流れをフローチャート (SVG) として描き、
#   Chrome ヘッドレスで PDF (docs/フローチャート.pdf) に出力する。
#
#   使い方: python tools/build_flowchart.py
# =============================================================================
import os, subprocess, shutil, html

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ---------------------------------------------------------------- 図形描画
CW, RH = 150, 42          # ノードの幅 / 高さ
GX, GY = 200, 74          # 格子の間隔 (列 / 行)

def esc(s): return html.escape(s)

class Chart:
    """格子座標 (col, row) にノードを置き、辺で結ぶ簡易フローチャート"""
    def __init__(self, cols, rows, colw=GX, rowh=GY, nodew=CW):
        self.cols, self.rows, self.colw, self.rowh, self.nodew = cols, rows, colw, rowh, nodew
        self.nodes = {}; self.items = []
    def pos(self, c, r):
        return 40 + c * self.colw + self.nodew / 2, 30 + r * self.rowh + RH / 2
    def node(self, nid, c, r, text, kind='proc', w=None, fill=None):
        self.nodes[nid] = (c, r, kind, w or self.nodew)
        x, y = self.pos(c, r); w = w or self.nodew
        lines = text.split('\n')
        colors = {'proc': ('#eef3fa', '#2b5797'), 'start': ('#dff0d8', '#5a8a2c'), 'end': ('#fbe9d0', '#c77'),
                  'dec': ('#fff8e6', '#d09a00'), 'sub': ('#f3f0fa', '#6a4fa0'), 'io': ('#e8f4f8', '#2a7a9a')}
        f, st = colors[kind]
        if fill: f = fill
        if kind in ('start', 'end'):
            shape = f'<rect x="{x-w/2}" y="{y-RH/2}" width="{w}" height="{RH}" rx="21" fill="{f}" stroke="{st}" stroke-width="1.5"/>'
        elif kind == 'dec':
            hw, hh = w / 2 + 14, RH / 2 + 8
            shape = f'<polygon points="{x},{y-hh} {x+hw},{y} {x},{y+hh} {x-hw},{y}" fill="{f}" stroke="{st}" stroke-width="1.5"/>'
        elif kind == 'sub':
            shape = (f'<rect x="{x-w/2}" y="{y-RH/2}" width="{w}" height="{RH}" fill="{f}" stroke="{st}" stroke-width="1.5"/>'
                     f'<line x1="{x-w/2+6}" y1="{y-RH/2}" x2="{x-w/2+6}" y2="{y+RH/2}" stroke="{st}"/><line x1="{x+w/2-6}" y1="{y-RH/2}" x2="{x+w/2-6}" y2="{y+RH/2}" stroke="{st}"/>')
        elif kind == 'io':
            sk = 10
            shape = f'<polygon points="{x-w/2+sk},{y-RH/2} {x+w/2},{y-RH/2} {x+w/2-sk},{y+RH/2} {x-w/2},{y+RH/2}" fill="{f}" stroke="{st}" stroke-width="1.5"/>'
        else:
            shape = f'<rect x="{x-w/2}" y="{y-RH/2}" width="{w}" height="{RH}" rx="4" fill="{f}" stroke="{st}" stroke-width="1.5"/>'
        fs = 10.5 if len(lines) <= 2 else 9.5
        ty = y - (len(lines) - 1) * (fs + 2) / 2 + 4
        txt = ''.join(f'<text x="{x}" y="{ty + i*(fs+2)}" text-anchor="middle" font-size="{fs}" font-weight="{"bold" if i == 0 and len(lines) > 1 else "normal"}">{esc(l)}</text>' for i, l in enumerate(lines))
        self.items.append(shape + txt)
    def edge(self, a, b, label='', side=None, dx=0):
        """a → b の矢印。同じ列なら縦、違う列なら L 字で結ぶ。side='right'/'left' で回り込み"""
        ca, ra, ka, wa = self.nodes[a]; cb, rb, kb, wb = self.nodes[b]
        ax, ay = self.pos(ca, ra); bx, by = self.pos(cb, rb)
        ha = RH / 2 + (8 if ka == 'dec' else 0); hb = RH / 2 + (8 if kb == 'dec' else 0)
        wa2 = wa / 2 + (14 if ka == 'dec' else 0)
        if side == 'right' or side == 'left':
            # 横に出て縦に進み、目的ノードの横から入る
            sgn = 1 if side == 'right' else -1
            x1 = ax + sgn * wa2; xm = ax + sgn * (wa2 + 22 + dx)
            xb = bx + sgn * (wb / 2 + (14 if kb == 'dec' else 0))
            if cb == ca:
                path = f'M{x1},{ay} H{xm} V{by} H{xb}'
            else:
                path = f'M{x1},{ay} H{xm} V{by} H{xb}'
            lx, ly = xm + sgn * 4, (ay + by) / 2
            anchor = 'start' if sgn > 0 else 'end'
        elif ca == cb:
            if by > ay: path = f'M{ax},{ay+ha} V{by-hb}'; lx, ly, anchor = ax + 6, (ay + ha + by - hb) / 2 + 4, 'start'
            else:       path = f'M{ax},{ay-ha} V{by+hb}'; lx, ly, anchor = ax + 6, (ay + by) / 2, 'start'
        elif ra == rb:
            sgn = 1 if bx > ax else -1
            path = f'M{ax+sgn*wa2},{ay} H{bx-sgn*(wb/2 + (14 if kb=="dec" else 0))}'
            lx, ly, anchor = (ax + bx) / 2, ay - 6, 'middle'
        else:
            # 横に出てから縦に降りる
            sgn = 1 if bx > ax else -1
            path = f'M{ax+sgn*wa2},{ay} H{bx} V{by-hb if by > ay else by+hb}'
            lx, ly, anchor = (ax + sgn * wa2 + bx) / 2, ay - 6, 'middle'
        self.items.append(f'<path d="{path}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
        if label:
            self.items.append(f'<text x="{lx}" y="{ly}" text-anchor="{anchor}" font-size="9.5" fill="#333">{esc(label)}</text>')
    def note(self, c, r, text, w=None, dx=0, dy=0, size=9.5, color='#555'):
        x, y = self.pos(c, r)
        lines = text.split('\n')
        self.items.append(''.join(f'<text x="{x - self.nodew/2 + dx}" y="{y - RH/2 + 12 + dy + i*(size+3)}" font-size="{size}" fill="{color}">{esc(l)}</text>' for i, l in enumerate(lines)))
    def group(self, c0, r0, c1, r1, title, pad=10):
        x0, y0 = self.pos(c0, r0); x1, y1 = self.pos(c1, r1)
        x0 -= self.nodew / 2 + pad; y0 -= RH / 2 + pad + 14; x1 += self.nodew / 2 + pad; y1 += RH / 2 + pad
        self.items.insert(0, f'<rect x="{x0}" y="{y0}" width="{x1-x0}" height="{y1-y0}" rx="8" fill="#fafbfd" stroke="#aab" stroke-dasharray="5,3"/><text x="{x0+8}" y="{y0+13}" font-size="10" fill="#557" font-weight="bold">{esc(title)}</text>')
    def svg(self, w=None, h=None):
        w = w or 40 * 2 + (self.cols - 1) * self.colw + self.nodew
        h = h or 30 * 2 + (self.rows - 1) * self.rowh + RH
        return (f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="\'Yu Gothic UI\', Meiryo, sans-serif">'
                f'<defs><marker id="ar" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#444"/></marker></defs>'
                + ''.join(self.items) + '</svg>')

# ---------------------------------------------------------------- 図 1: 全体像
def chart_overview():
    c = Chart(3, 12, colw=230, nodew=170)
    c.node('s', 1, 0, 'wWinMain 開始', 'start')
    c.node('win', 1, 1, 'ウィンドウ作成\nRegisterClassEx / CreateWindowEx')
    c.node('dir', 1, 2, 'shaders / assets\nフォルダは見つかった?', 'dec')
    c.node('err', 2, 2, 'メッセージボックス\n→ return 1', 'end', w=150)
    c.node('gfx', 1, 3, 'Graphics::Initialize\nD3D11 デバイス / スワップチェーン', 'sub')
    c.node('cb', 1, 4, 'CreateConstantBuffer\nFrameConstants (b0)')
    c.node('ter', 1, 5, 'Terrain::Initialize\n地形 (→ 図 2)', 'sub')
    c.node('sph', 1, 6, 'SPH::Initialize\n粒子配列 / ハッシュ表 / カーネル係数', 'sub')
    c.node('fr', 1, 7, 'FluidRenderer::Initialize\n粒子バッファ / シェーダー / 深度テクスチャ', 'sub')
    c.node('show', 1, 8, 'ShowWindow\ninitialized = true')
    c.node('loop', 1, 9, 'メインループ\n(→ 図 3)', 'sub')
    c.node('e', 1, 10, '終了 (WM_QUIT)', 'end')
    for a, b in [('s','win'),('win','dir'),('gfx','cb'),('cb','ter'),('ter','sph'),('sph','fr'),('fr','show'),('show','loop'),('loop','e')]:
        c.edge(a, b)
    c.edge('dir', 'gfx', 'はい')
    c.edge('dir', 'err', 'いいえ')
    c.note(0, 3, '各 Initialize が false を返したら\nreturn 1 で終了', dy=4)
    c.note(0, 9, 'PeekMessage で入力を処理し、\n無ければ 1 フレーム進める', dy=4)
    c.note(2, 5, '手続き生成 or\nassets/terrain.obj', dy=4)
    c.note(2, 7, 'Particle.hlsl\nFluidSurface.hlsl', dy=4)
    return c.svg()

# ---------------------------------------------------------------- 図 2: Terrain::Initialize
def chart_terrain_init():
    c = Chart(3, 10, colw=230, nodew=170)
    c.node('s', 1, 0, 'Terrain::Initialize', 'start')
    c.node('obj', 1, 1, 'assets/terrain.obj を\nMeshLoader::LoadObj', 'dec')
    c.node('flip', 0, 2, 'x 反転 (右手系→左手系)\n自動フィット 60 m / 巻き順統一')
    c.node('bake', 0, 3, 'BakeHeightMap\nメッシュを高さマップに焼き込み')
    c.node('gpu1', 0, 4, 'CreateGpuBuffers\nOBJ の頂点 / 法線 / UV')
    c.node('hm', 2, 2, 'BuildHeightMap\nHeightFunction を 257×257 で評価')
    c.node('mesh', 2, 3, 'BuildMesh\n頂点 / 法線 / UV / インデックス')
    c.node('gpu2', 2, 4, 'CreateGpuBuffers')
    c.node('tex', 1, 5, 'CreateRockTexture', 'sub')
    c.node('img', 1, 6, 'assets/rock.png 等を\nTextureLoader::LoadFromFile', 'dec')
    c.node('proc', 2, 7, 'CreateProceduralRockTexture\nノイズで 1024×1024 生成')
    c.node('sh', 1, 8, 'CreateShaders\nTerrain.hlsl (VS / PS / 入力レイアウト)')
    c.node('e', 1, 9, '戻る (true)', 'end')
    c.edge('s', 'obj')
    c.edge('obj', 'flip', '成功'); c.edge('obj', 'hm', '失敗 / なし')
    c.edge('flip', 'bake'); c.edge('bake', 'gpu1'); c.edge('hm', 'mesh'); c.edge('mesh', 'gpu2')
    c.edge('gpu1', 'tex'); c.edge('gpu2', 'tex')
    c.edge('tex', 'img'); c.edge('img', 'sh', '成功'); c.edge('img', 'proc', '失敗')
    c.edge('proc', 'sh'); c.edge('sh', 'e')
    c.group(0, 2, 0, 4, 'A. OBJ 地形 (Blender)')
    c.group(2, 2, 2, 4, 'B. 手続き生成')
    return c.svg()

# ---------------------------------------------------------------- 図 3: メインループ / 1 フレーム
def chart_mainloop():
    c = Chart(3, 13, colw=230, nodew=170)
    c.node('s', 1, 0, 'メインループ開始', 'start')
    c.node('peek', 1, 1, 'PeekMessage\nメッセージあり?', 'dec')
    c.node('disp', 2, 1, 'TranslateMessage\nDispatchMessage → WndProc (→ 図 4)', 'sub', w=190)
    c.node('quit', 2, 2, 'WM_QUIT ?', 'dec', w=110)
    c.node('end', 2, 3, 'ループ終了', 'end', w=110)
    c.node('dt', 1, 2, '経過時間 dt を計測\nsteady_clock')
    c.node('pause', 1, 3, '一時停止中?', 'dec')
    c.node('upd', 0, 4, 'SPH::Update(dt)\n物理を進める (→ 図 5)', 'sub')
    c.node('begin', 1, 5, 'Graphics::BeginFrame\n白でクリア / RTV + DSV 設定')
    c.node('ufc', 1, 6, 'UpdateFrameConstants\nView / Proj / 光源 → b0')
    c.node('ter', 1, 7, 'Terrain::Render\n地形を DrawIndexed')
    c.node('rd', 1, 8, 'SPH::GetRenderData\n位置 + 平滑化した速さ', 'io')
    c.node('fr', 1, 9, 'FluidRenderer::Render\n粒子 or 表面生成 (→ 図 6)', 'sub')
    c.node('pres', 1, 10, 'Graphics::EndFrame\nPresent (vsync)')
    c.node('fps', 1, 11, 'FPS 計測 (0.5 s ごと)\nタイトル更新 (0.25 s ごと)')
    c.edge('s', 'peek')
    c.edge('peek', 'disp', 'あり'); c.edge('disp', 'quit'); c.edge('quit', 'end', 'はい')
    c.edge('quit', 'peek', 'いいえ', side='right')
    c.edge('peek', 'dt', 'なし')
    c.edge('dt', 'pause'); c.edge('pause', 'upd', 'いいえ'); c.edge('upd', 'begin'); c.edge('pause', 'begin', 'はい')
    for a, b in [('begin','ufc'),('ufc','ter'),('ter','rd'),('rd','fr'),('fr','pres'),('pres','fps')]:
        c.edge(a, b)
    c.edge('fps', 'peek', '次のフレーム', side='right', dx=30)
    c.group(1, 5, 1, 10, '描画 (1 フレーム)')
    return c.svg()

# ---------------------------------------------------------------- 図 4: WndProc
def chart_wndproc():
    c = Chart(3, 10, colw=230, nodew=170)
    c.node('s', 0, 0, 'WndProc (メッセージ受信)', 'start')
    c.node('sw', 0, 1, 'メッセージの種類', 'dec')
    items = [
        ('WM_LBUTTONDOWN / RBUTTONDOWN', '押下フラグ ON, 位置記憶\nSetCapture'),
        ('WM_LBUTTONUP / RBUTTONUP', '押下フラグ OFF\nReleaseCapture'),
        ('WM_MOUSEMOVE', '左: Camera::Rotate\n右: Camera::Pan'),
        ('WM_MOUSEWHEEL', 'Camera::Zoom (0.9 / 1.1)'),
        ('WM_KEYDOWN 1 / 2', 'mode = Particles / Surface'),
        ('WM_KEYDOWN Space / R', 'paused 反転 / SPH::Reset'),
        ('WM_KEYDOWN Esc, WM_DESTROY', 'PostQuitMessage'),
        ('WM_SIZE', 'Graphics::Resize\nFluidRenderer::OnResize'),
    ]
    for i, (k, v) in enumerate(items):
        c.node(f'k{i}', 1, 2 + i, k, 'io', w=190)
        c.node(f'v{i}', 2, 2 + i, v)
        c.edge(f'k{i}', f'v{i}')
    c.edge('s', 'sw')
    # 分岐: sw から各 k へ縦線 + 横線
    x, y = c.pos(0, 1); xk, _ = c.pos(1, 2)
    c.items.append(f'<path d="M{x},{y+RH/2+8} V{c.pos(0, 9)[1]}" fill="none" stroke="#444" stroke-width="1.4"/>')
    for i in range(len(items)):
        _, yi = c.pos(1, 2 + i)
        c.items.append(f'<path d="M{x},{yi} H{xk - 95 + 6}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
    c.node('def', 0, 9, 'その他: DefWindowProc', 'end')
    c.note(0, 3, '初期化前の WM_SIZE は\ninitialized フラグで無視', dy=4)
    return c.svg()

# ---------------------------------------------------------------- 図 5: SPH::Update / Step
def chart_sph():
    c = Chart(3, 13, colw=230, nodew=180)
    c.node('s', 1, 0, 'SPH::Update(frameTime)', 'start')
    c.node('n', 1, 1, 'substeps = ceil(frameTime / Δt)\n上限 maxSubsteps (=5) でクランプ')
    c.node('loop', 1, 2, '残りサブステップ > 0 ?', 'dec')
    c.node('emit', 1, 3, 'Emit(Δt)\n投入タイマー ≥ s / v_emit ごとに\nEmitSheet (14×4 の板を上流に配置)')
    c.node('c0', 1, 4, '粒子数 > 0 ?', 'dec')
    c.node('grid', 1, 5, 'BuildGrid\nセル座標 → ハッシュ → カウンティングソート')
    c.node('nl', 1, 6, 'BuildNeighborLists   ‖ 並列\n27 セル走査, r² < h² を最大 96 個記録')
    c.node('dp', 1, 7, 'ComputeDensityPressure   ‖ 並列\nρ = Σ m W_poly6,  p = k·max(ρ−ρ0, 0)')
    c.node('f', 1, 8, 'ComputeForces   ‖ 並列\n圧力力 + 粘性力 → 加速度クランプ → +重力')
    c.node('it', 1, 9, 'Integrate   ‖ 並列\nv += aΔt (速度クランプ), x += vΔt\n→ ResolveCollision (図 5-1)')
    c.node('rm', 1, 10, 'RemoveDrained\n下流端 z > 29.5 の粒子を末尾と入れ替えて削除')
    c.node('t', 1, 11, 'time += Δt')
    c.node('e', 1, 12, '戻る', 'end')
    c.edge('s', 'n'); c.edge('n', 'loop'); c.edge('loop', 'emit', 'はい'); c.edge('emit', 'c0')
    c.edge('c0', 'grid', 'はい')
    for a, b in [('grid','nl'),('nl','dp'),('dp','f'),('f','it'),('it','rm'),('rm','t')]:
        c.edge(a, b)
    c.edge('c0', 't', 'いいえ', side='left')
    c.edge('t', 'loop', '次のサブステップ', side='right', dx=20)
    c.edge('loop', 'e', 'いいえ', side='left', dx=40)
    c.note(2, 5, '空間ハッシュ:\nkey = (cx·73856093 ⊕ cy·19349663\n ⊕ cz·83492791) & mask', dy=-2, size=9)
    c.note(2, 7, 'W_poly6 = 315/(64πh⁹)(h²−r²)³', dy=8, size=9)
    c.note(2, 8, '∇W_spiky = −45/(πh⁶)(h−r)² r̂\n∇²W_visc = 45/(πh⁶)(h−r)', dy=2, size=9)
    c.note(2, 9, 'シンプレクティック・オイラー法', dy=8, size=9)
    c.note(0, 6, '‖ = OpenMP parallel for\n(粒子ごとに独立)', dy=4, size=9)
    return c.svg()

# ---------------------------------------------------------------- 図 5-1: ResolveCollision
def chart_collision():
    c = Chart(3, 10, colw=230, nodew=180)
    c.node('s', 1, 0, 'ResolveCollision(i)', 'start')
    c.node('wall', 1, 1, '側壁 x = ±29.5 / 上流壁 z = −29.5\n越えていれば壁面に戻し、法線速度を 0')
    c.node('h', 1, 2, 'floorY = Terrain::GetHeight(x, z) + 半径')
    c.node('q1', 1, 3, 'y < floorY ?\n(地面にめり込んだ)', 'dec')
    c.node('push', 2, 4, 'y = floorY\nn = Terrain::GetNormal(x, z)')
    c.node('q2', 2, 5, 'v·n < 0 ?', 'dec', w=120)
    c.node('refl', 2, 6, 'v −= (1+e)(v·n) n\nめり込む速度成分を除去')
    c.node('fric', 2, 7, 'v *= (1 − friction)')
    c.node('cz', 1, 8, '高さ < contactZone (0.3 m) ?', 'dec')
    c.node('cd', 0, 8, 'v *= 1 − contactDamping·t\nt = 1 − 高さ / contactZone', w=190)
    c.node('sp', 1, 9, '近傍数 < sparseNeighbors (8) ?', 'dec')
    c.node('ad', 0, 9, 'v *= 1 − airDrag\n(孤立した飛沫を減速)', w=190)
    c.node('e', 2, 9, '戻る', 'end', w=110)
    c.edge('s', 'wall'); c.edge('wall', 'h'); c.edge('h', 'q1')
    c.edge('q1', 'push', 'はい'); c.edge('push', 'q2'); c.edge('q2', 'refl', 'はい'); c.edge('refl', 'fric')
    c.edge('q2', 'fric', 'いいえ', side='right')
    c.edge('fric', 'cz'); c.edge('q1', 'cz', 'いいえ')
    c.edge('cz', 'cd', 'はい'); c.edge('cz', 'sp', 'いいえ')
    x0, y0 = c.pos(0, 8); x1, y1 = c.pos(1, 9)
    ym = (y0 + y1) / 2
    c.items.append(f'<path d="M{x0},{y0+RH/2} V{ym} H{x1} V{y1-RH/2-8}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
    c.edge('sp', 'ad', 'はい'); c.edge('sp', 'e', 'いいえ')
    xa, ya = c.pos(0, 9); xe, ye = c.pos(2, 9)
    c.items.append(f'<path d="M{xa},{ya+RH/2} V{ya+RH/2+18} H{xe} V{ye+RH/2}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
    return c.svg(h=30 * 2 + 9 * GY + RH + 24)

# ---------------------------------------------------------------- 図 6: FluidRenderer::Render
def chart_render():
    c = Chart(3, 12, colw=230, nodew=180)
    c.node('s', 1, 0, 'FluidRenderer::Render', 'start')
    c.node('up', 1, 1, 'UploadParticles\nMap / Unmap で StructuredBuffer へ', 'io')
    c.node('m', 1, 2, 'mode ?', 'dec', w=120)
    # 粒子モード
    c.node('p1', 0, 3, 'Params.x = 粒子半径 → b0')
    c.node('p2', 0, 4, 'RT = バックバッファ + 深度')
    c.node('p3', 0, 5, 'DrawSpheres (PSColor)\nDrawInstanced(4, N)\n球インポスター + 速度で着色')
    # 表面モード
    c.node('s1', 2, 3, 'Params.x = 表面用半径 (1.3 s) → b0')
    c.node('s2', 2, 4, 'depthTex[0] を 0 でクリア\nRT = depthTex[0] + メイン深度')
    c.node('s3', 2, 5, 'DrawSpheres (PSDepth)\nビュー空間 z を出力')
    c.node('s4', 2, 6, 'ぼかし反復 < 4 ?', 'dec')
    c.node('s5', 2, 7, 'PSBlur 横: depthTex 0 → 1\nPSBlur 縦: depthTex 1 → 0')
    c.node('s6', 2, 8, 'RT = バックバッファ (深度なし)\nアルファブレンド ON')
    c.node('s7', 2, 9, 'PSComposite (全画面三角形)\n深度 → 位置 → 法線 → 陰影, α = 0.9')
    c.node('s8', 2, 10, 'RT を バックバッファ + 深度 に戻す')
    c.node('e', 1, 11, '戻る', 'end')
    c.edge('s', 'up'); c.edge('up', 'm')
    c.edge('m', 'p1', 'Particles'); c.edge('m', 's1', 'Surface')
    c.edge('p1', 'p2'); c.edge('p2', 'p3')
    for a, b in [('s1','s2'),('s2','s3'),('s3','s4'),('s5','s6'),('s6','s7'),('s7','s8')]:
        c.edge(a, b)
    c.edge('s4', 's5', 'はい')
    c.edge('s5', 's4', '', side='right', dx=10)
    x, y = c.pos(2, 6); c.items.append(f'<text x="{x + c.nodew/2 + 40}" y="{y + 40}" font-size="9.5">繰り返し</text>')
    xs, ys = c.pos(2, 6); xt, yt = c.pos(2, 8)
    # 「いいえ」で s6 へ (左側を回る)
    c.items.append(f'<path d="M{xs - c.nodew/2 - 14},{ys} H{xs - c.nodew/2 - 34} V{yt} H{xt - c.nodew/2}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
    c.items.append(f'<text x="{xs - c.nodew/2 - 38}" y="{ys - 6}" text-anchor="end" font-size="9.5">いいえ</text>')
    xp, yp = c.pos(0, 5); xe, ye = c.pos(1, 11)
    c.items.append(f'<path d="M{xp},{yp+RH/2} V{ye} H{xe-c.nodew/2}" fill="none" stroke="#444" stroke-width="1.4" marker-end="url(#ar)"/>')
    c.edge('s8', 'e')
    c.group(0, 3, 0, 5, '粒子モード (キー 1)')
    c.group(2, 3, 2, 10, '表面生成モード (キー 2)')
    c.note(1, 5, 'PSColor / PSDepth はどちらも\nuv から球面を再構成し\nSV_Depth に深度を書く', dy=6, size=9)
    c.note(1, 8, 'バイラテラルぼかし:\n距離 σs と深度差 σd の\n重みで平滑化', dy=6, size=9)
    return c.svg()

# ---------------------------------------------------------------- HTML
CSS = '''
  @page { size: A4; margin: 16mm 14mm 16mm 14mm; }
  html { font-family: "Yu Gothic UI", "Yu Gothic", Meiryo, sans-serif; font-size: 10.5pt; line-height: 1.6; color: #222; }
  body { margin: 0; }
  h1 { font-size: 19pt; color: #1a3c6e; border-bottom: 3px solid #2b5797; padding-bottom: 4px; margin: 0 0 8px; page-break-before: always; }
  p { margin: 4px 0 8px; }
  .lead { background: #eef3fa; border-left: 5px solid #2b5797; padding: 6px 10px; margin: 6px 0 10px; font-size: 10pt; }
  .fig { text-align: center; margin: 6px 0; }
  .fig svg { max-width: 100%; max-height: 160mm; height: auto; }
  .legend { display: flex; gap: 14px; flex-wrap: wrap; font-size: 9.5pt; margin: 6px 0 10px; }
  .legend span { display: inline-flex; align-items: center; gap: 5px; }
  .legend i { display: inline-block; width: 26px; height: 14px; border: 1.5px solid; }
  table { border-collapse: collapse; width: 100%; font-size: 9.5pt; margin: 6px 0 10px; page-break-inside: avoid; }
  th, td { border: 1px solid #b8c4d6; padding: 3px 6px; vertical-align: top; }
  th { background: #e4ecf7; text-align: left; }
  code { font-family: Consolas, monospace; font-size: 9.3pt; background: #eef1f5; padding: 0 3px; }
  .cover { text-align: center; padding-top: 80mm; }
  .cover .t { font-size: 26pt; font-weight: bold; color: #1a3c6e; }
  .cover .s { font-size: 13pt; color: #444; margin: 10px 0 30px; }
  .cover .m { font-size: 10.5pt; color: #666; line-height: 2; }
'''

LEGEND = '''<div class="legend">
<span><i style="background:#dff0d8;border-color:#5a8a2c;border-radius:7px"></i>開始 / 終了</span>
<span><i style="background:#eef3fa;border-color:#2b5797"></i>処理</span>
<span><i style="background:#fff8e6;border-color:#d09a00;transform:rotate(45deg) scale(.7)"></i>判断</span>
<span><i style="background:#f3f0fa;border-color:#6a4fa0"></i>別の図で詳述する処理</span>
<span><i style="background:#e8f4f8;border-color:#2a7a9a;transform:skewX(-12deg)"></i>入出力 / データ</span>
</div>'''

def section(title, lead, svg, table_rows=None, first=False):
    t = ''
    if table_rows:
        t = '<table><tr><th style="width:30%">処理</th><th>役割 / 補足</th></tr>' + ''.join(f'<tr><td>{esc(a)}</td><td>{esc(b)}</td></tr>' for a, b in table_rows) + '</table>'
    return f'<h1>{esc(title)}</h1><div class="lead">{esc(lead)}</div>{LEGEND}<div class="fig">{svg}</div>{t}'

def main():
    body = '''<div class="cover"><div class="t">山から粒子が落ちる<br>処理フロー図</div>
<div class="s">main 関数 (wWinMain) から追う、初期化・メインループ・物理・描画の流れ</div>
<div class="m">対象: water_from_mountain (src/main.cpp ほか)<br>https://github.com/githubtarohario/water_from_mountain</div></div>'''
    body += section('図 1  全体の流れ — wWinMain', 'プログラムの入口。ウィンドウを作り、各モジュールを順に初期化してからメインループに入る。どこかの初期化が失敗すると return 1 で終了する。', chart_overview(), [
        ('FindProjectDirectories', 'カレント → exe → 親 → 親の親 の順に shaders\\Common.hlsli を探し、shaders と assets の場所を登録'),
        ('Graphics::Initialize', 'D3D11 デバイス・スワップチェーン・RTV/DSV・共通ステートを作成 (Graphics.cpp)'),
        ('Terrain::Initialize', 'OBJ 地形または手続き生成、岩テクスチャ、地形シェーダー (図 2)'),
        ('SPH::Initialize', 'SPHParams を受け取り、カーネル係数の事前計算と配列 (最大 12,000 粒子) を確保'),
        ('FluidRenderer::Initialize', 'StructuredBuffer、6 つのシェーダー、ピンポン用の深度テクスチャ ×2'),
    ], first=True)
    body += section('図 2  Terrain::Initialize — 地形の準備', '地形は「assets/terrain.obj があれば読み込み、無ければ式から生成」の 2 系統。どちらも最終的に高さマップ (衝突判定用) と GPU バッファ (描画用) を作る。テクスチャも画像 → ノイズ生成の順に試す。', chart_terrain_init(), [
        ('MeshLoader::LoadObj', 'v/vt/vn/f を解釈。多角形を三角形化し (v,vt,vn) の組ごとに頂点を共有'),
        ('BakeHeightMap', '三角形を真上から 257×257 格子に投影し、重心座標で高さを補間 (最大値採用)。未被覆点は近傍平均で穴埋め'),
        ('HeightFunction', '谷の中心線 (sin の和) からの距離で山を立ち上げ、傾斜・U 字断面・fBm・尾根ノイズを加算'),
        ('CreateRockTexture', 'rock.png → jpg → jpeg → bmp を WIC で読み込み。失敗時はノイズから 1024² を生成。ミップマップ付き'),
    ])
    body += section('図 3  メインループと 1 フレームの処理', 'メッセージがあれば処理し、無ければ 1 フレーム進める。物理 (SPH::Update) と描画 (Terrain / FluidRenderer) を毎フレーム呼び、最後に Present で画面に出す。', chart_mainloop(), [
        ('dt の計測', 'steady_clock の差分。SPH::Update はこの dt からサブステップ数を決める'),
        ('UpdateFrameConstants', 'View / Proj / ViewProj (転置)、カメラ位置、光源方向、画面サイズを定数バッファ b0 へ転送し VS/PS にバインド'),
        ('Terrain::Render', '頂点/インデックスバッファをセットし DrawIndexed。岩テクスチャを t0 にバインド'),
        ('SPH::GetRenderData', '(x, y, z, 速さ) の配列。速さは近傍平均で平滑化 (色のまだら防止)'),
        ('FluidRenderer::Render', 'mode に応じて粒子モード / 表面生成モードを実行 (図 6)'),
    ])
    body += section('図 4  WndProc — 入力とイベントの処理', 'OS から届くメッセージを種類ごとに振り分ける。マウスはカメラ操作、キーはモード切替・一時停止・リセット、WM_SIZE はバッファの再作成。', chart_wndproc(), [
        ('マウス左ドラッグ', 'Camera::Rotate(−dx·0.005, dy·0.005): 注視点まわりの回転 (pitch は ±89° にクランプ)'),
        ('マウス右ドラッグ', 'Camera::Pan(−dx·0.05, dy·0.05): 注視点をカメラの右・上方向へ移動'),
        ('ホイール', 'Camera::Zoom: 距離を 0.9 倍 / 1.1 倍 (3〜300 m)'),
        ('WM_SIZE', 'Graphics::Resize → RTV/DSV を作り直し、FluidRenderer::OnResize で深度テクスチャも再作成'),
    ])
    body += section('図 5  SPH::Update / Step — 物理シミュレーション', '1 フレームぶんを Δt = 0.003 s のサブステップに分け (最大 5 回)、各サブステップで「投入 → 近傍探索 → 密度・圧力 → 力 → 積分・衝突 → 排水」を実行する。粒子ごとに独立な段は OpenMP で並列化されている。', chart_sph(), [
        ('Emit / EmitSheet', '上流 z = −27.5 の谷底 (GetValleyCenterX) に 14×4 の粒子板を初速 4 m/s で投入。間隔は s / v_emit = 0.075 s'),
        ('BuildGrid', 'floor(p/h) でセル座標、素数ハッシュで表の添え字。カウンティングソートで同じセルの粒子を連続配置'),
        ('BuildNeighborLists', '周囲 27 セルを走査。ハッシュ衝突対策としてセル座標の一致も確認'),
        ('ComputeForces', '圧力力 (spiky 勾配)・粘性力 (visc ラプラシアン) の和を ρ で割り、加速度上限 250 でクランプ後に重力を加算'),
        ('RemoveDrained', '消す粒子を配列末尾と入れ替えて m_count を減らす O(1) 削除'),
    ])
    body += section('図 5-1  ResolveCollision — 粒子 1 個の境界処理', 'Integrate の中で粒子ごとに呼ばれる。地形の高さマップとの衝突に加え、谷から飛び出さないための接触ゾーン減衰と孤立粒子の空気抵抗を適用する。', chart_collision(), [
        ('地形との衝突', '地面の高さ + 半径より低ければ押し戻し、法線 n に対して v·n < 0 の成分を除去 (反発係数 e = 0)'),
        ('接触ゾーン減衰', '地面から 0.3 m 以内では、地面に近いほど強く減衰 (最下部 6%/ステップ)。壁に当たった粒子の跳ね上がりを抑える'),
        ('空気抵抗', '近傍が 8 個未満 = 流れから離れた飛沫。1.5%/ステップ減速して流れに戻す'),
    ])
    body += section('図 6  FluidRenderer::Render — 粒子の描画と表面生成', '粒子データを GPU に転送した後、粒子モードでは球インポスターを直接描き、表面生成モードでは「深度 → バイラテラルぼかし → 法線復元・陰影・合成」の 3 パスで水面を作る。', chart_render(), [
        ('DrawSpheres', '頂点バッファなしの DrawInstanced(4, N)。VS が SV_VertexID/SV_InstanceID からビルボードを生成'),
        ('PSColor / PSDepth', 'uv² > 1 を discard、法線 (u, v, −√(1−r²)) から球面上の点を求め SV_Depth に書く'),
        ('PSBlur', 'ワールド半径 0.9 m を画面に投影したピクセル数で、深度差 σd = 1.5 m のバイラテラル平滑化。横→縦を 4 回'),
        ('PSComposite', 'Proj._m00/_m11 で深度から位置を復元し、隣接差分の外積で法線。拡散 + Blinn-Phong + Schlick フレネル、α = 0.9'),
    ])
    doc = f'<!DOCTYPE html><html lang="ja"><head><meta charset="utf-8"><title>処理フロー図</title><style>{CSS}</style></head><body>{body}</body></html>'
    out_html = os.path.join(ROOT, 'docs', 'フローチャート.html')
    open(out_html, 'w', encoding='utf-8').write(doc)
    tmp = os.path.join(os.environ.get('TEMP', '.'), 'flow_out.pdf')
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--print-to-pdf=' + tmp,
                    'file:///' + out_html.replace('\\', '/')], check=True, timeout=180, capture_output=True)
    out_pdf = os.path.join(ROOT, 'docs', 'フローチャート.pdf')
    shutil.copy(tmp, out_pdf)
    print('PDF:', out_pdf, os.path.getsize(out_pdf))

if __name__ == '__main__':
    main()
