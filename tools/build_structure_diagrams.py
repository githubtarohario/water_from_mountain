# =============================================================================
# build_structure_diagrams.py
#   main 関数から始まる処理の流れを、フローチャート以外の 3 つの作図法で描き
#   PDF (docs/処理の流れ図.pdf) にする。
#     1. 呼び出しツリー図   … 関数の呼び出し階層 (木構造)
#     2. シーケンス図       … 時間軸に沿ったモジュール間のやり取り
#     3. データフロー図     … 処理ブロックとデータの流れ
#
#   使い方: python tools/build_structure_diagrams.py
# =============================================================================
import os, subprocess, shutil, html

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
def esc(s): return html.escape(s)
FONT = "font-family=\"'Yu Gothic UI', Meiryo, sans-serif\""

# ============================================================ 1. 呼び出しツリー図
# (深さ, 名前, 説明, 種別)  種別: m=main, g=Graphics, t=Terrain, s=SPH, r=Renderer, c=Camera, o=その他
def call_tree(items, title):
    ROWH, IND, W = 24, 26, 720
    h = 40 + ROWH * len(items) + 10
    col = {'m': '#2b5797', 'g': '#2a7a9a', 't': '#6a9a3c', 's': '#c0392b', 'r': '#6a4fa0', 'c': '#b8860b', 'o': '#555'}
    out = [f'<text x="10" y="20" font-size="12" font-weight="bold" fill="#1a3c6e">{esc(title)}</text>']
    # 縦線: 各ノードから、同じ深さの次の兄弟までを親の位置で結ぶ
    ys = [40 + i * ROWH for i in range(len(items))]
    for i, (d, name, desc, k) in enumerate(items):
        x = 14 + d * IND
        y = ys[i]
        if d > 0:
            # 親を探す
            j = i - 1
            while j >= 0 and items[j][0] >= d: j -= 1
            px = 14 + (d - 1) * IND + 6
            out.append(f'<path d="M{px},{ys[j]+6} V{y} H{x-2}" fill="none" stroke="#9ab" stroke-width="1.2"/>')
        # 印
        out.append(f'<circle cx="{x+6}" cy="{y}" r="4" fill="{col[k]}"/>')
        out.append(f'<text x="{x+16}" y="{y+4}" font-size="10.5" font-weight="{"bold" if d <= 1 else "normal"}" fill="#222">{esc(name)}</text>')
        if desc:
            tx = 14 + 8 * IND + 250 if d < 8 else x + 16 + 260
            out.append(f'<text x="{max(tx, x + 16 + len(name) * 6.5 + 14)}" y="{y+4}" font-size="9.5" fill="#666">{esc(desc)}</text>')
    legend = ' '.join(f'<circle cx="{560 + i*0}" cy="0" r="0"/>' for i in range(0))
    return f'<svg width="{W}" height="{h}" viewBox="0 0 {W} {h}" {FONT}>{"".join(out)}</svg>'

