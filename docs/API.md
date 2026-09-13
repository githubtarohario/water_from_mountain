# API 仕様書 — water_from_mountain

DirectX 11 による「山から粒子が落ちる」SPH 流体シミュレーションの内部 API 仕様です。
モジュール（クラス / 名前空間）ごとに、公開関数の **シグネチャ・引数・戻り値・前提条件・副作用** を記載します。

- 座標系: 左手系、Y 軸が上。地形は x, z ∈ [-30, +30] の範囲。上流が -z、下流が +z。
- 単位: 長さ [m]、時間 [s]、質量 [kg]（SPH は SI 単位系で計算）
- 行列: DirectXMath（行優先）。GPU へ転送する際に転置し、HLSL 側では `mul(float4(v,1), M)` で使う。

---

## 目次

1. [Noise（名前空間）](#1-noise名前空間)
2. [Graphics](#2-graphics)
3. [FrameConstants（構造体）](#3-frameconstants構造体)
4. [Camera](#4-camera)
5. [Terrain](#5-terrain)
6. [SPHParams（構造体）](#6-sphparams構造体)
7. [SPH](#7-sph)
8. [FluidRenderer](#8-fluidrenderer)
9. [main.cpp（アプリケーション層）](#9-maincppアプリケーション層)
10. [HLSL シェーダー インターフェース](#10-hlsl-シェーダー-インターフェース)
11. [エラー処理方針](#11-エラー処理方針)

---

## 1. Noise（名前空間）

ファイル: `src/Noise.h`, `src/Noise.cpp`
用途: 地形の起伏と岩肌テクスチャの生成。すべて純粋関数（状態を持たない・スレッドセーフ）。

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `float Perlin2D(float x, float y)` | `x, y`: 評価座標（1.0 刻みで格子をまたぐ） | 約 -1.0〜+1.0 | Ken Perlin の Improved Noise を 2D 化したもの。格子周期 256。 |
| `float Fbm2D(float x, float y, int octaves = 6, float lacunarity = 2.0f, float gain = 0.5f)` | `octaves`: 重ねる層数<br>`lacunarity`: 層ごとの周波数倍率<br>`gain`: 層ごとの振幅倍率 | 約 -1.0〜+1.0（振幅合計で正規化済み） | fBm（fractional Brownian motion）。大小の起伏を同時に表現。 |
| `float Ridged2D(float x, float y, int octaves = 4)` | 同上 | 0.0〜1.0 程度 | `(1 - \|noise\|)^2` を重ねた尾根状ノイズ。稜線・亀裂用。 |
| `float Perlin2DPeriodic(float x, float y, int period)` | `period`: 繰り返し周期（格子単位、正の整数） | 約 -1.0〜+1.0 | 格子座標を `period` で折り返す周期ノイズ。タイリング可能。 |
| `float FbmTileable(float u, float v, int basePeriod, int octaves = 6)` | `u, v`: 0〜1 のテクスチャ座標<br>`basePeriod`: 最初の層の周期 | 約 -1.0〜+1.0 | 継ぎ目なく繰り返せる fBm。テクスチャ生成用。 |
| `float RidgedTileable(float u, float v, int basePeriod, int octaves = 4)` | 同上 | 0.0〜1.0 程度 | タイリング可能な尾根状ノイズ。 |

---

## 2. Graphics

ファイル: `src/Graphics.h`, `src/Graphics.cpp`
役割: D3D11 デバイス・スワップチェーン・共通ステートの所有と、各モジュールが使うヘルパーの提供。
ライフサイクル: `Initialize` → 毎フレーム `BeginFrame` … `EndFrame` → （ウィンドウサイズ変更時 `Resize`）→ デストラクタで自動解放（`ComPtr`）。

### 2.1 初期化・フレーム制御

| 関数 | 引数 | 戻り値 | 説明 / 副作用 |
|---|---|---|---|
| `bool Initialize(HWND hWnd, int width, int height)` | `hWnd`: 描画先ウィンドウ<br>`width, height`: バックバッファサイズ [px] | 成功で `true` | デバイス（FL 11_0）・スワップチェーン（R8G8B8A8, ダブルバッファ）・RTV・DSV（D24S8）・共通ステートを作成。Debug ビルドではデバッグレイヤーを試み、失敗時はフラグなしで再試行。失敗時はメッセージボックス表示。 |
| `bool Resize(int width, int height)` | 新しいサイズ [px] | 成功で `true` | RTV/DSV を解放してスワップチェーンをリサイズし再作成。`width` または `height` が 0 以下なら `false`。 |
| `void BeginFrame(const float clearColor[4])` | `clearColor`: RGBA 0〜1 | — | バックバッファ + 深度をクリアし、RTV/DSV/ビューポートを設定。 |
| `void EndFrame(bool vsync)` | `vsync`: 垂直同期の有無 | — | `Present`。 |

### 2.2 リソースヘルパー

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `static bool CompileShaderFromFile(const std::wstring& path, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& outBlob)` | `path`: HLSL パス<br>`entryPoint`: 例 `"VSMain"`<br>`target`: 例 `"vs_5_0"`<br>`outBlob`: [出力] バイトコード | 成功で `true` | `D3DCompileFromFile` を使用。`#include` は標準インクルードハンドラ（ソースファイルのフォルダ基準）。失敗時はエラー文をメッセージボックスに表示。 |
| `static void SetShaderDirectory(const std::wstring& dir)` | `dir`: シェーダーフォルダ | — | 末尾の区切り文字を除去して保持。既定は `"shaders"`。 |
| `static std::wstring ShaderPath(const wchar_t* file)` | `file`: ファイル名 | フルパス | `dir + "\\" + file`。 |
| `bool CreateConstantBuffer(UINT byteSize, ComPtr<ID3D11Buffer>& outBuf)` | `byteSize`: バイト数（16 の倍数に切り上げ） | 成功で `true` | `USAGE_DYNAMIC` / `CPU_ACCESS_WRITE` の定数バッファを作成。 |
| `void UpdateBuffer(ID3D11Buffer* buf, const void* data, size_t byteSize)` | 書き込み先・データ・サイズ | — | `Map(WRITE_DISCARD)` → `memcpy` → `Unmap`。DYNAMIC バッファ専用。 |

### 2.3 アクセサ

| 関数 | 戻り値 |
|---|---|
| `ID3D11Device* GetDevice() const` | デバイス |
| `ID3D11DeviceContext* GetContext() const` | 即時コンテキスト |
| `ID3D11RenderTargetView* GetBackBufferRTV() const` | バックバッファ RTV |
| `ID3D11DepthStencilView* GetDepthStencilView() const` | メイン深度バッファ DSV |
| `int GetWidth() const` / `int GetHeight() const` | 現在のバックバッファサイズ |
| `ID3D11RasterizerState* GetRasterizerSolid() const` | 裏面カリングあり（時計回りが表） |
| `ID3D11RasterizerState* GetRasterizerNoCull() const` | カリングなし（ビルボード・フルスクリーン用） |
| `ID3D11DepthStencilState* GetDepthStateDefault() const` | 深度テスト・書き込みあり（LESS） |
| `ID3D11DepthStencilState* GetDepthStateDisabled() const` | 深度無効（後処理用） |
| `ID3D11BlendState* GetBlendOpaque() const` | ブレンドなし |
| `ID3D11BlendState* GetBlendAlpha() const` | `Src*A + Dst*(1-A)` |
| `ID3D11SamplerState* GetSamplerLinearWrap() const` | 異方性 8・WRAP（地形テクスチャ用） |
| `ID3D11SamplerState* GetSamplerPointClamp() const` | ポイント・CLAMP（深度テクスチャ用） |

---

## 3. FrameConstants（構造体）

ファイル: `src/FrameConstants.h`（HLSL: `shaders/Common.hlsli` の `cbuffer CBFrame : register(b0)`）
サイズ: 320 バイト（16 バイト境界、`static_assert` で検証）。全シェーダーで `b0` に共有。

| メンバ | 型 | 内容 |
|---|---|---|
| `View` | `XMFLOAT4X4` | ワールド→ビュー（転置済み） |
| `Proj` | `XMFLOAT4X4` | ビュー→クリップ（転置済み）。`_m00`, `_m11` を法線復元に使用 |
| `ViewProj` | `XMFLOAT4X4` | `View * Proj`（転置済み） |
| `CameraPosW` | `XMFLOAT4` | xyz: カメラのワールド座標 |
| `LightDirW` | `XMFLOAT4` | xyz: ワールド空間の「光源へ向かう」単位ベクトル |
| `LightDirV` | `XMFLOAT4` | xyz: 同ベクトルのビュー空間表現 |
| `ScreenSize` | `XMFLOAT4` | x: 幅, y: 高さ, z: 1/幅, w: 1/高さ [px] |
| `Params` | `XMFLOAT4` | x: 粒子描画半径, y: 経過時間 [s], z,w: ぼかし方向 (1,0) or (0,1) |

---

## 4. Camera

ファイル: `src/Camera.h`, `src/Camera.cpp`
役割: 注視点を中心に回るオービットカメラ。位置は極座標 `(yaw, pitch, distance)` から算出。
既定値: `target=(0,0,-3)`, `yaw=0`, `pitch=34°`, `distance=42`, `fovY=45°`, `near=0.5`, `far=400`。

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `Camera()` | — | — | 上記既定値で初期化（下流 +z 側から上流を見下ろす構図）。 |
| `void Rotate(float dYaw, float dPitch)` | 回転増分 [rad] | — | `pitch` は ±89° にクランプ。 |
| `void Zoom(float factor)` | 距離に掛ける倍率 | — | 距離を 3〜300 にクランプ。 |
| `void Pan(float dx, float dy)` | 画面右方向・上方向の移動量 [m] | — | 注視点をカメラの右・上ベクトル方向へ移動。 |
| `XMFLOAT3 GetPosition() const` | — | カメラのワールド座標 | `target + distance * (cos p sin y, sin p, cos p cos y)` |
| `XMMATRIX GetViewMatrix() const` | — | ビュー行列（転置前） | `XMMatrixLookAtLH` |
| `XMMATRIX GetProjMatrix(float aspect) const` | `aspect`: 幅/高さ | 射影行列（転置前） | `XMMatrixPerspectiveFovLH` |
| `float GetNearZ() const` / `float GetFarZ() const` | — | クリップ距離 | |

---

## 5. Terrain

ファイル: `src/Terrain.h`, `src/Terrain.cpp`
役割: 蛇行する谷を持つ山の地形。描画と、SPH の衝突判定用の高さ/法線問い合わせを提供。

### 5.1 定数

| 定数 | 値 | 意味 |
|---|---|---|
| `GRID_N` | 257 | 1 辺の頂点数（セル数 256） |
| `WORLD_SIZE` | 60.0 | 地形の 1 辺 [m] |
| `HALF_SIZE` | 30.0 | `WORLD_SIZE / 2` |

### 5.2 公開関数

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `bool Initialize(Graphics& gfx)` | `gfx` | 成功で `true` | 高さマップ生成 → メッシュ作成 → 岩テクスチャ生成（1024², ミップ付き）→ `Terrain.hlsl` コンパイル。 |
| `void Render(Graphics& gfx)` | `gfx` | — | 地形を `DrawIndexed`。前提: `b0` の定数バッファが設定済み。副作用: IA/VS/PS/RS/OM ステートを地形用に変更、`t0`/`s0` に岩テクスチャ/サンプラーをバインド。 |
| `float GetHeight(float x, float z) const` | ワールド座標（範囲外は端にクランプ） | 地面の高さ y | 高さマップの双線形補間。スレッドセーフ（読み取りのみ）。 |
| `XMFLOAT3 GetNormal(float x, float z) const` | ワールド座標 | 上向き単位法線 | 中心差分 `normalize(-∂h/∂x, 1, -∂h/∂z)`。 |
| `static float ValleyCenterX(float z)` | `z` | 谷の中心の x | `4 sin(0.16 z) + 2 sin(0.41 z + 1.7)`。エミッタ位置の決定にも使用。 |

### 5.3 高さ関数（内部仕様）

```
d        = |x - ValleyCenterX(z)|
wallT    = smoothstep(1.6, 8.5, d)           // 0: 谷底, 1: 山
base     = -0.20 * z                         // 上流が高い
floorU   = min(d, 1.6)^2 * 0.3               // U 字断面
mountain = 11.0 * wallT
rough    = Fbm2D(0.05x + 7.3, 0.05z + 2.1, 6) * (0.25 + 5.0 * wallT)
ridge    = Ridged2D(0.12x + 3, 0.12z + 5, 4) * 1.5 * wallT
h(x, z)  = base + floorU + mountain + rough + ridge
```

### 5.4 頂点フォーマット

| セマンティクス | 型 | 内容 |
|---|---|---|
| `POSITION` | `float3` | ワールド座標 |
| `NORMAL` | `float3` | 法線 |
| `TEXCOORD0` | `float2` | `(x, z) * 0.12`（約 8.3 m でテクスチャ 1 周） |

インデックス: 32bit、三角形リスト。上から見て時計回り（D3D 既定の表面）。

---

## 6. SPHParams（構造体）

ファイル: `src/SPH.h`。`SPH::Initialize` に渡す調整パラメータ。既定値は CPU 8 コア程度で 60 fps 以上を想定。

| メンバ | 既定値 | 単位 | 意味 |
|---|---|---|---|
| `particleSpacing` | 0.30 | m | 粒子の初期間隔（粒子径）。質量は `restDensity * spacing³` |
| `smoothingRadius` | 0.60 | m | 平滑化半径 h（間隔の 2 倍） |
| `restDensity` | 1000 | kg/m³ | 静止密度 ρ0 |
| `stiffness` | 2000 | — | 圧力係数 k（`p = k·max(ρ-ρ0, 0)`）。音速 ≈ √k |
| `viscosity` | 0.40 | — | 粘性係数 μ |
| `gravity` | 9.8 | m/s² | 重力加速度 |
| `maxSpeed` | 10.0 | m/s | 速度上限（安全弁） |
| `maxAcceleration` | 250 | m/s² | 圧力・粘性による加速度の上限（壁際の蹴り防止。重力は除く） |
| `timeStep` | 0.003 | s | サブステップ幅。CFL: `dt < 0.4 h / √k` |
| `maxSubsteps` | 5 | — | 1 フレームのサブステップ上限（超過分は実時間より遅く進む） |
| `restitution` | 0.0 | — | 地形との反発係数 |
| `friction` | 0.010 | /step | 接地中に速度へ掛ける減衰率 |
| `contactZone` | 0.30 | m | 地面からこの高さ以内を接触ゾーンとする |
| `contactDamping` | 0.06 | /step | 接触ゾーン最下部での減衰率（上端で 0 に線形減少） |
| `airDrag` | 0.015 | /step | 孤立粒子（飛沫）に掛ける空気抵抗 |
| `sparseNeighbors` | 8 | 個 | 近傍がこの数未満なら孤立粒子とみなす |
| `maxParticles` | 12000 | 個 | 最大粒子数（バッファサイズ） |
| `maxNeighbors` | 96 | 個 | 1 粒子あたり記録する近傍数の上限 |
| `emitWidth` | 14 | 個 | 1 回の投入で横に並べる数 |
| `emitHeight` | 4 | 個 | 1 回の投入で縦に積む数 |
| `emitSpeed` | 4.0 | m/s | 投入初速（+z 方向）。投入間隔 = `spacing / emitSpeed` |
| `emitZ` | -27.5 | m | 投入位置の z |
| `emitLift` | 0.8 | m | 地面から投入位置までの高さ |

---

## 7. SPH

ファイル: `src/SPH.h`, `src/SPH.cpp`
役割: CPU（OpenMP）による WCSPH 流体シミュレーション。
スレッド: `Update` は呼び出しスレッドで実行し、内部の粒子ループを OpenMP で並列化。`Update` 中に `GetRenderData` を別スレッドから呼ぶことは不可。

### 7.1 公開関数

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `void Initialize(const Terrain* terrain, const SPHParams& params)` | `terrain`: 衝突判定用（SPH より長寿命であること）<br>`params`: パラメータ | — | カーネル係数を事前計算し、`maxParticles` 分の配列とハッシュ表（2 のべき乗、≥ 2N）を確保。最後に `Reset`。 |
| `void Reset()` | — | — | 粒子数 0、経過時間 0、エミッタタイマー・乱数を初期化。 |
| `void Update(float frameTime)` | `frameTime`: 前フレームからの経過 [s] | — | `ceil(frameTime / timeStep)` 回（最大 `maxSubsteps`）`Step` を実行。 |
| `const std::vector<XMFLOAT4>& GetRenderData()` | — | `(x, y, z, 速さ)` × 粒子数 | 速さは近傍平均で平滑化（描画の色用）。直前ステップの近傍リストを使用。 |
| `int GetParticleCount() const` | — | 有効粒子数 | |
| `float GetSimTime() const` | — | シミュレーション経過時間 [s] | |
| `const SPHParams& GetParams() const` | — | パラメータ | |

### 7.2 1 サブステップの処理（`Step`、内部）

| 順 | 関数 | 並列 | 内容 |
|---|---|---|---|
| 1 | `Emit(dt)` / `EmitSheet()` | — | タイマーが `spacing/emitSpeed` を超えるごとに `emitWidth × emitHeight` の板を上流に配置（ジッタ付き） |
| 2 | `BuildGrid()` | 一部 | セル座標 `floor(p/h)`、ハッシュ `(cx·73856093 ^ cy·19349663 ^ cz·83492791) & mask`、カウンティングソートで `cellStart`/`sorted` を構築 |
| 3 | `BuildNeighborLists()` | ○ | 周囲 27 セルを走査し `r² < h²` の粒子を最大 `maxNeighbors` 個記録（セル座標一致でハッシュ衝突を除外） |
| 4 | `ComputeDensityPressure()` | ○ | `ρ_i = Σ m W_poly6`, `p_i = k·max(ρ_i-ρ0, 0)` |
| 5 | `ComputeForces()` | ○ | 圧力力（spiky 勾配）+ 粘性力（visc ラプラシアン）→ 加速度をクランプ → 重力を加算 |
| 6 | `Integrate()` → `ResolveCollision(i)` | ○ | シンプレクティック・オイラー、速度クランプ、側壁/地形衝突、接触ゾーン減衰、孤立粒子の空気抵抗 |
| 7 | `RemoveDrained()` | — | `z > HALF_SIZE-0.5` または `y < -60` の粒子を末尾と入れ替えて削除 |

### 7.3 カーネル（h = smoothingRadius）

```
W_poly6(r)  = 315 / (64 π h^9) · (h² - r²)³         0 ≤ r ≤ h
∇W_spiky(r) = -45 / (π h^6) · (h - r)² · r̂          0 < r ≤ h
∇²W_visc(r) =  45 / (π h^6) · (h - r)               0 ≤ r ≤ h
```

---

## 8. FluidRenderer

ファイル: `src/FluidRenderer.h`, `src/FluidRenderer.cpp`
役割: 粒子の描画。2 モードを持つ。

```cpp
enum class FluidRenderMode { Particles, Surface };
```

| 関数 | 引数 | 戻り値 | 説明 |
|---|---|---|---|
| `bool Initialize(Graphics& gfx, int maxParticles)` | `maxParticles`: 粒子バッファ要素数 | 成功で `true` | `StructuredBuffer<float4>`（DYNAMIC）+ SRV、`Particle.hlsl` / `FluidSurface.hlsl` の 6 シェーダー、R32_FLOAT 深度テクスチャ ×2 を作成。 |
| `bool OnResize(Graphics& gfx)` | — | 成功で `true` | 深度テクスチャを現在の画面サイズで作り直す。`Graphics::Resize` の後に呼ぶ。 |
| `void Render(Graphics& gfx, const std::vector<XMFLOAT4>& particles, FluidRenderMode mode, FrameConstants& frame, ID3D11Buffer* cbFrame)` | `particles`: `(x,y,z,速さ)`<br>`mode`: 表示モード<br>`frame`: フレーム定数（`Params` を書き換える）<br>`cbFrame`: `b0` バッファ | — | 前提: 地形描画済み（バックバッファ + 深度が有効）。粒子数 0 なら何もしない。終了時は「バックバッファ + DSV」をバインドし直す。 |
| `void SetParticleRadius(float r)` | 半径 [m] | — | 粒子モードの球半径（既定 0.15） |
| `void SetSurfaceRadius(float r)` | 半径 [m] | — | 表面モードの球半径（既定 0.30、main では `spacing × 1.3`） |
| `void SetBlurIterations(int n)` | 回数 | — | 横+縦を 1 回とした反復回数（既定 4） |

### 8.1 描画パス

**Particles モード**
1. `Params.x = particleRadius` を `b0` に転送
2. バックバッファ + DSV に `DrawInstanced(4, N)`（`VSMain` / `PSColor`、深度書き込みあり、カリングなし）

**Surface モード**
1. `depthTex[0]` を 0 でクリア → メイン DSV と組で球の深度を描く（`PSDepth`）
2. `iterations` 回: `[0]→[1]` 横ぼかし、`[1]→[0]` 縦ぼかし（`PSBlur`、`Params.zw` で方向指定）
3. バックバッファへ `PSComposite` をアルファブレンドで合成（深度テストなし）

---

## 9. main.cpp（アプリケーション層）

ファイル: `src/main.cpp`
グローバル状態 `AppState g_app` に全モジュールを保持。

| 関数 | 説明 |
|---|---|
| `bool FileExists(const std::wstring& path)` | ファイル存在チェック |
| `bool FindShaderDirectory()` | `shaders` → `exe\shaders` → `exe\..\shaders` → `exe\..\..\shaders` の順に `Common.hlsli` を探し `Graphics::SetShaderDirectory` |
| `void UpdateFrameConstants()` | カメラ・光源から `FrameConstants` を組み立て `b0` に転送し VS/PS にバインド。光源方向 `normalize(-0.35, 0.85, 0.45)` |
| `void UpdateTitle(HWND)` | タイトルバーに粒子数・FPS・モードを表示 |
| `LRESULT WndProc(...)` | 入力処理（下表） |
| `int wWinMain(...)` | ウィンドウ作成（クライアント 1280×800）→ 初期化 → メインループ |

### 9.1 入力仕様

| 入力 | 動作 |
|---|---|
| 左ドラッグ | `Camera::Rotate(-dx·0.005, dy·0.005)` |
| 右ドラッグ | `Camera::Pan(-dx·0.05, dy·0.05)` |
| ホイール | `Camera::Zoom(0.9 or 1.1)` |
| `1` / `2` | 粒子モード / 表面生成モード |
| `Space` | 一時停止トグル |
| `R` | `SPH::Reset()` |
| `Esc` | 終了 |
| ウィンドウリサイズ | `Graphics::Resize` → `FluidRenderer::OnResize` |

---

## 10. HLSL シェーダー インターフェース

### 10.1 共通（`Common.hlsli`）

| 項目 | 内容 |
|---|---|
| `cbuffer CBFrame : register(b0)` | `FrameConstants` と同一配置 |
| `float3 JetColor(float t)` | 0〜1 → 青→水色→緑→黄→赤 |
| `FullscreenVSOut VSFullscreen(uint vid : SV_VertexID)` | 頂点バッファなしで画面全体を覆う三角形 |

### 10.2 `Terrain.hlsl`

| エントリ | 入力 | 出力 | リソース |
|---|---|---|---|
| `VSMain` | `POSITION, NORMAL, TEXCOORD0` | `SV_POSITION`, ワールド座標, 法線, UV | — |
| `PSMain` | 上記 | `SV_Target` (RGBA) | `t0`: 岩テクスチャ, `s0`: LinearWrap |

### 10.3 `Particle.hlsl`

| エントリ | 入力 | 出力 | リソース |
|---|---|---|---|
| `VSMain` | `SV_VertexID` (0〜3), `SV_InstanceID` | ビルボード頂点, uv(-1〜1), 球中心(ビュー), 速さ | `t0`: `StructuredBuffer<float4>` 粒子 |
| `PSColor` | 上記 | `SV_Target` 色, `SV_Depth` | `Params.x` = 半径。`MAX_SPEED_FOR_COLOR = 6.0` で赤 |
| `PSDepth` | 上記 | `SV_Target` ビュー空間 z (R32_FLOAT), `SV_Depth` | 同上 |

### 10.4 `FluidSurface.hlsl`

| エントリ | 入力 | 出力 | リソース / 定数 |
|---|---|---|---|
| `PSBlur` | フルスクリーン | ぼかした深度 (0 = 流体なし) | `t0`: 深度, `Params.zw`: 方向。`BLUR_WORLD_RADIUS=0.9`, `BLUR_DEPTH_SIGMA=1.5`, `BLUR_MAX_TAPS=24` |
| `PSComposite` | フルスクリーン | 水面色 (α=0.9)。深度 0 なら `discard` | `t0`: ぼかし済み深度。`Proj._m00/_m11` で位置復元 |

---

## 11. エラー処理方針

- 初期化系関数は `bool` を返し、失敗時は `false`。呼び出し側（`wWinMain`）は `return 1` で終了。
- D3D11 デバイス作成失敗・HLSL コンパイル失敗・`shaders` フォルダ未検出はメッセージボックスで通知。
- シミュレーション（`SPH`）は例外を投げない。発散対策として速度上限・加速度上限・サブステップ上限を持つ。
- リソースはすべて `Microsoft::WRL::ComPtr` で管理し、デストラクタで自動解放。
