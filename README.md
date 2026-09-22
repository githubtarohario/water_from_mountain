# 山から粒子が落ちる — DirectX 11 SPH 流体シミュレーション

参考動画「図6.37 表面生成.mp4」を再現したシミュレーションです。
岩肌テクスチャの山に蛇行した谷があり、上流から流体粒子 (SPH) が流れ落ちます。
粒子は速度で青→緑→赤に着色され、キー操作で「表面生成」(水面として描画) に切り替えられます。

## 動作環境

- Windows 10 / 11、DirectX 11 対応 GPU
- Visual Studio 2019 / 2022 (C++ デスクトップ開発ワークロード)
- CPU の並列化に OpenMP を使用 (コア数が多いほど粒子数を増やせます)

## ビルド方法

### A. Visual Studio で開く
`MountainParticles.sln` を開き、`Release | x64` でビルド → 実行 (F5)。
実行ファイルは `bin\Release\MountainParticles.exe` に出力されます。

### B. コマンドライン
```
build.bat      … cl.exe で一括ビルド (bin\MountainParticles.exe)
run.bat        … 起動
```

`shaders` フォルダと `assets` フォルダは実行時に読み込まれます (exe の場所から自動で探します)。

## 地形を Blender から取り込む

`assets/terrain.obj` があれば、起動時にその OBJ メッシュを地形として読み込みます（無ければ手続き生成）。
タイトルバーの「地形: OBJ / 生成」で確認できます。

1. Blender で地形メッシュを作る（上面図で **上 (+Y) が上流＝奥**、下 (−Y) が下流＝手前）
2. OBJ エクスポートで **Forward: −Z, Up: Y** を指定して `assets/terrain.obj` に保存
3. 起動すると自動で 60 m 四方にフィットされ（高さも同じ倍率）、上流端の最も低い点から水が投入されます

試作用の谷を作るスクリプトも同梱しています:
- `tools/blender_export_valley.py` … Blender 内で実行（または `blender -b -P tools/blender_export_valley.py`）すると谷を生成して OBJ を書き出す
- `tools/make_test_valley.py` … Blender が無い環境用。同じ谷を同じ OBJ 形式で書き出す（`python tools/make_test_valley.py`）

内部では、メッシュはそのまま描画に使い、衝突判定用には真上から見た高さを 257×257 の高さマップに焼き込んでいます。
そのため SPH 側の変更なしに任意形状の地形を使えますが、オーバーハング（ひさし）の下面は無視されます。

### 実在の地形を使う（国土地理院 標高タイル）

`tools/fetch_gsi_terrain.py` で国土地理院の標高タイル（DEM10B, 10 m メッシュ）をダウンロードし、実在の地形を `assets/terrain.obj` にできます。
同梱の `assets/terrain.obj` は **富士山**（山頂中心 8 km 四方、誇張なし）で、`assets/terrain.cfg` で投入位置を南斜面の山頂直下に指定しています。
関ヶ原の地形は `assets/terrain_sekigahara.obj` / `.cfg` に保存してあり、`terrain.obj` / `terrain.cfg` にコピーすれば切り替わります。

プリセットは `tools	errain_presets.bat` にまとめてあります（使いたい行の `rem` を外して実行）:

```
rem 富士山
python toolsetch_gsi_terrain.py --lat 35.3606 --lon 138.7274 --size 8 --exaggeration 1.0 --emit-lat 35.355 --emit-lon 138.729
rem 関ヶ原
rem python toolsetch_gsi_terrain.py --lat 35.362 --lon 136.472 --size 6 --exaggeration 2.0 --emit-lat 35.384 --emit-lon 136.470
```

- `--lat/--lon`: 中心、`--size`: 一辺 [km]、`--exaggeration`: 高さの誇張倍率（xz は 60 m に縮小されるので 2〜3 倍が見やすい）
- `--emit-lat/--emit-lon`: 粒子の投入位置。省略時は北端の最も低い点を自動選択
- 画面では北が奥（上流）、東が右。範囲の左右・下流端から出た粒子は消去されます
- 試作用の谷は `assets/terrain_test_valley.obj` に残してあるので、`terrain.obj` にコピーすれば戻せます

出典: 国土地理院 標高タイル（https://maps.gsi.go.jp/development/ichiran.html）

## 岩テクスチャの差し替え

`assets/rock.png` (または `rock.jpg` / `rock.jpeg` / `rock.bmp`) を任意の岩の写真に置き換えるだけで反映されます。
継ぎ目なく繰り返せる (シームレスな) 画像を推奨します。ファイルが見つからない場合はノイズによる手続き生成にフォールバックし、
タイトルバーの「テクスチャ: 画像 / ノイズ生成」でどちらが使われているか確認できます。

## 操作

| 操作 | 内容 |
|---|---|
| マウス左ドラッグ | カメラ回転 |
| マウス右ドラッグ | カメラ平行移動 |
| マウスホイール | ズーム |
| `1` | 粒子表示モード (速度で着色した球) |
| `2` | 表面生成モード (スクリーンスペース流体で水面を描画) |
| `Space` | 一時停止 / 再開 |
| `R` | 粒子をリセット |
| `Esc` | 終了 |

## ファイル構成