TREE_INIT = [
    (0, 'wWinMain', 'src/main.cpp — プログラムの入口', 'm'),
    (1, 'RegisterClassExW / CreateWindowExW', 'クライアント 1280×800 のウィンドウ', 'o'),
    (1, 'FindProjectDirectories', 'shaders / assets フォルダを検出して Graphics に登録', 'm'),
    (2, 'Graphics::SetShaderDirectory / SetAssetDirectory', '', 'g'),
    (1, 'Graphics::Initialize', 'D3D11 の初期化', 'g'),
    (2, 'D3D11CreateDeviceAndSwapChain', 'デバイス / コンテキスト / スワップチェーン', 'o'),
    (2, 'CreateBackBufferViews', 'RTV, 深度バッファ (D24S8), DSV, ビューポート', 'g'),
    (2, 'CreateCommonStates', 'ラスタライザ ×2, 深度 ×2, ブレンド ×2, サンプラー ×2', 'g'),
    (1, 'Graphics::CreateConstantBuffer', 'FrameConstants 用 b0 (DYNAMIC)', 'g'),
    (1, 'Terrain::Initialize', '地形の準備', 't'),
    (2, 'LoadMeshTerrain', 'assets/terrain.obj があれば', 't'),
    (3, 'MeshLoader::LoadObj', 'v/vt/vn/f を解釈、三角形化、頂点共有', 'o'),
    (3, '(x 反転 / 自動フィット / 巻き順統一)', '右手系→左手系、60 m に正規化', 't'),
    (3, 'BakeHeightMap', 'メッシュを 257×257 高さマップに焼き込み', 't'),
    (3, 'CreateGpuBuffers', '頂点 / インデックスバッファ (IMMUTABLE)', 't'),
    (2, 'BuildHeightMap → BuildMesh → CreateGpuBuffers', 'OBJ が無いとき: HeightFunction で生成', 't'),
    (3, 'Noise::Fbm2D / Ridged2D', '岩肌の凹凸', 'o'),
    (2, 'CreateRockTexture', '岩テクスチャ', 't'),
    (3, 'TextureLoader::LoadFromFile', 'assets/rock.png 等を WIC で読み込み', 'o'),
    (3, 'CreateProceduralRockTexture', '失敗時: Noise::FbmTileable 等で生成', 't'),
    (3, 'TextureLoader::CreateFromPixels', 'ミップマップ付きテクスチャ + SRV', 'o'),
    (2, 'CreateShaders', 'Terrain.hlsl → VS / PS / 入力レイアウト', 't'),
    (1, 'SPH::Initialize', 'SPHParams を受け取り配列とハッシュ表を確保', 's'),
    (2, 'Reset', '粒子数 0、時間 0', 's'),
    (1, 'FluidRenderer::Initialize', '粒子描画の準備', 'r'),
    (2, '(StructuredBuffer<float4> + SRV)', '最大 12,000 粒子', 'r'),
    (2, 'CreateShaders', 'Particle.hlsl ×3 + FluidSurface.hlsl ×3 をコンパイル', 'r'),
    (2, 'CreateDepthTargets', 'R32_FLOAT ×2 (ピンポン用)', 'r'),
    (1, 'ShowWindow', 'initialized = true', 'o'),
    (1, 'メインループ', '→ 次の図', 'm'),
]
TREE_FRAME = [
    (0, 'メインループ (1 回転)', 'while (msg != WM_QUIT)', 'm'),
    (1, 'PeekMessageW', 'メッセージがあれば Dispatch → WndProc (→ 表 1)', 'o'),
    (1, 'steady_clock で dt を計測', '', 'm'),
    (1, 'SPH::Update(dt)', '一時停止中はスキップ', 's'),
    (2, 'Step  × substeps (≤ 5)', 'Δt = 0.003 s', 's'),
    (3, 'Emit → EmitSheet', '上流に 14×4 の粒子板を投入', 's'),
    (4, 'Terrain::GetValleyCenterX', '投入位置 (谷底) を問い合わせ', 't'),
    (3, 'BuildGrid', 'CellCoord → HashCell → カウンティングソート', 's'),
    (3, 'BuildNeighborLists   ‖', 'ForEachNeighbor で 27 セル走査', 's'),
    (3, 'ComputeDensityPressure   ‖', 'ρ = Σ m W_poly6,  p = k·max(ρ−ρ0, 0)', 's'),
    (3, 'ComputeForces   ‖', '圧力力 + 粘性力 → 加速度クランプ → +g', 's'),
    (3, 'Integrate   ‖', 'v += aΔt, x += vΔt', 's'),
    (4, 'ResolveCollision', '側壁 / 地形 / 接触ゾーン / 空気抵抗', 's'),
    (5, 'Terrain::GetHeight / GetNormal', '高さマップの双線形補間と中心差分', 't'),
    (3, 'RemoveDrained', '下流端の粒子を削除', 's'),
    (1, 'Graphics::BeginFrame', '白でクリア、RTV + DSV 設定', 'g'),
    (1, 'UpdateFrameConstants', 'Camera → View / Proj (転置) → b0', 'm'),
    (2, 'Camera::GetViewMatrix / GetProjMatrix / GetPosition', '', 'c'),
    (2, 'Graphics::UpdateBuffer', 'Map(WRITE_DISCARD) / Unmap', 'g'),
    (1, 'Terrain::Render', 'DrawIndexed (131,072 三角形)', 't'),
    (1, 'SPH::GetRenderData', '(x, y, z, 平滑化した速さ)', 's'),
    (1, 'FluidRenderer::Render', 'mode で分岐', 'r'),
    (2, 'UploadParticles', 'StructuredBuffer へ Map / Unmap', 'r'),
    (2, 'RenderParticlesMode', '粒子モード', 'r'),
    (3, 'DrawSpheres (PSColor)', 'DrawInstanced(4, N) — 球インポスター', 'r'),
    (2, 'RenderSurfaceMode', '表面生成モード', 'r'),
    (3, 'DrawSpheres (PSDepth)', 'depthTex[0] にビュー空間 z', 'r'),
    (3, 'DrawFullscreen (PSBlur)  × 4 往復', 'depthTex 0 ⇄ 1 バイラテラルぼかし', 'r'),
    (3, 'DrawFullscreen (PSComposite)', '法線復元 + 陰影 → αブレンドで合成', 'r'),
    (1, 'Graphics::EndFrame', 'Present (vsync)', 'g'),
    (1, 'FPS 計測 / UpdateTitle', '0.5 s / 0.25 s ごと', 'm'),
]

