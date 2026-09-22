# =============================================================================
# build_callgraph.py
#   main 関数 (wWinMain) から始まる関数の呼び出し関係を「コール図」として描き、
#   PDF (docs/コール図.pdf) に出力する。
#
#   各ノードには関数名と「役割」を書く。役割の文はソースの
#     // 関数名 : Xxx
#     // 概要   : ....
#   から自動で抜き出すので、実装のコメントを直せば図も追随する。
#
#   使い方: python tools/build_callgraph.py
# =============================================================================
import html, os, re, shutil, subprocess

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
def esc(s): return html.escape(s)

# ---------------------------------------------------------------- 役割コメントの抽出
def collect_roles():
    """
    ソース中の「関数名 / 概要」コメントを辞書にする。
    同じ関数名が複数のクラスにあるため (Initialize, Render, CreateShaders ...)、
      修飾キー  "Graphics::Initialize"   … ファイル名をクラス名とみなしたキー
      短縮キー  "Initialize"             … 1 箇所にしか無いときだけ有効
    の 2 通りを作る。戻り値は (修飾キー辞書, 短縮キー辞書)。
    """
    qualified, seen = {}, {}
    files = []
    for d, exts in (('src', ('.h', '.cpp')), ('shaders', ('.hlsl', '.hlsli'))):
        for f in sorted(os.listdir(os.path.join(ROOT, d))):
            if f.endswith(exts):
                files.append(os.path.join(ROOT, d, f))
    for path in files:
        base = os.path.basename(path)
        owner = base.rsplit('.', 1)[0] if base.endswith(('.h', '.cpp')) else base   # Graphics / Particle.hlsl
        lines = open(path, encoding='utf-8').read().split('\n')
        for i, line in enumerate(lines):
            m = re.search(r'//\s*関数名\s*[:：]\s*(.+?)\s*$', line)
            if not m:
                continue
            names = [n.strip() for n in re.split(r'/', m.group(1))]
            desc = []
            for j in range(i + 1, min(i + 8, len(lines))):
                t = lines[j]
                m2 = re.search(r'//\s*概要\s*[:：]\s*(.+?)\s*$', t)
                if m2:
                    desc.append(m2.group(1)); continue
                if desc:
                    m3 = re.search(r'//\s{6,}(.+?)\s*$', t)
                    if m3 and not re.search(r'(引数|戻り値|関数名)\s*[:：]', t):
                        desc.append(m3.group(1)); continue
                    break
            if not desc:
                continue
            text = ' '.join(desc)
            for n in names:
                qualified.setdefault(f'{owner}::{n}', text)
                seen.setdefault(n, set()).add(text)
    short = {n: list(v)[0] for n, v in seen.items() if len(v) == 1}
    return qualified, short

ROLES_Q, ROLES_S = collect_roles()

def role_of(key, fallback=''):
    """修飾キー → 短縮キー (一意なときのみ) の順に役割を探す"""
    if key in ROLES_Q:
        return ROLES_Q[key]
    bare = key.split('::')[-1]
    if bare in ROLES_S:
        return ROLES_S[bare]
    return fallback

def short_role(key, fallback='', limit=40):
    """図に載せる短い役割。最初の文 (。まで) を取り、長ければ切り詰める"""
    r = role_of(key, fallback)
    if not r:
        return ''
    head = re.split(r'[。]', r)[0]
    head = re.sub(r'\s*\(.*?\)\s*', '', head).strip()     # 括弧書きは図では省く
    if len(head) > limit:
        head = head[:limit] + '…'
    return head

# ---------------------------------------------------------------- 図の描画
MOD = {   # モジュール: (塗り, 枠, ラベル)
    'main':   ('#e8eefb', '#2b5797', 'main.cpp'),
    'gfx':    ('#e4f1f6', '#2a7a9a', 'Graphics'),
    'terrain':('#eaf4e2', '#5f8f34', 'Terrain'),
    'sph':    ('#fbe9e7', '#c0392b', 'SPH'),
    'render': ('#f0ecfa', '#6a4fa0', 'FluidRenderer'),
    'camera': ('#fdf3dc', '#a8770b', 'Camera'),
    'util':   ('#eef0f2', '#667', 'Noise / Loader / Win32 / D3D'),
    'hlsl':   ('#fdeee4', '#c4703a', 'HLSL (GPU)'),
}

def wrap(text, n):
    """全角前提の簡易折り返し (n 文字ごと、句読点の直後で切れるよう微調整)"""
    out, line = [], ''
    for ch in text:
        line += ch
        if len(line) >= n and ch in '、。) ）/':
            out.append(line); line = ''
        elif len(line) >= n + 4:
            out.append(line); line = ''
    if line:
        out.append(line)
    return out

