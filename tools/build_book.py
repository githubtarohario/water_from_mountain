# =============================================================================
# build_book.py
#   docs/book/*.html を結合し、ソースコードの引用と図を埋め込んで
#   Chrome ヘッドレスで PDF (docs/CG解説書.pdf) を生成する。
#
#   引用の書式:  {{CODE:ファイル:開始行に含まれる文字列:終了行(完全一致)}}
#     例) {{CODE:src/SPH.cpp:void SPH::Integrate():}}      … 末尾が "}" の関数 (終了行省略時の既定)
#         {{CODE:src/SPH.h:struct SPHParams:};}}            … 末尾が "};" の構造体
#         {{CODE:src/Noise.cpp:float Fbm2D(:    }}}          … 名前空間内 (末尾 "    }")
#     開始行の直前に続くコメント行 (//) も一緒に引用する。
#
#   図の書式:    {{SVG_名前}}  … この中で Python が生成する SVG
#   画像の書式:  {{IMG_名前}}  … スクリーンショット (引数のフォルダから読む)
#
#   使い方: python tools/build_book.py [スクリーンショットのフォルダ]
# =============================================================================
import base64, html, io, math, os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
BOOK = os.path.join(ROOT, 'docs', 'book')
SHOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'book', 'img')
PARTS = ['part0_front.html', 'part1_basics.html', 'part2_terrain.html', 'part3_fluid.html', 'part4_render.html']

# ---------------------------------------------------------------- コード引用
def extract_code(path, start, end):
    lines = open(os.path.join(ROOT, path), encoding='utf-8').read().split('\n')
    idx = next((i for i, l in enumerate(lines) if start in l), None)
    if idx is None:
        raise SystemExit(f'CODE start not found: {path} :: {start!r}')
    # 直前のコメント行 / template 行を含める
    b = idx
    while b > 0 and (lines[b - 1].strip().startswith('//') or lines[b - 1].strip().startswith('template')):
        b -= 1
    e = next((j for j in range(idx + 1, len(lines)) if lines[j].rstrip() == end), None)
    if e is None:
        raise SystemExit(f'CODE end not found: {path} :: {start!r} end={end!r}')
    body = '\n'.join(lines[b:e + 1])
    # 名前空間内のインデント (4 スペース) を除く
    ind = min((len(l) - len(l.lstrip()) for l in body.split('\n') if l.strip()), default=0)
    body = '\n'.join(l[ind:] if len(l) >= ind else l for l in body.split('\n'))
    return (f'<div class="src-title">{html.escape(path)}</div>'
            f'<pre class="src">{html.escape(body)}</pre>')

def replace_code(text):
    pat = re.compile(r'\{\{CODE:([^:\n]+):(.+?):( *\};?|)\}\}')
    return pat.sub(lambda m: extract_code(m.group(1), m.group(2), m.group(3) or '}'), text)

# ---------------------------------------------------------------- SVG 図
def polyline(points, color, width=2):
    return f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in points)}" fill="none" stroke="{color}" stroke-width="{width}"/>'

def axes(x0, y0, w, h, xl, yl):
    return (f'<line x1="{x0}" y1="{y0}" x2="{x0+w}" y2="{y0}" stroke="#888"/><line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0-h}" stroke="#888"/>'
            f'<text x="{x0+w-4}" y="{y0+14}" text-anchor="end" font-size="10">{xl}</text><text x="{x0+4}" y="{y0-h+10}" font-size="10">{yl}</text>')

def fig(svg, cap, w, h):
    return f'<div class="fig"><svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="sans-serif" font-size="11">{svg}</svg><div class="cap">{cap}</div></div>'