# ============================================================ 2. シーケンス図
class Seq:
    def __init__(self, actors, width=760):
        self.actors = actors; self.w = width
        self.n = len(actors); self.gap = (width - 60) / (self.n - 1)
        self.items = []; self.y = 70
        self.frames = []
    def x(self, name): return 30 + self.actors.index(name) * self.gap
    def msg(self, a, b, text, kind='call', dy=26):
        xa, xb = self.x(a), self.x(b); y = self.y
        dash = ' stroke-dasharray="5,3"' if kind == 'ret' else ''
        if a == b:
            self.items.append(f'<path d="M{xa},{y} H{xa+28} V{y+14} H{xa+4}" fill="none" stroke="#333" stroke-width="1.3" marker-end="url(#ar)"/>')
            self.items.append(f'<text x="{xa+34}" y="{y+10}" font-size="9.5">{esc(text)}</text>')
            self.y += dy + 8
        else:
            self.items.append(f'<line x1="{xa}" y1="{y}" x2="{xb}" y2="{y}" stroke="#333" stroke-width="1.3"{dash} marker-end="url(#ar)"/>')
            tw = len(text) * 6.2 + 6
            if b == self.actors[-1] and xb - xa < tw:      # 右端へ届く長いラベルは終点に右寄せ
                tx, anchor = xb - 8, 'end'; rx = tx - tw
            elif a == self.actors[-1] and xa - xb < tw:
                tx, anchor = xa - 8, 'end'; rx = tx - tw
            else:
                tx, anchor = (xa + xb) / 2, 'middle'; rx = tx - tw / 2
            self.items.append(f'<rect x="{rx}" y="{y-14}" width="{tw}" height="13" fill="#fff" opacity="0.85"/>')
            self.items.append(f'<text x="{tx}" y="{y-4}" text-anchor="{anchor}" font-size="9.5">{esc(text)}</text>')
            self.y += dy
    def note(self, text, y=None, x=None, color='#fff8e6'):
        y = y or self.y; x = x or 30
        w = len(text) * 9.5 + 14
        self.items.append(f'<rect x="{x}" y="{y-11}" width="{w}" height="18" fill="{color}" stroke="#d09a00"/><text x="{x+7}" y="{y+2}" font-size="9.5">{esc(text)}</text>')
        self.y += 24
    def frame_begin(self, label):
        self.frames.append((label, self.y - 6)); self.y += 20
    def frame_end(self):
        label, y0 = self.frames.pop()
        self.items.insert(0, f'<rect x="12" y="{y0}" width="{self.w-24}" height="{self.y - y0}" fill="none" stroke="#6a4fa0" stroke-width="1.2" rx="4"/>'
                             f'<rect x="12" y="{y0}" width="{len(label)*9+16}" height="16" fill="#f3f0fa" stroke="#6a4fa0"/><text x="18" y="{y0+12}" font-size="9.5" font-weight="bold" fill="#6a4fa0">{esc(label)}</text>')
        self.y += 10
    def svg(self, title):
        h = self.y + 20
        head = [f'<text x="12" y="20" font-size="12" font-weight="bold" fill="#1a3c6e">{esc(title)}</text>']
        for a in self.actors:
            x = self.x(a)
            head.append(f'<rect x="{x-46}" y="30" width="92" height="26" rx="4" fill="#e4ecf7" stroke="#2b5797"/><text x="{x}" y="47" text-anchor="middle" font-size="10" font-weight="bold">{esc(a)}</text>')
            head.append(f'<line x1="{x}" y1="56" x2="{x}" y2="{h-10}" stroke="#aab" stroke-dasharray="4,3"/>')
        return (f'<svg width="{self.w}" height="{h}" viewBox="0 0 {self.w} {h}" {FONT}>'
                f'<defs><marker id="ar" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#333"/></marker></defs>'
                + ''.join(head) + ''.join(self.items) + '</svg>')