```
src/
  main.cpp           エントリーポイント、ウィンドウ、入力、メインループ
  Graphics.h/.cpp    D3D11 初期化、共通ステート、シェーダーコンパイル等のヘルパー
  FrameConstants.h   全シェーダー共通の定数バッファ構造体
  Camera.h/.cpp      オービットカメラ
  Noise.h/.cpp       パーリンノイズ / fBm / 尾根ノイズ (タイリング対応版あり)
  Terrain.h/.cpp     山の地形 (OBJ 読み込み or 高さマップ生成、メッシュ、岩テクスチャ、衝突用の高さ/法線取得)
  MeshLoader.h/.cpp  Wavefront OBJ の読み込み (v/vt/vn/f、多角形の三角形化、法線計算)
  TextureLoader.h/.cpp  WIC による画像ファイル (PNG/JPG/BMP 等) の読み込みとミップマップ付きテクスチャ作成
  SPH.h/.cpp         SPH 流体シミュレーション (空間ハッシュ近傍探索、密度/圧力/粘性、地形衝突、エミッタ)
  FluidRenderer.h/.cpp  粒子の球描画 (スフィア・インポスター) とスクリーンスペース流体レンダリング
shaders/
  Common.hlsli       定数バッファ、色マップ、フルスクリーン三角形 VS
  Terrain.hlsl       地形の描画
  Particle.hlsl      粒子ビルボード → 球の描画 (色 / 深度)
  FluidSurface.hlsl  深度のバイラテラルぼかし、法線復元と水面の合成
assets/
  rock.png           岩テクスチャ (差し替え可能)
  terrain.obj        地形メッシュ (差し替え可能。無ければ手続き生成。同梱は富士山の実地形)
  terrain_sekigahara.obj/.cfg  関ヶ原の実地形 (terrain.obj/.cfg にコピーして使う)
  terrain.cfg        粒子の投入位置 (emit_x / emit_z)。無ければ自動
  terrain_test_valley.obj  試作用の谷 (Blender なしで生成したもの)
tools/
  blender_export_valley.py  Blender で試作用の谷を作って OBJ 出力するスクリプト
  make_test_valley.py       同じ谷を Blender なしで OBJ 出力するスクリプト
  fetch_gsi_terrain.py      国土地理院の標高タイルから実在の地形を OBJ 出力するスクリプト
  terrain_presets.bat       地形プリセット (富士山 / 関ヶ原 / 試作の谷)
```

## ドキュメント

- [API 仕様書](docs/API.md) — 各クラス・関数のシグネチャ、引数、戻り値、パラメータ一覧
- [フロー図](docs/FLOW.md) — 起動〜フレーム処理〜SPH〜描画パスの流れ（Mermaid）
- [技術解説書 (PDF)](docs/技術解説書.pdf) — 実装の技術要素のまとめ
- [処理の流れ図 (PDF)](docs/処理の流れ図.pdf) — wWinMain からの処理を呼び出しツリー図・シーケンス図・データフロー図で図解（`tools/build_structure_diagrams.py` で生成）
- [コール図 (PDF)](docs/コール図.pdf) — main 関数から呼ばれる関数と、その役割コメント付きの呼び出し図（全 6 図 + 関数一覧。役割はソースの `// 概要 :` から自動抽出、`tools/build_callgraph.py` で生成）
- [CG 解説書 (PDF)](docs/CG解説書.pdf) — このプログラムを教材にした CG・流体シミュレーションの教科書（全 18 章 + 付録）。`docs/book/*.html` から `tools/build_book.py` で生成

## アルゴリズムの概要

### 地形 (Terrain)
- 谷の中心線を `ValleyCenterX(z) = 4 sin(0.16 z) + 2 sin(0.41 z + 1.7)` で蛇行させる
- 谷の中心からの距離 `d` に応じて `smoothstep` で山を立ち上げ、上流→下流に傾斜を付ける
- fBm ノイズと尾根状ノイズで岩肌の凹凸を加える
- 岩テクスチャは `assets/rock.png` を WIC で読み込み (無ければ周期的パーリンノイズから CPU で生成)

### 流体 (SPH)
Müller et al. 2003 の標準的な SPH:
- 密度 `ρ_i = Σ m W_poly6(r)`、圧力 `p = k max(ρ - ρ0, 0)`
- 圧力力 (spiky カーネル勾配)、粘性力 (viscosity カーネルのラプラシアン)、重力
- シンプレクティック・オイラー法で積分
- 近傍探索は一辺 h の格子を空間ハッシュ + カウンティングソートで管理し、周囲 27 セルのみ走査
- 地形の高さマップと衝突判定 (地面より下なら押し戻し、法線方向速度を除去、摩擦)
- 下流の端で粒子を消去し、上流のエミッタから再投入して流れを継続

パラメータは `src/SPH.h` の `SPHParams` にまとめてあり、粒子数・粘性・硬さ・投入量などを調整できます。

### 表面生成 (Screen Space Fluid Rendering)
1. 各粒子を球として描き、ビュー空間の深度を R32_FLOAT テクスチャに書く (地形との深度テストあり)
2. 深度テクスチャをバイラテラルフィルタで平滑化 (横→縦を数回)。流体の外側は除外して輪郭を保つ
3. 平滑化した深度から隣接ピクセルとの差分で法線を復元し、拡散反射 + 鏡面反射 + フレネルで水面として陰影付けし、地形の上に半透明合成