def svg_fade():
    x0, y0, w, h = 40, 150, 220, 120
    lin = [(x0 + t * w, y0 - t * h) for t in [i / 50 for i in range(51)]]
    fade = [(x0 + t * w, y0 - (t*t*t*(t*(t*6-15)+10)) * h) for t in [i / 50 for i in range(51)]]
    s = axes(x0, y0, w, h, 't', 'fade(t)') + polyline(lin, '#999') + polyline(fade, '#2b5797', 2.5)
    s += '<text x="280" y="60" font-size="10.5">— 灰: 線形 t</text><text x="280" y="80" font-size="10.5" fill="#2b5797">— 青: 6t⁵−15t⁴+10t³</text>'
    s += '<text x="280" y="110" font-size="10" fill="#555">両端で傾きが 0 になり、</text><text x="280" y="126" font-size="10" fill="#555">格子の境界がなめらかにつながる</text>'
    return fig(s, '図 5-2 Fade 曲線', 460, 180)

def _noise1d(x, seed):
    # 1D 勾配ノイズ (図示用)
    import random
    i = math.floor(x); f = x - i
    r = random.Random
    g0 = r(seed * 1000 + i).uniform(-1, 1); g1 = r(seed * 1000 + i + 1).uniform(-1, 1)
    u = f*f*f*(f*(f*6-15)+10)
    return (g0 * f) * (1 - u) + (g1 * (f - 1)) * u

def svg_fbm():
    x0, w = 40, 400
    rows = []
    ys = [50, 110, 170, 250]
    labels = ['オクターブ 0 (周波数 1, 振幅 1)', 'オクターブ 1 (周波数 2, 振幅 1/2)', 'オクターブ 2 (周波数 4, 振幅 1/4)', '合計 (fBm)']
    s = ''
    for k in range(3):
        pts = [(x0 + i, ys[k] - _noise1d(i / 80 * (2 ** k), 7 + k) * 40 / (2 ** k)) for i in range(w)]
        s += polyline(pts, '#2b5797', 1.5) + f'<text x="{x0}" y="{ys[k]-28}" font-size="10">{labels[k]}</text>'
    pts = [(x0 + i, ys[3] - sum(_noise1d(i / 80 * (2 ** k), 7 + k) * 40 / (2 ** k) for k in range(3))) for i in range(w)]
    s += polyline(pts, '#c33', 2) + f'<text x="{x0}" y="{ys[3]-40}" font-size="10" fill="#c33">{labels[3]}</text>'
    return fig(s, '図 5-3 fBm: 周波数を 2 倍、振幅を 1/2 にしたノイズを重ねる（1 次元で図示）', 480, 290)