def seq_init():
    s = Seq(['wWinMain', 'Graphics', 'Terrain', 'MeshLoader\n/TextureLoader', 'SPH', 'FluidRenderer', 'GPU (D3D11)'])
    s.actors[3] = 'Loader'
    s.msg('wWinMain', 'wWinMain', 'CreateWindowExW')
    s.msg('wWinMain', 'Graphics', 'SetShaderDirectory / SetAssetDirectory')
    s.msg('wWinMain', 'Graphics', 'Initialize(hWnd, 1280, 800)')
    s.msg('Graphics', 'GPU (D3D11)', 'D3D11CreateDeviceAndSwapChain')
    s.msg('Graphics', 'GPU (D3D11)', 'RTV / DSV / ステート作成')
    s.msg('Graphics', 'wWinMain', 'true', 'ret')
    s.msg('wWinMain', 'Graphics', 'CreateConstantBuffer(FrameConstants)')
    s.msg('wWinMain', 'Terrain', 'Initialize(gfx)')
    s.frame_begin('alt  assets/terrain.obj がある')
    s.msg('Terrain', 'Loader', 'MeshLoader::LoadObj')
    s.msg('Loader', 'Terrain', 'MeshData (頂点 / 三角形)', 'ret')
    s.msg('Terrain', 'Terrain', 'x 反転・自動フィット・BakeHeightMap')
    s.frame_end()
    s.frame_begin('else  手続き生成')
    s.msg('Terrain', 'Terrain', 'BuildHeightMap (Noise) → BuildMesh')
    s.frame_end()
    s.msg('Terrain', 'GPU (D3D11)', 'CreateBuffer (頂点 / インデックス)')
    s.msg('Terrain', 'Loader', 'TextureLoader::LoadFromFile(rock.png)')
    s.msg('Loader', 'GPU (D3D11)', 'CreateTexture2D + GenerateMips')
    s.msg('Loader', 'Terrain', 'SRV (失敗なら Terrain がノイズ生成)', 'ret')
    s.msg('Terrain', 'Graphics', 'CompileShaderFromFile(Terrain.hlsl)')
    s.msg('Terrain', 'wWinMain', 'true', 'ret')
    s.msg('wWinMain', 'SPH', 'Initialize(&terrain, params)')
    s.msg('SPH', 'SPH', 'カーネル係数 / 配列 / ハッシュ表 / Reset')
    s.msg('wWinMain', 'FluidRenderer', 'Initialize(gfx, 12000)')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'StructuredBuffer / 深度テクスチャ ×2')
    s.msg('FluidRenderer', 'Graphics', 'CompileShaderFromFile ×6')
    s.msg('wWinMain', 'wWinMain', 'ShowWindow → メインループへ')
    return s.svg('シーケンス図 1  起動と初期化')

