# フロー図 — water_from_mountain

処理の流れを Mermaid 図で示します（GitHub 上でそのまま描画されます）。

## 1. モジュール構成

```mermaid
graph TB
    subgraph App["アプリケーション層 (main.cpp)"]
        WND[ウィンドウ / 入力処理<br/>WndProc]
        LOOP[メインループ<br/>wWinMain]
    end

    subgraph Sim["シミュレーション"]
        SPH[SPH<br/>流体粒子]
        TER[Terrain<br/>地形 / 高さマップ]
        NOISE[Noise<br/>パーリンノイズ]
        TL[TextureLoader<br/>WIC 画像読み込み]
        ML[MeshLoader<br/>OBJ 読み込み]
    end

    subgraph Render["描画"]
        GFX[Graphics<br/>D3D11 デバイス / ステート]
        CAM[Camera<br/>オービットカメラ]
        FR[FluidRenderer<br/>粒子 / 表面生成]
        CB[FrameConstants<br/>定数バッファ b0]
    end

    subgraph GPU["HLSL"]
        HT[Terrain.hlsl]
        HP[Particle.hlsl]
        HF[FluidSurface.hlsl]
        HC[Common.hlsli]
    end

    WND -->|回転/ズーム/移動| CAM
    WND -->|モード / 一時停止 / リセット| LOOP
    LOOP -->|Update dt| SPH
    LOOP -->|Render| TER
    LOOP -->|Render| FR
    LOOP --> CB
    SPH -->|GetHeight / GetNormal<br/>衝突判定| TER
    TER --> NOISE
    TER -->|岩テクスチャ生成 フォールバック| NOISE
    TER -->|assets/rock.png| TL
    TER -->|assets/terrain.obj| ML
    CAM -->|View / Proj| CB
    CB --> HC
    TER --> GFX
    FR --> GFX
    TER -.-> HT
    FR -.-> HP
    FR -.-> HF
    HT --> HC
    HP --> HC
    HF --> HC
```

## 2. 起動から終了までの全体フロー

```mermaid
flowchart TD
    START([起動 wWinMain]) --> REG[ウィンドウクラス登録<br/>CreateWindow 1280x800]
    REG --> FSD{FindProjectDirectories<br/>shaders / assets フォルダ検出}
    FSD -->|見つからない| ERR1[メッセージボックス] --> END1([終了 1])
    FSD -->|見つかった| GI[Graphics::Initialize<br/>デバイス / スワップチェーン / RTV / DSV / ステート]
    GI --> CBUF[CreateConstantBuffer<br/>FrameConstants]
    CBUF --> TI[Terrain::Initialize]
    TI --> TM{assets/terrain.obj<br/>MeshLoader::LoadObj}
    TM -->|読み込み成功| TM1[x 反転 右手系→左手系<br/>自動フィット 60 m<br/>巻き順統一]
    TM1 --> TM2[BakeHeightMap<br/>三角形を真上から高さマップに焼き込み<br/>穴埋め]
    TM2 --> TM3[CreateGpuBuffers<br/>OBJ の頂点 / 法線 / UV]
    TM3 --> TI3
    TM -->|ファイルなし / 失敗| TI1[BuildHeightMap<br/>257x257 で HeightFunction 評価]
    TI1 --> TI2[BuildMesh<br/>頂点 / 法線 / UV / インデックス]
    TI2 --> TI3{CreateRockTexture<br/>assets/rock.png 等を<br/>TextureLoader::LoadFromFile}
    TI3 -->|読み込み成功| TI3B[画像テクスチャ + GenerateMips]
    TI3 -->|ファイルなし / 失敗| TI3C[CreateProceduralRockTexture<br/>1024x1024 ノイズ生成 + GenerateMips]
    TI3B --> TI4[CreateShaders<br/>Terrain.hlsl]
    TI3C --> TI4
    TI4 --> SI[SPH::Initialize<br/>カーネル係数 / 配列 / ハッシュ表]
    SI --> FI[FluidRenderer::Initialize<br/>粒子バッファ / 6 シェーダー / 深度テクスチャ x2]
    FI --> SHOW[ShowWindow]
    SHOW --> LOOP{メッセージあり?}
    LOOP -->|あり| MSG[TranslateMessage<br/>DispatchMessage → WndProc]
    MSG --> QUIT{WM_QUIT?}
    QUIT -->|はい| END2([終了])
    QUIT -->|いいえ| LOOP
    LOOP -->|なし| FRAME[1 フレーム処理<br/>（図 3 参照）]
    FRAME --> LOOP
```

## 3. 1 フレームの処理