def svg_valley_profile():
    x0, y0, w, h = 40, 170, 420, 130
    def ss(a, b, x):
        t = max(0, min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    def hgt(x):
        d = abs(x); wt = ss(1.6, 8.5, d)
        return 0.3 * min(d, 1.6) ** 2 + 11 * wt
    pts = [(x0 + (x + 15) / 30 * w, y0 - hgt(x) / 13 * h) for x in [i / 4 - 15 for i in range(121)]]
    s = axes(x0, y0, w, h, 'x − c(z)  [m]', '高さ') + polyline(pts, '#8a6a4a', 2.5)
    s += f'<line x1="{x0 + w/2}" y1="{y0}" x2="{x0 + w/2}" y2="{y0-h}" stroke="#36c" stroke-dasharray="3,2"/>'
    s += f'<text x="{x0 + w/2 + 4}" y="{y0 - h + 12}" font-size="10" fill="#36c">谷の中心線</text>'
    s += f'<text x="{x0 + w/2 - 30}" y="{y0 + 28}" font-size="10">U 字 (|d| &lt; 1.6)</text>'
    s += f'<text x="{x0 + 20}" y="{y0 - h + 30}" font-size="10">山 11 m</text>'
    s += f'<text x="{x0 + w/2 + 60}" y="{y0 - 50}" font-size="10">smoothstep(1.6, 8.5, d)</text>'
    return fig(s, '図 6-1 谷の断面（ノイズを除いた基本形）。中心から離れるほど smoothstep で山が立ち上がる', 480, 210)

def svg_kernels():
    x0, y0, w, h = 40, 150, 200, 110
    hh = 1.0
    def poly6(r): return (hh*hh - r*r) ** 3 if r < hh else 0
    def spiky_grad(r): return (hh - r) ** 2 if r < hh else 0
    def visc_lap(r): return (hh - r) if r < hh else 0
    s = ''
    for k, (fn, name, col) in enumerate([(poly6, 'W_poly6 (密度)', '#2b5797'), (spiky_grad, '|∇W_spiky| (圧力)', '#c33'), (visc_lap, '∇²W_visc (粘性)', '#3a3')]):
        ox = x0 + k * 230 if k < 2 else x0 + 115
        oy = y0 if k < 2 else y0 + 150
        vals = [fn(i / 100) for i in range(101)]
        mx = max(vals)
        pts = [(ox + i / 100 * w, oy - v / mx * h) for i, v in enumerate(vals)]
        s += axes(ox, oy, w, h, 'r / h', '') + polyline(pts, col, 2.5)
        s += f'<text x="{ox + 60}" y="{oy - h - 4}" font-size="10.5" fill="{col}">{name}</text>'
    s += f'<text x="{x0 + 230 + 70}" y="{y0 - 40}" font-size="9.5" fill="#555">r=0 で最大</text>'
    s += f'<text x="{x0 + 70}" y="{y0 - 20}" font-size="9.5" fill="#555">r=0 で勾配 0</text>'
    return fig(s, '図 10-1 3 種類のカーネル（h = 1 で正規化）。poly6 は中心が平ら、spiky は中心で勾配が最大', 520, 320)

def svg_jet():
    stops = [(0, (0.05, 0.05, 0.70)), (0.25, (0, 0.6, 1)), (0.5, (0.05, 0.95, 0.3)), (0.75, (1, 0.95, 0)), (1, (1, 0.15, 0))]
    g = ''.join(f'<stop offset="{p*100}%" stop-color="rgb({int(c[0]*255)},{int(c[1]*255)},{int(c[2]*255)})"/>' for p, c in stops)
    s = (f'<defs><linearGradient id="jet">{g}</linearGradient></defs>'
         f'<rect x="30" y="20" width="400" height="30" fill="url(#jet)" stroke="#555"/>'
         f'<text x="30" y="68" font-size="10">0 m/s (遅い)</text><text x="430" y="68" text-anchor="end" font-size="10">6 m/s 以上 (速い)</text>'
         f'<text x="230" y="68" text-anchor="middle" font-size="10">3 m/s</text>')
    return fig(s, '図 14-2 速度カラーマップ（JetColor）', 460, 80)

def svg_bilateral():
    x0, y0, w, h = 40, 130, 200, 90
    sig = 0.35
    sp = [(x0 + (i / 100) * w, y0 - math.exp(-((i / 100 - 0.5) / sig) ** 2 * 2) * h) for i in range(101)]
    dp = [(x0 + 260 + (i / 100) * w, y0 - math.exp(-((i / 100 - 0.5) / 0.22) ** 2 * 2) * h) for i in range(101)]
    s = axes(x0, y0, w, h, '画素距離 k', 'w_s') + polyline(sp, '#2b5797', 2.5)
    s += axes(x0 + 260, y0, w, h, '深度差 d_k − d_0', 'w_d') + polyline(dp, '#c33', 2.5)
    s += f'<text x="{x0+40}" y="{y0-h-4}" font-size="10.5">空間の重み exp(−k²/2σs²)</text>'
    s += f'<text x="{x0+300}" y="{y0-h-4}" font-size="10.5">深度の重み exp(−Δd²/2σd²)</text>'
    s += f'<text x="{x0}" y="{y0+34}" font-size="10" fill="#555">σs = 半径の 1/2 (ピクセル)</text>'
    s += f'<text x="{x0+260}" y="{y0+34}" font-size="10" fill="#555">σd = 1.5 m。深度 0 (流体外) は重み 0 にする</text>'
    return fig(s, '図 15-2 バイラテラルフィルタの 2 つの重み。両方の積が最終的な重みになる', 520, 180)

# 処理の流れ図 (呼び出しツリー / シーケンス / データフロー) は build_structure_diagrams.py から借りる
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_structure_diagrams as sd
def _wrap(svg, cap):
    return f'<div class="fig">{svg}<div class="cap">{cap}</div></div>'
def svg_calltree_init():  return _wrap(sd.call_tree(sd.TREE_INIT, '呼び出しツリー: wWinMain (初期化)'), '図 16-1 起動と初期化の呼び出しツリー')
def svg_calltree_frame(): return _wrap(sd.call_tree(sd.TREE_FRAME, '呼び出しツリー: メインループ 1 回転'), '図 16-2 メインループ 1 回転の呼び出しツリー')
def svg_seq_init():       return _wrap(sd.seq_init(), '図 16-3 起動と初期化のシーケンス図')
def svg_seq_frame():      return _wrap(sd.seq_frame(), '図 16-4 メインループ 1 フレームのシーケンス図')
def svg_dataflow():       return _wrap(sd.dataflow(), '図 16-5 データフロー図（上段 CPU、下段 GPU）')

SVGS = {'CALLTREE_INIT': svg_calltree_init, 'CALLTREE_FRAME': svg_calltree_frame, 'SEQ_INIT': svg_seq_init,
        'SEQ_FRAME': svg_seq_frame, 'DATAFLOW': svg_dataflow,
        'FADE': svg_fade, 'FBM': svg_fbm, 'VALLEY_PROFILE': svg_valley_profile, 'KERNELS': svg_kernels, 'JET': svg_jet, 'BILATERAL': svg_bilateral}

# ---------------------------------------------------------------- 画像
def img_data(name, width=900, crop_title=False):
    from PIL import Image
    p = os.path.join(SHOT, name)
    if not os.path.exists(p):
        return ''
    im = Image.open(p).convert('RGB')
    if crop_title:
        wd, ht = im.size
        im = im.crop((8, 30, wd - 8, ht - 8))
    im.thumbnail((width, width))
    buf = io.BytesIO(); im.save(buf, 'JPEG', quality=88)
    return 'data:image/jpeg;base64,' + base64.b64encode(buf.getvalue()).decode()

IMAGES = {'PARTICLES': ('shot8.png', True), 'SURFACE': ('shot9.png', True), 'OBJ1': ('shot11.png', True), 'OBJ2': ('shot12.png', True)}

# ---------------------------------------------------------------- 本体
def main():
    text = ''.join(open(os.path.join(BOOK, p), encoding='utf-8').read() for p in PARTS)
    text = replace_code(text)
    for k, fn in SVGS.items():
        text = text.replace('{{SVG_' + k + '}}', fn())
    for k, (f, crop) in IMAGES.items():
        text = text.replace('{{IMG_' + k + '}}', img_data(f, crop_title=crop))
    # 共通の矢印マーカーを最初の svg に依存させないよう、先頭に定義を置く
    text = text.replace('<body>', '<body><svg width="0" height="0" style="position:absolute"><defs><marker id="ar" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="#555"/></marker></defs></svg>', 1)
    left = re.findall(r'\{\{[A-Z_]+[:}]', text)
    if left:
        print('WARNING unresolved placeholders:', set(left))
    out_html = os.path.join(ROOT, 'docs', 'CG解説書.html')
    open(out_html, 'w', encoding='utf-8').write(text)
    out_pdf = os.path.join(ROOT, 'docs', 'CG解説書.pdf')
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    tmp_pdf = os.path.join(os.environ.get('TEMP', '.'), 'cgbook_out.pdf')
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                    '--print-to-pdf=' + tmp_pdf, 'file:///' + out_html.replace('\\', '/')],
                   check=True, timeout=300, capture_output=True)
    import shutil; shutil.copy(tmp_pdf, out_pdf)
    print('PDF:', out_pdf, os.path.getsize(out_pdf), 'bytes')

if __name__ == '__main__':
    main()