def seq_frame():
    s = Seq(['wWinMain', 'WndProc', 'Camera', 'SPH', 'Terrain', 'FluidRenderer', 'GPU (D3D11)'])
    s.frame_begin('loop  毎フレーム')
    s.frame_begin('opt  メッセージがある')
    s.msg('wWinMain', 'WndProc', 'DispatchMessage')
    s.msg('WndProc', 'Camera', 'Rotate / Pan / Zoom (マウス)')
    s.msg('WndProc', 'SPH', 'Reset (R キー)')
    s.frame_end()
    s.msg('wWinMain', 'wWinMain', 'dt = steady_clock 差分')
    s.frame_begin('opt  一時停止中でない')
    s.msg('wWinMain', 'SPH', 'Update(dt)')
    s.frame_begin('loop  サブステップ ×(≤5)')
    s.msg('SPH', 'Terrain', 'GetValleyCenterX (投入位置)')
    s.msg('SPH', 'SPH', 'BuildGrid → 近傍 → 密度・圧力 → 力  ‖')
    s.msg('SPH', 'Terrain', 'GetHeight / GetNormal (粒子ごと, 衝突)')
    s.msg('SPH', 'SPH', 'Integrate + ResolveCollision  ‖ → RemoveDrained')
    s.frame_end()
    s.frame_end()
    s.msg('wWinMain', 'GPU (D3D11)', 'Graphics::BeginFrame (クリア, RTV/DSV)')
    s.msg('wWinMain', 'Camera', 'GetViewMatrix / GetProjMatrix')
    s.msg('wWinMain', 'GPU (D3D11)', 'UpdateBuffer(b0 ← FrameConstants 転置済み)')
    s.msg('wWinMain', 'Terrain', 'Render')
    s.msg('Terrain', 'GPU (D3D11)', 'DrawIndexed (Terrain.hlsl)')
    s.msg('wWinMain', 'SPH', 'GetRenderData')
    s.msg('SPH', 'wWinMain', 'float4[] (x,y,z,速さ)', 'ret')
    s.msg('wWinMain', 'FluidRenderer', 'Render(particles, mode)')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'Map/Unmap → StructuredBuffer')
    s.frame_begin('alt  mode == Particles')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'DrawInstanced(4, N)  PSColor → バックバッファ')
    s.frame_end()
    s.frame_begin('else  mode == Surface')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'DrawInstanced(4, N)  PSDepth → depthTex[0]')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'Draw(3) PSBlur 横/縦 ×4  depthTex 0 ⇄ 1')
    s.msg('FluidRenderer', 'GPU (D3D11)', 'Draw(3) PSComposite → バックバッファ (αブレンド)')
    s.frame_end()
    s.msg('wWinMain', 'GPU (D3D11)', 'Graphics::EndFrame → Present')
    s.frame_end()
    return s.svg('シーケンス図 2  メインループ (1 フレーム)')