```mermaid
flowchart TD
    A([フレーム開始]) --> DT[経過時間 dt を計測<br/>steady_clock]
    DT --> P{一時停止中?}
    P -->|いいえ| SU[SPH::Update dt<br/>（図 4 参照）]
    P -->|はい| BF
    SU --> BF[Graphics::BeginFrame<br/>白でクリア / RTV+DSV 設定]
    BF --> UFC[UpdateFrameConstants<br/>View / Proj / ViewProj 転置<br/>光源 / 画面サイズ → b0]
    UFC --> TR[Terrain::Render<br/>DrawIndexed]
    TR --> RD[SPH::GetRenderData<br/>x,y,z + 平滑化した速さ]
    RD --> MODE{表示モード}
    MODE -->|1: 粒子| PM[粒子モード描画<br/>（図 6 参照）]
    MODE -->|2: 表面| SM[表面生成モード描画<br/>（図 7 参照）]
    PM --> EF[Graphics::EndFrame<br/>Present vsync]
    SM --> EF
    EF --> FPS[FPS 計測 0.5 s ごと<br/>タイトル更新 0.25 s ごと]
    FPS --> Z([フレーム終了])
```

## 4. SPH::Update — サブステップ制御

```mermaid
flowchart TD
    U([SPH::Update frameTime]) --> N[substeps = ceil frameTime / timeStep]
    N --> C[substeps = clamp 1 .. maxSubsteps]
    C --> L{残りサブステップ > 0?}
    L -->|はい| S[Step<br/>（図 5 参照）]
    S --> L
    L -->|いいえ| R([戻る])
```

## 5. SPH::Step — 1 サブステップの物理計算

```mermaid
flowchart TD
    S([Step]) --> EM[Emit dt<br/>タイマー >= spacing/emitSpeed ごとに<br/>EmitSheet: emitWidth x emitHeight の板を投入]
    EM --> C0{粒子数 > 0?}
    C0 -->|いいえ| T
    C0 -->|はい| BG[BuildGrid<br/>セル座標 floor p/h → ハッシュ<br/>カウンティングソート → cellStart / sorted]
    BG --> NL[BuildNeighborLists ‖並列<br/>周囲 27 セルを走査<br/>r² < h² かつ セル座標一致 → 記録]
    NL --> DP[ComputeDensityPressure ‖並列<br/>ρ = Σ m·W_poly6<br/>p = k·max ρ-ρ0, 0]
    DP --> F[ComputeForces ‖並列<br/>圧力力 spiky 勾配<br/>粘性力 visc ラプラシアン<br/>加速度クランプ + 重力]
    F --> I[Integrate ‖並列<br/>v += a·dt, 速度クランプ<br/>x += v·dt]
    I --> RC[ResolveCollision<br/>（図 5-1 参照）]
    RC --> RM[RemoveDrained<br/>z > 下流端 or y < -60 の粒子を<br/>末尾と入れ替えて削除]
    RM --> T[time += dt]
    T --> E([戻る])
```

### 5-1. ResolveCollision — 粒子 1 個の境界処理

```mermaid
flowchart TD
    A([ResolveCollision i]) --> W[側壁 x = ±limit / 上流壁 z = -limit<br/>越えていれば壁面に戻し法線速度を 0]
    W --> H[floorY = Terrain::GetHeight x,z + 半径]
    H --> Q1{y < floorY?}
    Q1 -->|はい| PUSH[y = floorY<br/>n = Terrain::GetNormal]
    PUSH --> VN{v·n < 0?}
    VN -->|はい| REFL[v -= 1+e · v·n · n<br/>めり込む速度成分を除去]
    VN -->|いいえ| FRIC
    REFL --> FRIC[v *= 1 - friction]
    Q1 -->|いいえ| CZ
    FRIC --> CZ{高さ < contactZone?}
    CZ -->|はい| CD[t = 1 - 高さ/contactZone<br/>v *= 1 - contactDamping·t]
    CZ -->|いいえ| SP
    CD --> SP{近傍数 < sparseNeighbors?}
    SP -->|はい| AD[v *= 1 - airDrag<br/>飛沫の勢いを削ぐ]
    SP -->|いいえ| Z
    AD --> Z([戻る])
```

## 6. 粒子モード描画（FluidRenderer, mode = Particles）

```mermaid
flowchart LR
    U[UploadParticles<br/>Map/Unmap で StructuredBuffer へ] --> P[Params.x = particleRadius<br/>→ b0 更新]
    P --> RT[OMSetRenderTargets<br/>バックバッファ + メイン DSV]
    RT --> D[DrawInstanced 4, N<br/>トライアングルストリップ / カリングなし]
    D --> VS[VSMain<br/>粒子中心をビュー空間へ<br/>SV_VertexID から 4 隅を生成]
    VS --> PS[PSColor<br/>uv² > 1 → discard<br/>法線 = uv, -sqrt 1-r²<br/>SV_Depth に球面の深度<br/>JetColor 速さ/6 + 拡散 + 鏡面]
```

## 7. 表面生成モード描画（FluidRenderer, mode = Surface）