class Graph:
    """左→右に階層を取るコール図。ノードは (列, 順序) で自動配置する"""
    def __init__(self, colw=196, boxw=186, x0=16, y0=38, gap=7):
        self.colw, self.boxw, self.x0, self.y0, self.gap = colw, boxw, x0, y0, gap
        self.nodes = {}      # id -> dict
        self.order = []      # 配置順
        self.edges = []      # (from, to, label, style)
        self.groups = []
        self.y = y0

    def node(self, nid, col, name, mod, role=None, key=None, w=None, extra=''):
        r = role if role is not None else short_role(key or name, '')
        boxw = w or self.boxw
        lines = wrap(r, int(boxw / 8.4)) if r else []
        if extra:
            lines += wrap(extra, int(boxw / 8.4))
        h = 16 + max(len(lines), 0) * 10.5 + 4
        name_lines = name.split(chr(10))
        h += (len(name_lines) - 1) * 12
        self.nodes[nid] = dict(col=col, name=name_lines, mod=mod, lines=lines, x=self.x0 + col * self.colw,
                               y=self.y, w=boxw, h=h)
        self.order.append(nid)
        self.y += h + self.gap
        return nid

    def space(self, px=14):
        """次のノードまでの縦の空きを広げる (グループ枠の見出しを重ねないため)"""
        self.y += px

    def edge(self, a, b, label='', style='call'):
        self.edges.append((a, b, label, style))

    def group(self, ids, title, color='#99a'):
        self.groups.append((ids, title, color))

    def render(self, title, note=''):
        it = []
        # グループ枠 (最初に描いて背面へ)
        for ids, gtitle, color in self.groups:
            ns = [self.nodes[i] for i in ids]
            x0 = min(n['x'] for n in ns) - 7; x1 = max(n['x'] + n['w'] for n in ns) + 7
            y0 = min(n['y'] for n in ns) - 19; y1 = max(n['y'] + n['h'] for n in ns) + 7
            it.append(f'<rect x="{x0}" y="{y0}" width="{x1-x0}" height="{y1-y0}" rx="7" fill="#fcfcfd" '
                      f'stroke="{color}" stroke-dasharray="5,3"/>'
                      f'<text x="{x0+8}" y="{y0+13}" font-size="9.5" font-weight="bold" fill="{color}">{esc(gtitle)}</text>')
        # 矢印
        for a, b, label, style in self.edges:
            na, nb = self.nodes[a], self.nodes[b]
            dash = ' stroke-dasharray="4,3"' if style != 'call' else ''
            col = '#888' if style != 'call' else '#444'
            if nb['col'] > na['col']:
                # 親の下辺から降りて子の左辺へ (木の枝)
                sx = na['x'] + 14; sy = na['y'] + na['h']
                ex = nb['x']; ey = nb['y'] + nb['h'] / 2
                d = f'M{sx},{sy} V{ey} H{ex-3}'
            elif nb['col'] == na['col']:
                sx = na['x'] + na['w'] / 2; sy = na['y'] + na['h']
                ex, ey = nb['x'] + nb['w'] / 2, nb['y']
                d = f'M{sx},{sy} V{ey-3}'
            else:
                # 右の列へ戻る / 別モジュールへの呼び出しは右側を回る
                sx = na['x'] + na['w']; sy = na['y'] + na['h'] / 2
                mx = max(na['x'] + na['w'], nb['x'] + nb['w']) + 16
                ex, ey = nb['x'] + nb['w'], nb['y'] + nb['h'] / 2
                d = f'M{sx},{sy} H{mx} V{ey} H{ex+3}'
            it.append(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="1.3"{dash} marker-end="url(#ar)"/>')
            if label:
                lx, ly = (na['x'] + 18, (na['y'] + na['h'] + nb['y'] + nb['h'] / 2) / 2)
                it.append(f'<text x="{lx}" y="{ly}" font-size="8.5" fill="#555">{esc(label)}</text>')
        # ノード
        for nid in self.order:
            n = self.nodes[nid]
            fill, stroke, _ = MOD[n['mod']]
            it.append(f'<rect x="{n["x"]}" y="{n["y"]}" width="{n["w"]}" height="{n["h"]}" rx="4" '
                      f'fill="{fill}" stroke="{stroke}" stroke-width="1.3"/>')
            it.append(f'<rect x="{n["x"]}" y="{n["y"]}" width="4" height="{n["h"]}" rx="2" fill="{stroke}"/>')
            for k, nl in enumerate(n['name']):
                it.append(f'<text x="{n["x"]+10}" y="{n["y"]+13+k*12}" font-size="9.8" font-weight="bold" '
                          f'fill="#1a2b44">{esc(nl)}</text>')
            base = n['y'] + 25.5 + (len(n['name']) - 1) * 12
            for k, ln in enumerate(n['lines']):
                it.append(f'<text x="{n["x"]+10}" y="{base+k*10.5}" font-size="8.2" fill="#555">{esc(ln)}</text>')
        w = self.x0 + max(n['x'] + n['w'] for n in self.nodes.values()) + 26
        # 見出しと説明を図の中に描く (ページ見出しを兼ねるので図を大きく取れる)
        note_lines = wrap(note, int(w / 8.0)) if note else []
        dy = 16 + len(note_lines) * 13
        head = (f'<text x="14" y="22" font-size="14" font-weight="bold" fill="#1a3c6e">{esc(title)}</text>'
                f'<line x1="14" y1="29" x2="{w-14}" y2="29" stroke="#2b5797" stroke-width="2"/>')
        for k, ln in enumerate(note_lines):
            head += f'<text x="14" y="{44 + k*13}" font-size="9.6" fill="#555">{esc(ln)}</text>'
        h = self.y + 12 + dy
        return (f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
                f'font-family="\'Yu Gothic UI\', Meiryo, sans-serif">'
                f'<defs><marker id="ar" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto">'
                f'<path d="M0,0 L8,4 L0,8 z" fill="#444"/></marker></defs>'
                + head + f'<g transform="translate(0,{dy})">' + ''.join(it) + '</g></svg>')

# ---------------------------------------------------------------- 図 1: 全体
def fig_overview():
    g = Graph(colw=232, boxw=222)
    g.node('main', 0, 'wWinMain', 'main', role='プログラムの入口。初期化してメインループを回す')
    g.node('find', 1, 'FindProjectDirectories', 'main', key='main::FindProjectDirectories')
    g.node('gi', 1, 'Graphics::Initialize', 'gfx', key='Graphics::Initialize')
    g.node('cb', 1, 'Graphics::CreateConstantBuffer', 'gfx', key='Graphics::CreateConstantBuffer')
    g.node('ti', 1, 'Terrain::Initialize', 'terrain', key='Terrain::Initialize')
    g.node('si', 1, 'SPH::Initialize', 'sph', key='SPH::Initialize')
    g.node('fi', 1, 'FluidRenderer::Initialize', 'render', key='FluidRenderer::Initialize')
    g.node('loop', 1, 'メインループ (while)', 'main', role='更新と描画を繰り返す → 図 3')
    g.node('wp', 2, 'WndProc', 'main', key='main::WndProc')
    g.node('upd', 2, 'SPH::Update', 'sph', key='SPH::Update')
    g.node('ufc', 2, 'UpdateFrameConstants', 'main', key='main::UpdateFrameConstants')
    g.node('tr', 2, 'Terrain::Render', 'terrain', key='Terrain::Render')
    g.node('grd', 2, 'SPH::GetRenderData', 'sph', key='SPH::GetRenderData')
    g.node('fr', 2, 'FluidRenderer::Render', 'render', key='FluidRenderer::Render')
    g.node('ef', 2, 'Graphics::EndFrame', 'gfx', key='Graphics::EndFrame')
    for a in ('find', 'gi', 'cb', 'ti', 'si', 'fi', 'loop'):
        g.edge('main', a)
    for b in ('wp', 'upd', 'ufc', 'tr', 'grd', 'fr', 'ef'):
        g.edge('loop', b)
    g.group(['find', 'gi', 'cb', 'ti', 'si', 'fi'], '初期化 (起動時 1 回) — 図 2・3')
    g.group(['wp', 'upd', 'ufc', 'tr', 'grd', 'fr', 'ef'], '毎フレーム — 図 4・5・6')
    return g.render('図 1  全体のコール図 — wWinMain から見た呼び出し',
                    '箱 = 関数（上段: 関数名、下段: 役割）。矢印 = 呼び出し。色 = 所属モジュール')

# ---------------------------------------------------------------- 図 2: 初期化 (Terrain 以外)
def fig_init_core():
    g = Graph(colw=236, boxw=226)
    g.node('main', 0, 'wWinMain', 'main', role='各モジュールを順に初期化する')
    g.node('find', 1, 'FindProjectDirectories', 'main', key='main::FindProjectDirectories')
    g.node('sd', 2, 'Graphics::SetShaderDirectory\n/ SetAssetDirectory', 'gfx', key='Graphics::SetShaderDirectory')
    g.node('gi', 1, 'Graphics::Initialize', 'gfx', key='Graphics::Initialize')
    g.node('dev', 2, 'D3D11CreateDeviceAndSwapChain', 'util', role='デバイス・スワップチェーンを作る')
    g.node('bbv', 2, 'CreateBackBufferViews', 'gfx', key='Graphics::CreateBackBufferViews')
    g.node('cs', 2, 'CreateCommonStates', 'gfx', key='Graphics::CreateCommonStates')
    g.node('cb', 1, 'Graphics::CreateConstantBuffer', 'gfx', key='Graphics::CreateConstantBuffer')
    g.node('ti', 1, 'Terrain::Initialize', 'terrain', role='地形の準備 → 内部は図 3')
    g.node('si', 1, 'SPH::Initialize', 'sph', key='SPH::Initialize')
    g.node('rst', 2, 'SPH::Reset', 'sph', key='SPH::Reset')
    g.node('fi', 1, 'FluidRenderer::Initialize', 'render', key='FluidRenderer::Initialize')
    g.node('fsh', 2, 'FluidRenderer::CreateShaders', 'render', key='FluidRenderer::CreateShaders')
    g.node('cdt', 2, 'CreateDepthTargets', 'render', key='FluidRenderer::CreateDepthTargets')
    g.node('comp', 3, 'Graphics::CompileShaderFromFile', 'gfx', key='Graphics::CompileShaderFromFile')
    for a in ('find', 'gi', 'cb', 'ti', 'si', 'fi'):
        g.edge('main', a)
    g.edge('find', 'sd')
    for a in ('dev', 'bbv', 'cs'):
        g.edge('gi', a)
    g.edge('si', 'rst')
    g.edge('fi', 'fsh'); g.edge('fi', 'cdt'); g.edge('fsh', 'comp')
    return g.render('図 2  初期化のコール図（Graphics / SPH / FluidRenderer）',
                    '実線 = 直接の呼び出し。Terrain::Initialize の内部は図 3')

# ---------------------------------------------------------------- 図 3: Terrain の初期化
def fig_init_terrain():
    g = Graph(colw=228, boxw=218)
    g.node('ti', 0, 'Terrain::Initialize', 'terrain', key='Terrain::Initialize')
    g.space()
    g.node('lmt', 1, 'LoadMeshTerrain', 'terrain', key='Terrain::LoadMeshTerrain')
    g.node('obj', 2, 'MeshLoader::LoadObj', 'util', key='MeshLoader::LoadObj')
    g.node('bake', 2, 'BakeHeightMap', 'terrain', key='Terrain::BakeHeightMap')
    g.node('cfg', 2, 'LoadTerrainConfig', 'terrain', key='Terrain::LoadTerrainConfig')
    g.space()
    g.node('bhm', 1, 'BuildHeightMap', 'terrain', key='Terrain::BuildHeightMap')
    g.node('hf', 2, 'HeightFunction', 'terrain', key='Terrain::HeightFunction')
    g.node('noise', 3, 'Noise::Fbm2D / Ridged2D', 'util', key='Noise::Fbm2D')
    g.node('bm', 1, 'BuildMesh', 'terrain', key='Terrain::BuildMesh')
    g.node('gn', 2, 'Terrain::GetNormal', 'terrain', key='Terrain::GetNormal')
    g.node('cgb', 1, 'CreateGpuBuffers', 'terrain', key='Terrain::CreateGpuBuffers')
    g.node('crt', 1, 'CreateRockTexture', 'terrain', key='Terrain::CreateRockTexture')
    g.node('tl', 2, 'TextureLoader::LoadFromFile', 'util', key='TextureLoader::LoadFromFile')
    g.node('prt', 2, 'CreateProceduralRockTexture', 'terrain', key='Terrain::CreateProceduralRockTexture')
    g.node('cfp', 3, 'TextureLoader::CreateFromPixels', 'util', key='TextureLoader::CreateFromPixels')
    g.node('tsh', 1, 'Terrain::CreateShaders', 'terrain', key='Terrain::CreateShaders')
    for a in ('lmt', 'bhm', 'bm', 'cgb', 'crt', 'tsh'):
        g.edge('ti', a)
    for a in ('obj', 'bake', 'cfg'):
        g.edge('lmt', a)
    g.edge('lmt', 'cgb', 'OBJ 側も共用', 'weak')
    g.edge('bhm', 'hf'); g.edge('hf', 'noise')
    g.edge('bm', 'gn'); g.edge('bm', 'cgb', '', 'weak')
    g.edge('crt', 'tl'); g.edge('crt', 'prt', '画像が無ければ'); g.edge('prt', 'cfp'); g.edge('tl', 'cfp', '', 'weak')
    g.group(['lmt', 'obj', 'bake', 'cfg'], 'A. OBJ 地形 (assets/terrain.obj があるとき)', '#5f8f34')
    g.group(['bhm', 'hf', 'noise', 'bm', 'gn'], 'B. 手続き生成 (OBJ が無いとき)', '#5f8f34')
    return g.render('図 3  Terrain::Initialize のコール図',
                    '地形は A / B のどちらか一方を通る。破線 = 両方から呼ばれる共通処理')

# ---------------------------------------------------------------- 図 3: 毎フレーム
def fig_frame():
    g = Graph(colw=222, boxw=212)
    g.node('loop', 0, 'メインループ (1 回転)', 'main', role='入力処理と 1 フレームの更新・描画')
    g.node('peek', 1, 'PeekMessageW / DispatchMessage', 'util', role='OS のメッセージを WndProc へ配る')
    g.node('wp', 2, 'WndProc', 'main', key='main::WndProc')
    g.node('cam', 3, 'Camera::Rotate / Pan / Zoom', 'camera', key='Camera::Rotate')
    g.node('rsz', 3, 'Graphics::Resize\n→ FluidRenderer::OnResize', 'gfx', key='Graphics::Resize')
    g.node('srst', 3, 'SPH::Reset', 'sph', key='SPH::Reset')
    g.node('upd', 1, 'SPH::Update', 'sph', key='SPH::Update')
    g.node('step', 2, 'SPH::Step', 'sph', key='Step', extra='→ 内部は図 6')
    g.node('bf', 1, 'Graphics::BeginFrame', 'gfx', key='Graphics::BeginFrame')
    g.node('ufc', 1, 'UpdateFrameConstants', 'main', key='main::UpdateFrameConstants')
    g.node('gvm', 2, 'Camera::GetViewMatrix / GetProjMatrix\n/ GetPosition', 'camera', key='Camera::GetViewMatrix')
    g.node('ub', 2, 'Graphics::UpdateBuffer', 'gfx', key='Graphics::UpdateBuffer')
    g.node('tr', 1, 'Terrain::Render', 'terrain', key='Terrain::Render')
    g.node('thlsl', 2, 'Terrain.hlsl  VSMain → PSMain', 'hlsl',
           role='頂点を変換し、岩テクスチャと拡散反射で陰影を付ける')
    g.node('grd', 1, 'SPH::GetRenderData', 'sph', key='SPH::GetRenderData')
    g.node('fr', 1, 'FluidRenderer::Render', 'render', key='FluidRenderer::Render', extra='→ 内部は図 6')
    g.node('ef', 1, 'Graphics::EndFrame', 'gfx', key='Graphics::EndFrame')
    g.node('ttl', 1, 'UpdateTitle', 'main', key='main::UpdateTitle')
    for a in ('peek', 'upd', 'bf', 'ufc', 'tr', 'grd', 'fr', 'ef', 'ttl'):
        g.edge('loop', a)
    g.edge('peek', 'wp')
    for a in ('cam', 'rsz', 'srst'):
        g.edge('wp', a)
    g.edge('upd', 'step')
    g.edge('ufc', 'gvm'); g.edge('ufc', 'ub')
    g.edge('tr', 'thlsl', 'DrawIndexed')
    return g.render('図 4  メインループ 1 フレームのコール図',
                    '上から下が実行順。オレンジの箱は GPU 上で動くシェーダー')

# ---------------------------------------------------------------- 図 4: SPH
def fig_sph():
    g = Graph(colw=218, boxw=208)
    g.node('upd', 0, 'SPH::Update', 'sph', key='SPH::Update')
    g.node('step', 1, 'SPH::Step', 'sph', key='SPH::Step')
    g.node('emit', 2, 'Emit', 'sph', key='SPH::Emit')
    g.node('sheet', 3, 'EmitSheet', 'sph', key='SPH::EmitSheet')
    g.node('gep', 4, 'Terrain::GetEmitPoint', 'terrain', key='Terrain::GetEmitPoint')
    g.node('gvc', 4, 'Terrain::GetValleyCenterX', 'terrain', key='Terrain::GetValleyCenterX')
    g.node('bg', 2, 'BuildGrid', 'sph', key='SPH::BuildGrid')
    g.node('cc', 3, 'CellCoord', 'sph', key='SPH::CellCoord')
    g.node('hc', 3, 'HashCell', 'sph', key='SPH::HashCell')
    g.space()
    g.node('bnl', 2, 'BuildNeighborLists   ‖', 'sph', key='SPH::BuildNeighborLists')
    g.node('fen', 3, 'ForEachNeighbor', 'sph', key='SPH::ForEachNeighbor')
    g.node('cdp', 2, 'ComputeDensityPressure   ‖', 'sph', key='SPH::ComputeDensityPressure')
    g.node('cf', 2, 'ComputeForces   ‖', 'sph', key='SPH::ComputeForces')
    g.node('int', 2, 'Integrate   ‖', 'sph', key='SPH::Integrate')
    g.node('rc', 3, 'ResolveCollision', 'sph', key='SPH::ResolveCollision')
    g.node('gh', 4, 'Terrain::GetHeight', 'terrain', key='Terrain::GetHeight')
    g.node('gn', 4, 'Terrain::GetNormal', 'terrain', key='Terrain::GetNormal')
    g.node('rd', 2, 'RemoveDrained', 'sph', key='SPH::RemoveDrained')
    g.edge('upd', 'step', 'サブステップ ×(≤5)')
    for a in ('emit', 'bg', 'bnl', 'cdp', 'cf', 'int', 'rd'):
        g.edge('step', a)
    g.edge('emit', 'sheet'); g.edge('sheet', 'gep'); g.edge('gep', 'gvc')
    g.edge('bg', 'cc'); g.edge('bg', 'hc')
    g.edge('bnl', 'fen'); g.edge('fen', 'hc', '', 'weak')
    g.edge('int', 'rc'); g.edge('rc', 'gh'); g.edge('rc', 'gn')
    g.edge('sheet', 'gh', '', 'weak')
    g.group(['bnl', 'fen', 'cdp', 'cf', 'int', 'rc', 'gh', 'gn'], '‖ = OpenMP で粒子ごとに並列実行', '#c0392b')
    return g.render('図 5  SPH（物理シミュレーション）のコール図',
                    '1 サブステップの中で上から順に呼ばれる。Terrain へは高さ・法線の問い合わせだけを行う')

# ---------------------------------------------------------------- 図 5: 描画
def fig_render():
    g = Graph(colw=226, boxw=216)
    g.node('fr', 0, 'FluidRenderer::Render', 'render', key='FluidRenderer::Render')
    g.node('up', 1, 'UploadParticles', 'render', key='FluidRenderer::UploadParticles')
    g.space()
    g.node('rpm', 1, 'RenderParticlesMode', 'render', key='FluidRenderer::RenderParticlesMode')
    g.node('ds1', 2, 'DrawSpheres (PSColor)', 'render', key='FluidRenderer::DrawSpheres')
    g.node('pvs', 3, 'Particle.hlsl  VSMain', 'hlsl',
           role='粒子ごとにカメラへ正対する四角形を作る')
    g.node('pc', 3, 'Particle.hlsl  PSColor', 'hlsl', key='Particle.hlsl::PSColor')
    g.node('ss1', 4, 'SphereSurface', 'hlsl', key='Particle.hlsl::SphereSurface')
    g.node('jet', 4, 'JetColor', 'hlsl', key='Common.hlsli::JetColor')
    g.space()
    g.node('rsm', 1, 'RenderSurfaceMode', 'render', key='FluidRenderer::RenderSurfaceMode')
    g.node('ds2', 2, 'DrawSpheres (PSDepth)', 'render', key='FluidRenderer::DrawSpheres')
    g.node('pd', 3, 'Particle.hlsl  PSDepth', 'hlsl', key='Particle.hlsl::PSDepth')
    g.node('df1', 2, 'DrawFullscreen (PSBlur) ×4 往復', 'render', key='FluidRenderer::DrawFullscreen')
    g.node('vfs', 3, 'Common.hlsli  VSFullscreen', 'hlsl', key='Common.hlsli::VSFullscreen')
    g.node('blur', 3, 'FluidSurface.hlsl  PSBlur', 'hlsl', key='FluidSurface.hlsl::PSBlur')
    g.node('df2', 2, 'DrawFullscreen (PSComposite)', 'render', key='FluidRenderer::DrawFullscreen')
    g.node('comp', 3, 'FluidSurface.hlsl  PSComposite', 'hlsl', key='FluidSurface.hlsl::PSComposite')
    g.node('vpd', 4, 'ViewPosFromDepth', 'hlsl', key='FluidSurface.hlsl::ViewPosFromDepth')
    g.edge('fr', 'up'); g.edge('fr', 'rpm', 'mode = 粒子'); g.edge('fr', 'rsm', 'mode = 表面生成')
    g.edge('rpm', 'ds1'); g.edge('ds1', 'pvs'); g.edge('ds1', 'pc')
    g.edge('pc', 'ss1'); g.edge('pc', 'jet')
    g.edge('rsm', 'ds2'); g.edge('rsm', 'df1'); g.edge('rsm', 'df2')
    g.edge('ds2', 'pd'); g.edge('ds2', 'pvs', '', 'weak'); g.edge('pd', 'ss1', '', 'weak')
    g.edge('df1', 'vfs'); g.edge('df1', 'blur')
    g.edge('df2', 'comp'); g.edge('df2', 'vfs', '', 'weak'); g.edge('comp', 'vpd')
    g.group(['rpm', 'ds1', 'pvs', 'pc', 'ss1', 'jet'], '粒子モード（キー 1）', '#6a4fa0')
    g.group(['rsm', 'ds2', 'pd', 'df1', 'vfs', 'blur', 'df2', 'comp', 'vpd'], '表面生成モード（キー 2）', '#6a4fa0')
    return g.render('図 6  描画（FluidRenderer）のコール図',
                    'オレンジの箱は GPU 上のシェーダー関数。DrawSpheres / DrawFullscreen が描画命令を出すと呼ばれる')

# ---------------------------------------------------------------- HTML → PDF
CSS = '''
  @page { size: A4 landscape; margin: 12mm 12mm 12mm 12mm; }
  html { font-family: "Yu Gothic UI", "Yu Gothic", Meiryo, sans-serif; font-size: 10pt; line-height: 1.6; color: #222; }
  body { margin: 0; }
  h1 { font-size: 15pt; color: #1a3c6e; border-bottom: 2.5px solid #2b5797; padding-bottom: 2px; margin: 0 0 4px; page-break-before: always; }
  .lead { background: #eef3fa; border-left: 4px solid #2b5797; padding: 4px 8px; margin: 3px 0 4px; font-size: 9pt; }
  .figpage { text-align: center; page-break-before: always; page-break-inside: avoid; }
  .figpage svg { max-width: 100%; max-height: 184mm; height: auto; }
  .legend { font-size: 9pt; color: #444; margin: 6px 0 10px; }
  .legend span { display: inline-block; margin-right: 12px; }
  .legend i { display: inline-block; width: 22px; height: 11px; border-radius: 2px; border: 1.2px solid; vertical-align: -1px; margin-right: 3px; }
  table { border-collapse: collapse; width: 100%; font-size: 9pt; margin: 4px 0 8px; page-break-inside: avoid; }
  th, td { border: 1px solid #b8c4d6; padding: 3px 6px; vertical-align: top; }
  th { background: #e4ecf7; text-align: left; }
  code { font-family: Consolas, monospace; font-size: 8.8pt; background: #eef1f5; padding: 0 3px; }
  .cover { text-align: center; padding-top: 55mm; }
  .cover .t { font-size: 24pt; font-weight: bold; color: #1a3c6e; }
  .cover .s { font-size: 12pt; color: #444; margin: 8px 0 24px; }
  .cover .m { font-size: 10pt; color: #666; line-height: 1.9; }
'''

def legend():
    parts = []
    for k in ('main', 'gfx', 'terrain', 'sph', 'render', 'camera', 'util', 'hlsl'):
        f, s, label = MOD[k]
        parts.append(f'<span><i style="background:{f};border-color:{s}"></i>{esc(label)}</span>')
    return '<div class="legend">' + ''.join(parts) + '</div>'

def main():
    body = ('<div class="cover"><div class="t">山から粒子が落ちる<br>コール図</div>'
            '<div class="s">main 関数 (wWinMain) から呼ばれる関数と、その役割</div>'
            '<div class="m">対象: water_from_mountain（C++ / Direct3D 11 / HLSL）<br>'
            'https://github.com/githubtarohario/water_from_mountain<br>'
            '各関数の役割はソースの「// 概要 :」コメントから自動抽出しています</div></div>')

    figs = [
        ('図 1  全体のコール図',
         '最上位の呼び出し関係。起動時に 1 回だけ呼ばれる初期化と、毎フレーム呼ばれる更新・描画に分かれます。',
         fig_overview()),
        ('図 2  初期化のコール図（Graphics / SPH / FluidRenderer）',
         'wWinMain が各モジュールを初期化する流れ。D3D11 のデバイス作成、定数バッファ、粒子配列、シェーダーの用意までを行います。',
         fig_init_core()),
        ('図 3  Terrain::Initialize のコール図',
         '地形は「OBJ 読み込み」と「手続き生成」の 2 系統があり、岩テクスチャも「画像読み込み」と「ノイズ生成」に分かれます。',
         fig_init_terrain()),
        ('図 4  メインループのコール図',
         '1 フレームで呼ばれる関数。入力処理 → 物理更新 → 定数バッファ更新 → 地形描画 → 粒子描画 → Present の順です。',
         fig_frame()),
        ('図 5  SPH のコール図',
         '物理シミュレーションの内部。1 サブステップの中で近傍探索・密度・力・積分・排水が順に呼ばれます。',
         fig_sph()),
        ('図 6  描画のコール図',
         '粒子の描画と表面生成。C++ 側の関数が描画命令を出すと、GPU 上の HLSL 関数（オレンジ）が呼ばれます。',
         fig_render()),
    ]
    body += ('<h1>図の見方</h1>'
             '<p>本書の図は、関数を箱で表し、矢印で「どの関数がどの関数を呼ぶか」を示したコール図です。'
             '箱の上段が関数名、下段がその関数の役割です。役割の文はソースコードの「// 概要 :」コメントから'
             '自動で抜き出しているため、実装と一致しています（全文は巻末の付表を参照）。</p>'
             + legend() +
             '<table><tr><th style="width:22%">表記</th><th>意味</th></tr>'
             '<tr><td>箱の左の色帯</td><td>その関数が属するモジュール（上の凡例の色）</td></tr>'
             '<tr><td>実線の矢印</td><td>直接の呼び出し（呼ぶ側 → 呼ばれる側）</td></tr>'
             '<tr><td>破線の矢印</td><td>別の経路からも呼ばれる共通処理</td></tr>'
             '<tr><td>点線の枠</td><td>処理のまとまり（分岐する系統、並列実行される範囲など）</td></tr>'
             '<tr><td>‖ の印</td><td>OpenMP により粒子ごとに並列実行される関数</td></tr>'
             '<tr><td>オレンジの箱</td><td>GPU 上で動く HLSL シェーダーの関数</td></tr></table>'
             '<p>図は 6 枚あります。図 1 が全体像で、そこから初期化（図 2・3）、'
             'メインループ（図 4）、物理（図 5）、描画（図 6）へと掘り下げていきます。</p>')

    for title, lead, svg in figs:
        body += f'<div class="figpage">{svg}</div>'

    # 関数一覧（役割つき）
    rows = []
    for key, mod in [
        ('main::wWinMain', 'main.cpp'), ('main::FindProjectDirectories', 'main.cpp'),
        ('main::UpdateFrameConstants', 'main.cpp'), ('main::UpdateTitle', 'main.cpp'),
        ('main::WndProc', 'main.cpp'), ('main::FileExists', 'main.cpp'),
        ('Graphics::Initialize', 'Graphics'), ('Graphics::CreateBackBufferViews', 'Graphics'),
        ('Graphics::CreateCommonStates', 'Graphics'), ('Graphics::Resize', 'Graphics'),
        ('Graphics::BeginFrame', 'Graphics'), ('Graphics::EndFrame', 'Graphics'),
        ('Graphics::CompileShaderFromFile', 'Graphics'), ('Graphics::SetShaderDirectory', 'Graphics'),
        ('Graphics::CreateConstantBuffer', 'Graphics'), ('Graphics::UpdateBuffer', 'Graphics'),
        ('Terrain::Initialize', 'Terrain'), ('Terrain::LoadMeshTerrain', 'Terrain'),
        ('Terrain::BakeHeightMap', 'Terrain'), ('Terrain::LoadTerrainConfig', 'Terrain'),
        ('Terrain::BuildHeightMap', 'Terrain'), ('Terrain::HeightFunction', 'Terrain'),
        ('Terrain::BuildMesh', 'Terrain'), ('Terrain::CreateGpuBuffers', 'Terrain'),
        ('Terrain::CreateRockTexture', 'Terrain'), ('Terrain::CreateProceduralRockTexture', 'Terrain'),
        ('Terrain::CreateShaders', 'Terrain'), ('Terrain::Render', 'Terrain'),
        ('Terrain::GetHeight', 'Terrain'), ('Terrain::GetNormal', 'Terrain'),
        ('Terrain::ValleyCenterX', 'Terrain'), ('Terrain::GetValleyCenterX', 'Terrain'),
        ('Terrain::GetEmitPoint', 'Terrain'),
        ('SPH::Initialize', 'SPH'), ('SPH::Reset', 'SPH'), ('SPH::Update', 'SPH'), ('SPH::Step', 'SPH'),
        ('SPH::Emit', 'SPH'), ('SPH::EmitSheet', 'SPH'), ('SPH::BuildGrid', 'SPH'),
        ('SPH::CellCoord', 'SPH'), ('SPH::HashCell', 'SPH'), ('SPH::ForEachNeighbor', 'SPH'),
        ('SPH::BuildNeighborLists', 'SPH'), ('SPH::ComputeDensityPressure', 'SPH'),
        ('SPH::ComputeForces', 'SPH'), ('SPH::Integrate', 'SPH'), ('SPH::ResolveCollision', 'SPH'),
        ('SPH::RemoveDrained', 'SPH'), ('SPH::GetRenderData', 'SPH'),
        ('FluidRenderer::Initialize', 'FluidRenderer'), ('FluidRenderer::CreateShaders', 'FluidRenderer'),
        ('FluidRenderer::CreateDepthTargets', 'FluidRenderer'), ('FluidRenderer::OnResize', 'FluidRenderer'),
        ('FluidRenderer::UploadParticles', 'FluidRenderer'), ('FluidRenderer::Render', 'FluidRenderer'),
        ('FluidRenderer::RenderParticlesMode', 'FluidRenderer'), ('FluidRenderer::RenderSurfaceMode', 'FluidRenderer'),
        ('FluidRenderer::DrawSpheres', 'FluidRenderer'), ('FluidRenderer::DrawFullscreen', 'FluidRenderer'),
        ('Camera::Rotate', 'Camera'), ('Camera::Pan', 'Camera'), ('Camera::Zoom', 'Camera'),
        ('Camera::GetPosition', 'Camera'), ('Camera::GetViewMatrix', 'Camera'), ('Camera::GetProjMatrix', 'Camera'),
        ('MeshLoader::LoadObj', 'MeshLoader'), ('MeshLoader::ComputeSmoothNormals', 'MeshLoader'),
        ('MeshLoader::ComputeBounds', 'MeshLoader'),
        ('TextureLoader::LoadFromFile', 'TextureLoader'), ('TextureLoader::CreateFromPixels', 'TextureLoader'),
        ('Noise::Perlin2D', 'Noise'), ('Noise::Fbm2D', 'Noise'), ('Noise::Ridged2D', 'Noise'),
        ('Noise::Perlin2DPeriodic', 'Noise'), ('Noise::FbmTileable', 'Noise'), ('Noise::RidgedTileable', 'Noise'),
        ('Common.hlsli::JetColor', 'Common.hlsli'), ('Common.hlsli::VSFullscreen', 'Common.hlsli'),
        ('Terrain.hlsl::VSMain', 'Terrain.hlsl'), ('Terrain.hlsl::PSMain', 'Terrain.hlsl'),
        ('Particle.hlsl::VSMain', 'Particle.hlsl'), ('Particle.hlsl::SphereSurface', 'Particle.hlsl'),
        ('Particle.hlsl::PSColor', 'Particle.hlsl'), ('Particle.hlsl::PSDepth', 'Particle.hlsl'),
        ('FluidSurface.hlsl::PSBlur', 'FluidSurface.hlsl'),
        ('FluidSurface.hlsl::ViewPosFromDepth', 'FluidSurface.hlsl'),
        ('FluidSurface.hlsl::PSComposite', 'FluidSurface.hlsl'),
    ]:
        r = ROLES_Q.get(key, '')
        if r:
            rows.append(f'<tr><td><code>{esc(key)}</code></td><td>{esc(mod)}</td><td>{esc(r)}</td></tr>')
    body += ('<h1>付表  関数一覧と役割</h1>'
             '<div class="lead">図に登場する関数の役割を一覧にしたものです（ソースの「// 概要 :」コメントより自動生成）。</div>'
             '<table><tr><th style="width:26%">関数</th><th style="width:12%">定義場所</th><th>役割</th></tr>'
             + ''.join(rows) + '</table>')

    doc = f'<!DOCTYPE html><html lang="ja"><head><meta charset="utf-8"><title>コール図</title><style>{CSS}</style></head><body>{body}</body></html>'
    out_html = os.path.join(ROOT, 'docs', 'コール図.html')
    open(out_html, 'w', encoding='utf-8').write(doc)
    tmp = os.path.join(os.environ.get('TEMP', '.'), 'callgraph_out.pdf')
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--print-to-pdf=' + tmp,
                    'file:///' + out_html.replace('\\', '/')], check=True, timeout=180, capture_output=True)
    out_pdf = os.path.join(ROOT, 'docs', 'コール図.pdf')
    shutil.copy(tmp, out_pdf)
    print(f'roles extracted: {len(ROLES_Q)} qualified, {len(ROLES_S)} unique-short')
    print('PDF:', out_pdf, os.path.getsize(out_pdf))

if __name__ == '__main__':
    main()