# ============================================================ 3. データフロー図
def dataflow():
    W, H = 760, 560
    it = []
    def box(x, y, w, h, text, kind='proc'):
        fill, st = {'proc': ('#eef3fa', '#2b5797'), 'data': ('#fff8e6', '#d09a00'), 'gpu': ('#fbe9d0', '#c77'), 'ext': ('#eee', '#888')}[kind]
        if kind == 'data':
            it.append(f'<path d="M{x},{y+6} Q{x+w/2},{y-6} {x+w},{y+6} V{y+h-6} Q{x+w/2},{y+h+6} {x},{y+h-6} Z" fill="{fill}" stroke="{st}" stroke-width="1.4"/>')
        else:
            it.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="5" fill="{fill}" stroke="{st}" stroke-width="1.4"/>')
        lines = text.split('\n'); fs = 10 if len(lines) <= 2 else 9
        ty = y + h/2 - (len(lines)-1)*(fs+2)/2 + 4
        for i, l in enumerate(lines):
            it.append(f'<text x="{x+w/2}" y="{ty+i*(fs+2)}" text-anchor="middle" font-size="{fs}" font-weight="{"bold" if i==0 and len(lines)>1 else "normal"}">{esc(l)}</text>')
    def arrow(x1, y1, x2, y2, text='', mid=None, dash=False):
        d = f'M{x1},{y1} ' + (f'L{mid[0]},{mid[1]} ' if mid else '') + f'L{x2},{y2}'
        dash_attr = ' stroke-dasharray="5,3"' if dash else ''
        it.append(f'<path d="{d}" fill="none" stroke="#444" stroke-width="1.4"{dash_attr} marker-end="url(#ar)"/>')
        if text:
            tx, ty = ((x1+x2)/2, (y1+y2)/2 - 5) if not mid else (mid[0], mid[1] - 5)
            tw = len(text) * 6 + 6
            it.append(f'<rect x="{tx-tw/2}" y="{ty-10}" width="{tw}" height="13" rx="2" fill="#fff" opacity="0.9"/>')
            it.append(f'<text x="{tx}" y="{ty}" text-anchor="middle" font-size="9" fill="#333">{esc(text)}</text>')
    # レーン
    it.append(f'<rect x="8" y="30" width="{W-16}" height="250" fill="#fafbfd" stroke="#bbc" stroke-dasharray="4,3"/><text x="14" y="44" font-size="10" font-weight="bold" fill="#557">CPU</text>')
    it.append(f'<rect x="8" y="300" width="{W-16}" height="250" fill="#fdf9f5" stroke="#cbb" stroke-dasharray="4,3"/><text x="14" y="314" font-size="10" font-weight="bold" fill="#755">GPU</text>')
    # 外部入力
    box(20, 60, 100, 34, 'キー / マウス', 'ext')
    box(20, 120, 100, 44, 'assets/\nterrain.obj\nrock.png', 'ext')
    # CPU processes / data
    box(150, 60, 110, 34, 'WndProc')
    box(290, 60, 110, 34, 'Camera')
    box(430, 55, 120, 44, 'UpdateFrame\nConstants')
    box(150, 120, 110, 44, 'Terrain\nInitialize')
    box(290, 120, 110, 44, '高さマップ\n257×257', 'data')
    box(430, 120, 120, 44, 'SPH::Update\n(粒子法, OpenMP)')
    box(590, 120, 140, 44, '粒子配列\npos / vel / acc / ρ / p', 'data')
    box(430, 200, 120, 40, 'GetRenderData\n近傍平均で速さ平滑化')
    box(590, 200, 140, 40, 'float4[]\n(x, y, z, 速さ)', 'data')
    box(150, 200, 110, 40, 'Terrain\nRender')
    # GPU
    box(150, 330, 110, 44, '頂点 / インデックス\nバッファ', 'gpu')
    box(290, 330, 110, 44, '岩テクスチャ\n+ mips', 'gpu')
    box(430, 330, 120, 44, 'cbuffer b0\nFrameConstants', 'gpu')
    box(590, 330, 140, 44, 'StructuredBuffer\n<float4> 粒子', 'gpu')
    box(150, 410, 250, 44, 'Terrain.hlsl  VS → PS\n(ランバート + テクスチャ)')
    box(430, 410, 300, 44, 'Particle.hlsl  VS(ビルボード) → PSColor / PSDepth\n(球インポスター, SV_Depth)')
    box(590, 480, 140, 44, 'depthTex 0 ⇄ 1\nR32_FLOAT', 'gpu')
    box(430, 480, 130, 44, 'FluidSurface.hlsl\nPSBlur ×4 → PSComposite')
    box(150, 480, 250, 44, 'バックバッファ + 深度バッファ\n→ Present', 'gpu')
    # arrows CPU
    arrow(120, 77, 150, 77, '')
    arrow(260, 77, 290, 77, 'Rotate/Pan/Zoom')
    arrow(400, 77, 430, 77, 'View/Proj')
    arrow(120, 142, 150, 142, '')
    arrow(260, 142, 290, 142, 'Bake / Build')
    arrow(400, 142, 430, 142, 'GetHeight/Normal')
    arrow(550, 142, 590, 142, '読み書き')
    arrow(660, 164, 660, 200, '')
    arrow(590, 220, 550, 220, '')
    arrow(205, 164, 205, 200, '')
    arrow(490, 240, 490, 330, '', dash=False)
    it.append(f'<text x="470" y="290" font-size="9" fill="#333" text-anchor="end">Map/Unmap 毎フレーム</text>')
    arrow(660, 240, 660, 330, 'Map/Unmap 毎フレーム')
    arrow(205, 240, 205, 330, 'DrawIndexed')
    arrow(345, 164, 345, 330, '起動時 1 回 (WIC)')
    arrow(120, 150, 130, 150);
    # arrows GPU
    arrow(205, 374, 205, 410, '')
    arrow(345, 374, 300, 410, 't0')
    arrow(490, 374, 400, 410, 'b0', mid=(470, 392))
    arrow(490, 374, 490, 410, 'b0')
    arrow(660, 374, 660, 410, 't0')
    arrow(275, 454, 275, 480, '色 + 深度')
    arrow(520, 454, 520, 480, '')
    it.append('<text x="520" y="471" text-anchor="middle" font-size="8.5" fill="#333">粒子: 色→バックバッファ / 表面: 深度→depthTex</text>')
    arrow(660, 454, 660, 480, 'ビュー空間 z')
    arrow(590, 502, 560, 502, '')
    arrow(430, 502, 400, 502, 'αブレンド合成')
    it.append(f'<text x="14" y="{H-6}" font-size="9" fill="#555">四角 = 処理、円筒 = データ (CPU メモリ)、橙 = GPU リソース、灰 = 外部入力。矢印のラベルはデータの内容または転送方法。</text>')
    return (f'<svg width="{W}" height="{H}" viewBox="0 0 {W} {H}" {FONT}>'
            f'<defs><marker id="ar" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#444"/></marker></defs>'
            + ''.join(it) + '</svg>')