```mermaid
flowchart TD
    U[UploadParticles] --> P1[Params.x = surfaceRadius → b0]
    P1 --> CL[depthTex 0 を 0 でクリア<br/>0 = 流体なし]
    CL --> D1[RT = depthTex 0 + メイン DSV<br/>DrawInstanced → PSDepth<br/>ビュー空間 z を出力 / 地形と深度テスト]
    D1 --> LOOP{反復 < blurIterations?}
    LOOP -->|はい| BH[Params.zw = 1,0<br/>depthTex 0 → 1 横ぼかし<br/>PSBlur]
    BH --> BV[Params.zw = 0,1<br/>depthTex 1 → 0 縦ぼかし<br/>PSBlur]
    BV --> LOOP
    LOOP -->|いいえ| CMP[RT = バックバッファ 深度なし<br/>アルファブレンド ON<br/>PSComposite depthTex 0]
    CMP --> N[法線復元<br/>ViewPosFromDepth で隣接 4 画素の位置<br/>差の小さい側を採用<br/>n = normalize cross ddx, ddy]
    N --> SH[シェーディング<br/>拡散 + Blinn-Phong 鏡面 + Schlick フレネル<br/>α = 0.9]
    SH --> RS[RT を バックバッファ + DSV に戻す]
```

### 7-1. PSBlur — バイラテラルぼかし（1 画素）

```mermaid
flowchart TD
    A([PSBlur pix]) --> D0[d0 = Depth pix]
    D0 --> Q{d0 <= 0?}
    Q -->|はい| R0([0 を返す])
    Q -->|いいえ| RP[radiusPx = BLUR_WORLD_RADIUS / d0 · Proj._m11 · H/2<br/>clamp 2 .. 24]
    RP --> L{k = -24 .. 24}
    L --> K1{abs k > radiusPx?}
    K1 -->|はい| L
    K1 -->|いいえ| DK[d = Depth pix + dir·k]
    DK --> K2{d <= 0?}
    K2 -->|はい| L
    K2 -->|いいえ| WGT[w = exp -k²/2σs² · exp -d-d0 ²/2σd²<br/>sum += d·w, wsum += w]
    WGT --> L
    L -->|終了| OUT([sum / wsum を返す])
```

## 8. 入力イベント処理（WndProc）

```mermaid
flowchart LR
    M([WM_*]) --> K{メッセージ}
    K -->|WM_LBUTTONDOWN| LB[左ドラッグ開始<br/>SetCapture]
    K -->|WM_RBUTTONDOWN| RB[右ドラッグ開始<br/>SetCapture]
    K -->|WM_MOUSEMOVE| MV{ボタン}
    MV -->|左| ROT[Camera::Rotate]
    MV -->|右| PAN[Camera::Pan]
    K -->|WM_MOUSEWHEEL| ZM[Camera::Zoom 0.9 / 1.1]
    K -->|WM_KEYDOWN 1| M1[mode = Particles]
    K -->|WM_KEYDOWN 2| M2[mode = Surface]
    K -->|WM_KEYDOWN Space| PS[paused 反転]
    K -->|WM_KEYDOWN R| RS[SPH::Reset]
    K -->|WM_KEYDOWN Esc| QT[PostQuitMessage]
    K -->|WM_SIZE| SZ[Graphics::Resize<br/>FluidRenderer::OnResize]
    K -->|WM_DESTROY| DS[PostQuitMessage]
```

## 9. GPU リソースとデータの流れ

```mermaid
graph LR
    subgraph CPU
        HM[高さマップ<br/>float 257x257]
        PD[粒子配列<br/>pos / vel / acc / ρ / p]
        RD[描画用配列<br/>float4 x,y,z,速さ]
        FC[FrameConstants]
    end
    subgraph GPU
        VB[頂点/インデックスバッファ<br/>IMMUTABLE]
        TX[岩テクスチャ<br/>R8G8B8A8 + mips]
    end
    subgraph Disk
        IMG[assets/rock.png]
        OBJ[assets/terrain.obj]
        SB[StructuredBuffer float4<br/>DYNAMIC]
        CB[cbuffer b0<br/>DYNAMIC]
        DT0[depthTex 0<br/>R32_FLOAT]
        DT1[depthTex 1<br/>R32_FLOAT]
        BB[バックバッファ<br/>R8G8B8A8]
        DS[深度バッファ<br/>D24S8]
    end
    IMG -->|起動時 1 回 WIC| TX
    OBJ -->|起動時 1 回 OBJ 解析| VB
    OBJ -->|BakeHeightMap| HM
    HM -->|起動時 1 回| VB
    HM -->|GetHeight/GetNormal| PD
    PD -->|GetRenderData| RD
    RD -->|毎フレーム Map| SB
    FC -->|毎フレーム Map| CB
    VB --> BB
    TX --> BB
    SB -->|粒子モード| BB
    SB -->|表面モード PSDepth| DT0
    DT0 <-->|ぼかし ピンポン| DT1
    DT0 -->|PSComposite| BB
    BB --- DS
```