# ============================================================ HTML
CSS = '''
  @page { size: A4; margin: 16mm 14mm 16mm 14mm; }
  html { font-family: "Yu Gothic UI", "Yu Gothic", Meiryo, sans-serif; font-size: 10.5pt; line-height: 1.6; color: #222; }
  body { margin: 0; }
  h1 { font-size: 19pt; color: #1a3c6e; border-bottom: 3px solid #2b5797; padding-bottom: 4px; margin: 0 0 8px; page-break-before: always; }
  h2 { font-size: 13pt; color: #1a3c6e; border-left: 6px solid #2b5797; padding-left: 8px; margin: 14px 0 6px; page-break-after: avoid; }
  p { margin: 4px 0 8px; }
  .lead { background: #eef3fa; border-left: 5px solid #2b5797; padding: 6px 10px; margin: 6px 0 10px; font-size: 10pt; }
  .fig { text-align: center; margin: 6px 0 10px; page-break-inside: avoid; }
  .fig svg { max-width: 100%; max-height: 235mm; height: auto; }
  table { border-collapse: collapse; width: 100%; font-size: 9.5pt; margin: 6px 0 10px; page-break-inside: avoid; }
  th, td { border: 1px solid #b8c4d6; padding: 3px 6px; vertical-align: top; }
  th { background: #e4ecf7; text-align: left; }
  .legend { font-size: 9.5pt; color: #444; margin: 2px 0 8px; }
  .dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin: 0 3px 0 8px; }
  .cover { text-align: center; padding-top: 80mm; }
  .cover .t { font-size: 26pt; font-weight: bold; color: #1a3c6e; }
  .cover .s { font-size: 13pt; color: #444; margin: 10px 0 30px; }
  .cover .m { font-size: 10.5pt; color: #666; line-height: 2; }
'''

def main():
    tree_legend = ('<div class="legend">色の意味:'
                   '<span class="dot" style="background:#2b5797"></span>main.cpp'
                   '<span class="dot" style="background:#2a7a9a"></span>Graphics'
                   '<span class="dot" style="background:#6a9a3c"></span>Terrain'
                   '<span class="dot" style="background:#c0392b"></span>SPH'
                   '<span class="dot" style="background:#6a4fa0"></span>FluidRenderer'
                   '<span class="dot" style="background:#b8860b"></span>Camera'
                   '<span class="dot" style="background:#555"></span>その他 / Win32 / D3D　　‖ = OpenMP 並列</div>')
    wnd_table = ('<table><tr><th style="width:34%">メッセージ</th><th>処理</th></tr>'
                 '<tr><td>WM_LBUTTONDOWN / RBUTTONDOWN</td><td>押下フラグ ON、位置を記憶、SetCapture</td></tr>'
                 '<tr><td>WM_MOUSEMOVE</td><td>左ドラッグ: Camera::Rotate、右ドラッグ: Camera::Pan</td></tr>'
                 '<tr><td>WM_MOUSEWHEEL</td><td>Camera::Zoom (0.9 / 1.1 倍)</td></tr>'
                 '<tr><td>WM_KEYDOWN 1 / 2 / Space / R / Esc</td><td>粒子モード / 表面モード / 一時停止 / SPH::Reset / 終了</td></tr>'
                 '<tr><td>WM_SIZE</td><td>Graphics::Resize → FluidRenderer::OnResize (初期化前は無視)</td></tr>'
                 '<tr><td>WM_DESTROY</td><td>PostQuitMessage</td></tr></table>')
    body = '''<div class="cover"><div class="t">山から粒子が落ちる<br>処理の流れ図</div>
<div class="s">呼び出しツリー図・シーケンス図・データフロー図で見る<br>main 関数からの処理の流れ</div>
<div class="m">対象: water_from_mountain (src/main.cpp ほか)<br>https://github.com/githubtarohario/water_from_mountain</div></div>'''

    body += '<h1>1. 呼び出しツリー図</h1><div class="lead">wWinMain から呼ばれる関数を、呼び出しの階層（木構造）で示します。上から下が実行順、右にいくほど深い呼び出しです。「どの関数が何を呼ぶか」を一覧するのに向いています。</div>' + tree_legend
    body += '<h2>1-1  起動と初期化</h2><div class="fig">' + call_tree(TREE_INIT, '呼び出しツリー: wWinMain (初期化)') + '</div>'
    body += '<h2>1-2  メインループ（毎フレーム）</h2><div class="fig">' + call_tree(TREE_FRAME, '呼び出しツリー: メインループ 1 回転') + '</div>'
    body += '<h2>表 1  WndProc のメッセージ処理</h2>' + wnd_table

    body += '<h1>2. シーケンス図</h1><div class="lead">時間の流れを上から下に取り、モジュール（縦の破線＝ライフライン）の間でどの順にメッセージ（関数呼び出し）が飛ぶかを示します。実線の矢印が呼び出し、破線が戻り値、枠は loop（繰り返し）/ alt（分岐）/ opt（条件付き）です。「誰が誰をいつ呼ぶか」を追うのに向いています。</div>'
    body += '<div class="fig">' + seq_init() + '</div>'
    body += '<div class="fig">' + seq_frame() + '</div>'

    body += '<h1>3. データフロー図</h1><div class="lead">処理（四角）とデータ（円筒 = CPU メモリ、橙 = GPU リソース）を並べ、データがどこで作られ、どこへ運ばれるかを矢印で示します。上段が CPU、下段が GPU で、その境界を越える矢印が「CPU → GPU 転送」です。「どのデータがどこにあるか」「毎フレーム何が転送されるか」を把握するのに向いています。</div>'
    body += '<div class="fig">' + dataflow() + '</div>'
    body += ('<table><tr><th style="width:30%">転送</th><th>頻度</th><th>内容</th></tr>'
             '<tr><td>頂点 / インデックス / 岩テクスチャ</td><td>起動時 1 回</td><td>IMMUTABLE バッファ、ミップマップ付きテクスチャ (WIC またはノイズ生成)</td></tr>'
             '<tr><td>cbuffer b0 (FrameConstants)</td><td>毎フレーム (+ パスごとに Params 更新)</td><td>View / Proj / ViewProj (転置済み)、光源、画面サイズ、粒子半径、ぼかし方向 — 320 B</td></tr>'
             '<tr><td>StructuredBuffer 粒子</td><td>毎フレーム</td><td>(x, y, z, 速さ) × 最大 12,000 = 192 KB。Map(WRITE_DISCARD)</td></tr>'
             '<tr><td>depthTex 0 ⇄ 1</td><td>表面モードのみ、GPU 内</td><td>ビュー空間深度。深度パス → ぼかし 8 パス → 合成の入力</td></tr></table>')

    doc = f'<!DOCTYPE html><html lang="ja"><head><meta charset="utf-8"><title>処理の流れ図</title><style>{CSS}</style></head><body>{body}</body></html>'
    out_html = os.path.join(ROOT, 'docs', '処理の流れ図.html')
    open(out_html, 'w', encoding='utf-8').write(doc)
    tmp = os.path.join(os.environ.get('TEMP', '.'), 'struct_out.pdf')
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--print-to-pdf=' + tmp,
                    'file:///' + out_html.replace('\\', '/')], check=True, timeout=180, capture_output=True)
    out_pdf = os.path.join(ROOT, 'docs', '処理の流れ図.pdf')
    shutil.copy(tmp, out_pdf)
    print('PDF:', out_pdf, os.path.getsize(out_pdf))

if __name__ == '__main__':
    main()
