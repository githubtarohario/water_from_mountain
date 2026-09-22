//=============================================================================
// Terrain.cpp
//   山の地形の生成と描画の実装。
//=============================================================================
#include "Terrain.h"
#include "Noise.h"
#include "TextureLoader.h"
#include "MeshLoader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

using namespace DirectX;

namespace
{
    //-------------------------------------------------------------------------
    // 関数名 : SmoothStep
    // 概要   : x が edge0 以下なら 0、edge1 以上なら 1、その間を 3t^2 - 2t^3 で滑らかにつなぐ
    // 引数   : edge0, edge1 : 遷移の開始・終了値
    //          x            : 入力値
    // 戻り値 : 0～1
    //-------------------------------------------------------------------------
    inline float SmoothStep(float edge0, float edge1, float x)
    {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);  // 区間内の位置を 0～1 に
        return t * t * (3.0f - 2.0f * t);   // 両端で傾きが 0 になる 3 次曲線 (折れ目なくつながる)
    }

    //-------------------------------------------------------------------------
    // 関数名 : Saturate
    // 概要   : 値を 0～1 に切り詰める
    //-------------------------------------------------------------------------
    inline float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }

    //-------------------------------------------------------------------------
    // 関数名 : Lerp3
    // 概要   : 3 成分 (RGB) の線形補間
    //-------------------------------------------------------------------------
    inline XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
    {
        return XMFLOAT3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }
}

//-----------------------------------------------------------------------------
// ValleyCenterX
//   周期の異なる 2 つの sin を足して自然な蛇行を作る。
//   振幅 4.0 と 2.0 なので中心線は x = -6～+6 の範囲で揺れる。
//-----------------------------------------------------------------------------
float Terrain::ValleyCenterX(float z)
{
    // 周期の違う 2 つの波を足すことで、単純な蛇行より自然な曲がり方になる。
    // 0.16, 0.41 が波の細かさ、4.0, 2.0 が曲がりの大きさ [m]、1.7 は位相のずらし。
    return 4.0f * std::sin(z * 0.16f) + 2.0f * std::sin(z * 0.41f + 1.7f);
}

//-----------------------------------------------------------------------------
// HeightFunction
//   アルゴリズム:
//     d        = |x - 谷の中心 x|             … 谷の中心からの水平距離
//     wallT    = smoothstep(1.6, 8.5, d)      … 0 = 谷底, 1 = 山の斜面
//     base     = -0.20 * z                    … 上流 (z=-30) が高く下流 (z=+30) が低い傾斜
//     floorU   = min(d,1.6)^2 * 0.3           … 谷底を浅い U 字断面にして水を中央へ集める
//     mountain = 11.0 * wallT                 … 両側の山の高さ
//     rough    = fBm * (0.25 + 5.0 * wallT)   … 谷底は小さく、山肌は大きく凹凸を付ける
//     ridge    = ridged * 1.5 * wallT         … 山肌の稜線状の岩の筋
//     h        = base + floorU + mountain + rough + ridge
//-----------------------------------------------------------------------------
float Terrain::HeightFunction(float x, float z)
{
    const float cx    = ValleyCenterX(z);           // この z における谷の中心の x 座標 [m]
    const float d     = std::fabs(x - cx);          // 谷の中心からの水平距離 [m]
    // 谷底 (d < 1.6) では 0、山の斜面 (d > 8.5) では 1 になる重み。
    // この値を各項に掛けることで「谷底では控えめ、山では大きく」を一括で表現する。
    const float wallT = SmoothStep(1.6f, 8.5f, d);

    const float base     = -0.20f * z;              // 全体の傾斜: 上流 (-z) が高く下流 (+z) が低い
    const float dFloor   = std::min(d, 1.6f);       // 谷底の範囲に限った距離
    const float floorU   = dFloor * dFloor * 0.3f;  // 2 乗なので U 字の断面になり、水が中央へ集まる
    const float mountain = 11.0f * wallT;           // 両側の山の高さ [m]

    // 座標にかける 0.05 がノイズの細かさ (小さいほど大きなうねり)、
    // +7.3, +2.1 はノイズの「どの場所を使うか」をずらすためのオフセット。
    const float fbm   = Noise::Fbm2D(x * 0.05f + 7.3f, z * 0.05f + 2.1f, 6);  // -1～1 の起伏
    const float rough = fbm * (0.25f + 5.0f * wallT);   // 谷底は ±0.25 m、山肌は ±5 m の凹凸

    // 尾根状ノイズ: 山肌に鋭い岩の筋を入れる (0.12 は fBm より細かい模様にするため)
    const float ridge = Noise::Ridged2D(x * 0.12f + 3.0f, z * 0.12f + 5.0f, 4) * 1.5f * wallT;

    return base + floorU + mountain + rough + ridge;    // すべて足し合わせた高さ [m]
}

//-----------------------------------------------------------------------------
// Initialize
//-----------------------------------------------------------------------------
bool Terrain::Initialize(Graphics& gfx)
{
    // A. OBJ メッシュがあればそれを使う / B. なければ手続き生成
    if (!LoadMeshTerrain(gfx))              // assets/terrain.obj を読み込む (無ければ false)
    {
        m_meshFromFile = false;
        BuildHeightMap();                   // HeightFunction を格子状に評価して高さマップを作る
        if (!BuildMesh(gfx))      return false;   // 高さマップから頂点・三角形を作り GPU へ送る
    }
    if (!CreateRockTexture(gfx))  return false;   // 岩肌テクスチャ (画像読み込み、失敗ならノイズ生成)
    if (!CreateShaders(gfx))      return false;   // Terrain.hlsl をコンパイルして描画の準備
    return true;
}

//-----------------------------------------------------------------------------
// BuildHeightMap
//   格子点 (ix, iz) のワールド座標は
//     x = -HALF_SIZE + ix * cellSize,  z = -HALF_SIZE + iz * cellSize
//-----------------------------------------------------------------------------
void Terrain::BuildHeightMap()
{
    m_heights.resize(static_cast<size_t>(GRID_N) * GRID_N);   // 257 × 257 個の高さを格納する
    for (int iz = 0; iz < GRID_N; ++iz)     // iz: 奥行き方向の格子番号
    {
        const float z = -HALF_SIZE + iz * m_cellSize;   // 格子番号 → ワールド座標 [m]
        for (int ix = 0; ix < GRID_N; ++ix) // ix: 横方向の格子番号
        {
            const float x = -HALF_SIZE + ix * m_cellSize;
            // 2 次元配列を 1 次元で持っているので、添え字は iz 行目の先頭 + ix
            m_heights[static_cast<size_t>(iz) * GRID_N + ix] = HeightFunction(x, z);
        }
    }
}

//-----------------------------------------------------------------------------
// GetHeight
//   アルゴリズム (双線形補間):
//     1. ワールド座標 → 格子座標 (fx, fz) に変換
//     2. 整数部 (ix, iz) と小数部 (tx, tz) に分ける
//     3. 4 隅の高さ h00, h10, h01, h11 を tx, tz で補間
//-----------------------------------------------------------------------------
float Terrain::GetHeight(float x, float z) const
{
    // ワールド座標 → 格子座標 (小数)。-30 m が 0、+30 m が 256 になる
    float fx = (x + HALF_SIZE) / m_cellSize;
    float fz = (z + HALF_SIZE) / m_cellSize;
    // -0.001 は、ちょうど端 (256.0) のときに ix+1 が配列の外に出ないようにするための余裕
    fx = std::clamp(fx, 0.0f, static_cast<float>(GRID_N - 1) - 0.001f);
    fz = std::clamp(fz, 0.0f, static_cast<float>(GRID_N - 1) - 0.001f);

    const int ix = static_cast<int>(fx);    // 左下の格子点の番号 (整数部)
    const int iz = static_cast<int>(fz);
    const float tx = fx - ix;               // セル内での位置 0～1 (小数部)
    const float tz = fz - iz;

    // 周囲 4 点の高さ (h[z][x] の形。00 = 左下、10 = 右下、01 = 左上、11 = 右上)
    const float h00 = m_heights[static_cast<size_t>(iz) * GRID_N + ix];
    const float h10 = m_heights[static_cast<size_t>(iz) * GRID_N + ix + 1];
    const float h01 = m_heights[static_cast<size_t>(iz + 1) * GRID_N + ix];
    const float h11 = m_heights[static_cast<size_t>(iz + 1) * GRID_N + ix + 1];

    const float h0 = h00 + (h10 - h00) * tx;   // 下側の辺を x 方向に補間
    const float h1 = h01 + (h11 - h01) * tx;   // 上側の辺を x 方向に補間
    return h0 + (h1 - h0) * tz;                // z 方向に補間
}

//-----------------------------------------------------------------------------
// GetNormal
//   中心差分: hx = (h(x+e) - h(x-e)) / 2e,  hz = (h(z+e) - h(z-e)) / 2e
//   曲面 y = h(x,z) の法線は (-hx, 1, -hz) を正規化したもの
//-----------------------------------------------------------------------------
XMFLOAT3 Terrain::GetNormal(float x, float z) const
{
    const float e  = m_cellSize;            // 差分を取る幅 [m] (格子 1 マスぶん)
    // 中心差分で傾きを求める: (右の高さ - 左の高さ) / 2e ≒ ∂h/∂x
    const float hx = (GetHeight(x + e, z) - GetHeight(x - e, z)) / (2.0f * e);
    const float hz = (GetHeight(x, z + e) - GetHeight(x, z - e)) / (2.0f * e);

    // 曲面 y = h(x,z) の法線は (-∂h/∂x, 1, -∂h/∂z)。平らな地面なら (0,1,0) になる
    XMVECTOR n = XMVector3Normalize(XMVectorSet(-hx, 1.0f, -hz, 0.0f));
    XMFLOAT3 out;
    XMStoreFloat3(&out, n);
    return out;
}

//-----------------------------------------------------------------------------
// BuildMesh
//   アルゴリズム:
//     頂点 : 格子点ごとに位置・法線・UV を作る (UV はワールド座標に比例させてタイリング)
//     索引 : 各セルを 2 枚の三角形に分割する。
//            D3D の既定では「画面上で時計回り」が表面なので、
//            上から見下ろしたときに時計回りになる順序で並べる。
//-----------------------------------------------------------------------------
bool Terrain::BuildMesh(Graphics& gfx)
{
    // ---- 頂点 ----
    std::vector<Vertex> vertices(static_cast<size_t>(GRID_N) * GRID_N);   // 格子点と同数の頂点
    const float uvScale = 0.12f;   // ワールド 1 単位あたりの UV 進み (約 8.3 単位でテクスチャ 1 周)

    for (int iz = 0; iz < GRID_N; ++iz)
    {
        const float z = -HALF_SIZE + iz * m_cellSize;
        for (int ix = 0; ix < GRID_N; ++ix)
        {
            const float x = -HALF_SIZE + ix * m_cellSize;
            Vertex& v = vertices[static_cast<size_t>(iz) * GRID_N + ix];
            v.pos    = XMFLOAT3(x, m_heights[static_cast<size_t>(iz) * GRID_N + ix], z);  // 位置
            v.normal = GetNormal(x, z);                     // 陰影付けに使う法線
            v.uv     = XMFLOAT2(x * uvScale, z * uvScale);  // 位置に比例した UV (真上から貼る)
        }
    }

    // ---- インデックス ----
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(GRID_N - 1) * (GRID_N - 1) * 6);
    for (int iz = 0; iz < GRID_N - 1; ++iz)
    {
        for (int ix = 0; ix < GRID_N - 1; ++ix)
        {
            // 1 つの四角いセルの 4 隅の頂点番号
            const uint32_t i00 = static_cast<uint32_t>(iz * GRID_N + ix);          // (x0, z0)
            const uint32_t i10 = i00 + 1;                                           // (x1, z0)
            const uint32_t i01 = i00 + GRID_N;                                      // (x0, z1)
            const uint32_t i11 = i01 + 1;                                           // (x1, z1)

            // 四角形を対角線で 2 枚の三角形に割る。
            // 上から見て時計回りに並べると、D3D の既定で「表向き」と判定される。
            // 三角形 1: (x0,z0) → (x1,z1) → (x1,z0)
            indices.push_back(i00); indices.push_back(i11); indices.push_back(i10);
            // 三角形 2: (x0,z0) → (x0,z1) → (x1,z1)
            indices.push_back(i00); indices.push_back(i01); indices.push_back(i11);
        }
    }
    return CreateGpuBuffers(gfx, vertices, indices);
}

//-----------------------------------------------------------------------------
// CreateGpuBuffers
//-----------------------------------------------------------------------------
bool Terrain::CreateGpuBuffers(Graphics& gfx, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    m_indexCount = static_cast<UINT>(indices.size());   // 描画時に DrawIndexed へ渡す個数

    // IMMUTABLE: 作成後に変更しない (GPU 側で最も高速に扱える)
    D3D11_BUFFER_DESC vbd = {};
    vbd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));   // バッファ全体のバイト数
    vbd.Usage     = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;          // 頂点バッファとして使う
    D3D11_SUBRESOURCE_DATA vinit = { vertices.data(), 0, 0 };   // 作成時に流し込む中身
    if (FAILED(gfx.GetDevice()->CreateBuffer(&vbd, &vinit, m_vertexBuffer.ReleaseAndGetAddressOf())))
        return false;

    D3D11_BUFFER_DESC ibd = {};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibd.Usage     = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = { indices.data(), 0, 0 };
    if (FAILED(gfx.GetDevice()->CreateBuffer(&ibd, &iinit, m_indexBuffer.ReleaseAndGetAddressOf())))
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// LoadMeshTerrain
//-----------------------------------------------------------------------------
bool Terrain::LoadMeshTerrain(Graphics& gfx)
{
    MeshLoader::MeshData mesh;              // 読み込んだ頂点と三角形が入る
    // AssetPath は assets フォルダのフルパスを作る。LoadObj は OBJ を解析して三角形リストにする
    if (!MeshLoader::LoadObj(Graphics::AssetPath(TERRAIN_MESH_FILE), mesh))
        return false;                       // ファイルが無い / 読めない → 手続き生成に切り替わる

    // ---- 1. 右手系 (Blender/OBJ) → 左手系 (DirectX): x を反転 ----
    // 鏡に映す操作なので、位置だけでなく法線の向きも同じように反転させる
    for (auto& v : mesh.vertices)
    {
        v.pos.x    = -v.pos.x;
        v.normal.x = -v.normal.x;
    }
    MeshLoader::ComputeBounds(mesh);        // 変換後の大きさを測り直す (次のフィットで使う)

    // ---- 2. 自動フィット: xz の大きい方の辺を WORLD_SIZE に、中心を原点に ----
    const XMFLOAT3& mn = mesh.boundsMin;    // メッシュを囲む箱の最小座標
    const XMFLOAT3& mx = mesh.boundsMax;    // 同じく最大座標
    const float extent = std::max(mx.x - mn.x, mx.z - mn.z);   // 横幅と奥行きの大きい方 [元の単位]
    if (extent < 1e-6f)
        return false;                       // 大きさが 0 (平面や空データ) なら使えない
    const float scale = (WORLD_SIZE * 0.999f) / extent;   // 端が格子の外に出ないよう僅かに縮める
    const XMFLOAT3 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);  // 箱の中心
    for (auto& v : mesh.vertices)
    {
        // 中心を原点へ移してから拡大縮小する。高さ (y) も同じ倍率なので形は崩れない
        v.pos.x = (v.pos.x - center.x) * scale;
        v.pos.y = (v.pos.y - center.y) * scale;
        v.pos.z = (v.pos.z - center.z) * scale;
    }

    // ---- 3. 三角形の巻き順を統一 ----
    //   左手系 + D3D 既定 (時計回りが表面) では、上を向く面は cross(e1, e2).y > 0 になる
    //   (BuildMesh と同じ規約)。地形は上から見える面が表なので、下向きなら順序を入れ替える。
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const XMFLOAT3& p0 = mesh.vertices[mesh.indices[i]].pos;       // 三角形の 3 頂点
        const XMFLOAT3& p1 = mesh.vertices[mesh.indices[i + 1]].pos;
        const XMFLOAT3& p2 = mesh.vertices[mesh.indices[i + 2]].pos;
        const float e1x = p1.x - p0.x, e1z = p1.z - p0.z;   // 辺ベクトル e1 = p1 - p0 (xz 成分のみ)
        const float e2x = p2.x - p0.x, e2z = p2.z - p0.z;   // 辺ベクトル e2 = p2 - p0
        const float crossY = e1z * e2x - e1x * e2z;   // cross(e1, e2).y : 正なら上向きの面
        if (crossY < 0.0f)
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);   // 裏返っていたら頂点順を入れ替え
    }

    // ---- 4. 衝突判定用の高さマップに焼き込む ----
    BakeHeightMap(mesh);

    // ---- 5. 描画用頂点 (UV が無ければワールド座標から生成) ----
    std::vector<Vertex> vertices(mesh.vertices.size());   // 描画用の頂点配列に詰め替える
    const float uvScale = 0.12f;            // UV が無いときにワールド座標から作るときの倍率
    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const MeshLoader::MeshVertex& mv = mesh.vertices[i];
        vertices[i].pos    = mv.pos;
        vertices[i].normal = mv.normal;
        // Blender で UV を作ってあればそれを使い、無ければ真上から貼り付けた UV を生成する
        vertices[i].uv     = mesh.hasUVs ? mv.uv : XMFLOAT2(mv.pos.x * uvScale, mv.pos.z * uvScale);
    }
    if (!CreateGpuBuffers(gfx, vertices, mesh.indices))   // 頂点・インデックスを GPU へ送る
        return false;

    m_meshFromFile = true;                  // タイトルバーに「地形: OBJ」と出すための印
    LoadTerrainConfig();                    // assets/terrain.cfg があれば投入位置の指定を読む
    return true;
}

//-----------------------------------------------------------------------------
// LoadTerrainConfig
//   例) assets/terrain.cfg
//        emit_x = 12.5
//        emit_z = -27.5
//   '#' 以降はコメント。tools/fetch_gsi_terrain.py が --emit-lat/--emit-lon から生成する。
//-----------------------------------------------------------------------------
void Terrain::LoadTerrainConfig()
{
    std::ifstream file(Graphics::AssetPath(L"terrain.cfg"));
    if (!file)
        return;

    // 前後の空白を取り除く
    auto trim = [](const std::string& s) -> std::string {
        const size_t b = s.find_first_not_of(" \t\r");
        const size_t e = s.find_last_not_of(" \t\r");
        return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    };

    bool hasX = false, hasZ = false;
    std::string line;
    while (std::getline(file, line))
    {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "emit_x") { m_emitX = std::stof(val); hasX = true; }
        if (key == "emit_z") { m_emitZ = std::stof(val); hasZ = true; }
    }
    m_hasEmitOverride = hasX && hasZ;
    if (m_hasEmitOverride)
    {
        // 地形の範囲内に収める
        m_emitX = std::clamp(m_emitX, -HALF_SIZE + 1.0f, HALF_SIZE - 1.0f);
        m_emitZ = std::clamp(m_emitZ, -HALF_SIZE + 1.0f, HALF_SIZE - 1.0f);
    }
}

//-----------------------------------------------------------------------------
// GetEmitPoint
//-----------------------------------------------------------------------------
void Terrain::GetEmitPoint(float defaultZ, float& outX, float& outZ) const
{
    if (m_hasEmitOverride)
    {
        outX = m_emitX;
        outZ = m_emitZ;
        return;
    }
    outZ = defaultZ;
    outX = GetValleyCenterX(defaultZ);
}

//-----------------------------------------------------------------------------
// BakeHeightMap
//   アルゴリズム (三角形ごと):
//     1. xz 平面での包含格子範囲 [ix0, ix1] × [iz0, iz1] を求める
//     2. 各格子点 (x, z) について辺関数 (エッジ関数) で重心座標 (w0, w1, w2) を計算
//     3. すべて ≥ 0 (三角形の内側) なら y = w0 y0 + w1 y1 + w2 y2 を高さ候補にする
//     4. 候補が既存値より高ければ採用 (オーバーハングは上面のみ残る)
//   穴埋め: 覆われなかった格子点は、覆われた 4 近傍の平均で埋める処理を
//           全て埋まるまで繰り返す (最大 GRID_N 回)。
//-----------------------------------------------------------------------------
void Terrain::BakeHeightMap(const MeshLoader::MeshData& mesh)
{
    const float UNSET = -1e30f;   // 未設定を表す番兵 (どんな標高より低い値にしておく)
    m_heights.assign(static_cast<size_t>(GRID_N) * GRID_N, UNSET);   // まず全マスを未設定にする

    // ワールド x → 格子添え字 (小数)。三角形の範囲を格子の番号に直すために使う
    auto toGrid = [this](float w) { return (w + HALF_SIZE) / m_cellSize; };

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const XMFLOAT3& p0 = mesh.vertices[mesh.indices[t]].pos;
        const XMFLOAT3& p1 = mesh.vertices[mesh.indices[t + 1]].pos;
        const XMFLOAT3& p2 = mesh.vertices[mesh.indices[t + 2]].pos;

        // 1. 包含格子範囲
        // 三角形の 3 頂点を格子座標に直す (真上から見た位置)
        const float gx0 = toGrid(p0.x), gz0 = toGrid(p0.z);
        const float gx1 = toGrid(p1.x), gz1 = toGrid(p1.z);
        const float gx2 = toGrid(p2.x), gz2 = toGrid(p2.z);
        // この三角形が覆いうる格子の範囲 (外接する長方形)。地形の外にはみ出さないよう制限する
        const int ix0 = std::max(0, static_cast<int>(std::floor(std::min({ gx0, gx1, gx2 }))));
        const int ix1 = std::min(GRID_N - 1, static_cast<int>(std::ceil(std::max({ gx0, gx1, gx2 }))));
        const int iz0 = std::max(0, static_cast<int>(std::floor(std::min({ gz0, gz1, gz2 }))));
        const int iz1 = std::min(GRID_N - 1, static_cast<int>(std::ceil(std::max({ gz0, gz1, gz2 }))));

        // 三角形の xz 面積の 2 倍 (0 なら真横から見た縮退三角形なので無視)
        const float area = (gx1 - gx0) * (gz2 - gz0) - (gx2 - gx0) * (gz1 - gz0);
        if (std::fabs(area) < 1e-9f)
            continue;                       // 真横を向いた壁のような面は、真上から見ると線になる
        const float invArea = 1.0f / area;  // 重心座標の計算で毎回割らずに済むよう逆数にしておく

        for (int iz = iz0; iz <= iz1; ++iz)
        {
            for (int ix = ix0; ix <= ix1; ++ix)
            {
                const float px = static_cast<float>(ix);   // 調べている格子点の位置 (格子座標)
                const float pz = static_cast<float>(iz);
                // 2. 重心座標: 3 頂点をどれくらいの割合で混ぜればこの点になるか (合計 1)
                const float w0 = ((gx1 - px) * (gz2 - pz) - (gx2 - px) * (gz1 - pz)) * invArea;
                const float w1 = ((gx2 - px) * (gz0 - pz) - (gx0 - px) * (gz2 - pz)) * invArea;
                const float w2 = 1.0f - w0 - w1;
                const float eps = -1e-4f;   // 辺上の格子点も含めるための許容誤差
                if (w0 < eps || w1 < eps || w2 < eps)
                    continue;               // ひとつでも負 = 三角形の外側なので対象外
                // 3-4. 3 頂点の高さを同じ割合で混ぜれば、その点の高さになる
                const float y = w0 * p0.y + w1 * p1.y + w2 * p2.y;
                float& h = m_heights[static_cast<size_t>(iz) * GRID_N + ix];
                if (y > h)
                    h = y;                  // 最大値を採る = 真上から見て一番手前の面が残る
            }
        }
    }

    // 穴埋め (覆われなかった格子点を隣接点の平均で埋める)
    std::vector<float> next(m_heights.size());   // 書き込み先 (読みながら書くと結果が偏るため別配列)
    for (int iter = 0; iter < GRID_N; ++iter)    // 1 回で 1 マスぶん外側へ広がるので最大 GRID_N 回
    {
        bool anyUnset = false;              // まだ埋まらないマスが残っているか
        next = m_heights;
        for (int iz = 0; iz < GRID_N; ++iz)
        {
            for (int ix = 0; ix < GRID_N; ++ix)
            {
                const size_t idx = static_cast<size_t>(iz) * GRID_N + ix;
                if (m_heights[idx] != UNSET)
                    continue;
                float sum = 0.0f;           // 埋まっている隣のマスの高さの合計
                int count = 0;              // その個数
                const int nx[4] = { ix - 1, ix + 1, ix, ix };   // 左・右・手前・奥の 4 近傍
                const int nz[4] = { iz, iz, iz - 1, iz + 1 };
                for (int k = 0; k < 4; ++k)
                {
                    if (nx[k] < 0 || nx[k] >= GRID_N || nz[k] < 0 || nz[k] >= GRID_N)
                        continue;
                    const float hn = m_heights[static_cast<size_t>(nz[k]) * GRID_N + nx[k]];
                    if (hn != UNSET) { sum += hn; ++count; }
                }
                if (count > 0)
                    next[idx] = sum / count;
                else
                    anyUnset = true;
            }
        }
        m_heights.swap(next);
        if (!anyUnset)
            break;
    }
    // メッシュが空などで最後まで埋まらなかった格子点は 0 にする
    for (float& h : m_heights)
        if (h == UNSET) h = 0.0f;
}

//-----------------------------------------------------------------------------
// GetValleyCenterX
//-----------------------------------------------------------------------------
float Terrain::GetValleyCenterX(float z) const
{
    if (!m_meshFromFile)
        return ValleyCenterX(z);

    // OBJ 地形: この z の行を端から端まで調べ、最も標高が低い x を谷底とみなす
    float bestX = 0.0f;                     // これまでで最も低かった位置
    float bestH = 1e30f;                    // そのときの高さ (十分大きな値から始める)
    for (float x = -HALF_SIZE + 1.0f; x <= HALF_SIZE - 1.0f; x += m_cellSize)   // 端 1 m は除外
    {
        const float h = GetHeight(x, z);
        if (h < bestH) { bestH = h; bestX = x; }
    }
    return bestX;
}

//-----------------------------------------------------------------------------
// CreateRockTexture
//   1. Graphics::AssetPath(ROCK_TEXTURE_FILES[i]) を順に TextureLoader::LoadFromFile で試す
//   2. すべて失敗したら CreateProceduralRockTexture にフォールバック
//-----------------------------------------------------------------------------
bool Terrain::CreateRockTexture(Graphics& gfx)
{
    for (const wchar_t* file : ROCK_TEXTURE_FILES)
    {
        const std::wstring path = Graphics::AssetPath(file);
        if (TextureLoader::LoadFromFile(gfx, path, m_rockTexSRV))
        {
            m_textureFromFile = true;
            return true;
        }
    }

    // 画像が見つからない → ノイズ生成にフォールバック
    m_textureFromFile = false;
    return CreateProceduralRockTexture(gfx);
}

//-----------------------------------------------------------------------------
// CreateProceduralRockTexture
//   アルゴリズム (ピクセルごと):
//     f1     = タイリング fBm (大きなまだら)      → 明るい肌色と茶色を混ぜる
//     f2     = タイリング fBm (別位相・細かい)    → 赤みのある斑点を加える
//     crack  = 尾根状ノイズを閾値処理            → 細い暗い亀裂線
//     fine   = 高周波ノイズ                       → ざらつき
//     color  = lerp(dark, light, f1) → 赤斑点を混合 → 亀裂で暗く → ざらつき
//   その後、ミップマップ付きテクスチャを作り GenerateMips で縮小版を自動生成する。
//-----------------------------------------------------------------------------
bool Terrain::CreateProceduralRockTexture(Graphics& gfx)
{
    const int texSize = 1024;
    std::vector<uint32_t> pixels(static_cast<size_t>(texSize) * texSize);

    const XMFLOAT3 cLight(0.90f, 0.78f, 0.68f);   // 明るいピンクがかった肌色
    const XMFLOAT3 cDark (0.66f, 0.52f, 0.44f);   // やや暗い茶色
    const XMFLOAT3 cRed  (0.74f, 0.42f, 0.34f);   // 赤みのある斑点

    for (int y = 0; y < texSize; ++y)
    {
        const float v = static_cast<float>(y) / texSize;
        for (int x = 0; x < texSize; ++x)
        {
            const float u = static_cast<float>(x) / texSize;

            const float f0 = Noise::FbmTileable(u + 0.9f, v + 0.4f, 2, 3);       // -1～1 (大きな明暗)
            // 4 種類のノイズを別々の位相 (+0.37 など) で評価し、役割を分けて合成する。
            // 第 3 引数が模様の大きさ (数が大きいほど細かい)、第 4 引数が重ねる層の数。
            const float f1 = Noise::FbmTileable(u, v, 4, 6);                      // -1～1 大きなまだら
            const float f2 = Noise::FbmTileable(u + 0.37f, v + 0.11f, 9, 4);      // -1～1 赤みの斑点用
            const float rg = Noise::RidgedTileable(u + 0.5f, v + 0.25f, 10, 3);   // 0～1 (細かい亀裂)
            const float fine = Noise::FbmTileable(u + 0.2f, v + 0.7f, 64, 2);     // -1～1 ざらつき

            // 明暗のまだら
            XMFLOAT3 c = Lerp3(cDark, cLight, 0.5f + 0.5f * f1);
            // 赤みの斑点 (f2 が高い場所だけ)
            c = Lerp3(c, cRed, Saturate(f2 * 1.6f - 0.35f));
            // 亀裂: 尾根ノイズが高い場所を細く暗くする (閾値を高くすると線が細くなる)
            const float crack = SmoothStep(0.76f, 0.93f, rg);
            const float darken = (1.0f - 0.33f * crack) * (0.88f + 0.12f * f0);
            // ざらつき
            const float grain = 0.93f + 0.07f * fine;

            const float r = Saturate(c.x * darken * grain);
            const float g = Saturate(c.y * darken * grain);
            const float b = Saturate(c.z * darken * grain);

            // RGBA8 (リトルエンディアンで R が最下位バイト)
            // 0.0～1.0 を 0～255 に直す (+0.5 は四捨五入のため)
            const uint32_t R = static_cast<uint32_t>(r * 255.0f + 0.5f);
            const uint32_t G = static_cast<uint32_t>(g * 255.0f + 0.5f);
            const uint32_t B = static_cast<uint32_t>(b * 255.0f + 0.5f);
            // 1 ピクセルを 32 ビットに詰める。R が下位、A (不透明度 255) が上位バイト
            pixels[static_cast<size_t>(y) * texSize + x] = R | (G << 8) | (B << 16) | (0xFFu << 24);
        }
    }

    // GPU テクスチャ化 (ミップマップ生成込み) はファイル読み込みと共通の処理を使う
    return TextureLoader::CreateFromPixels(gfx, pixels.data(), texSize, texSize, m_rockTexSRV);
}

//-----------------------------------------------------------------------------
// CreateShaders
//-----------------------------------------------------------------------------
bool Terrain::CreateShaders(Graphics& gfx)
{
    ComPtr<ID3DBlob> vsBlob, psBlob;
    const std::wstring path = Graphics::ShaderPath(L"Terrain.hlsl");

    if (!Graphics::CompileShaderFromFile(path, "VSMain", "vs_5_0", vsBlob)) return false;
    if (!Graphics::CompileShaderFromFile(path, "PSMain", "ps_5_0", psBlob)) return false;

    ID3D11Device* dev = gfx.GetDevice();
    if (FAILED(dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.GetAddressOf())))
        return false;
    if (FAILED(dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.GetAddressOf())))
        return false;

    // 頂点レイアウト: Vertex 構造体のメンバ順と一致させる
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, uv),     D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(dev->CreateInputLayout(layout, _countof(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf())))
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------
void Terrain::Render(Graphics& gfx)
{
    ID3D11DeviceContext* ctx = gfx.GetContext();   // GPU へ命令を出す窓口

    const UINT stride = sizeof(Vertex);     // 頂点 1 個のバイト数 (次の頂点まで何バイト進むか)
    const UINT offset = 0;                  // バッファの何バイト目から使うか
    ID3D11Buffer* vb = m_vertexBuffer.Get();

    // ---- 入力アセンブラ: 何を、どう組み立てて描くか ----
    ctx->IASetInputLayout(m_inputLayout.Get());        // 頂点データの並び (位置/法線/UV) の指定
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);                 // 頂点バッファを接続
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);  // インデックスは 32 bit
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);   // 3 個で 1 三角形

    // ---- シェーダー: 頂点変換と色の計算を行う GPU プログラム ----
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);          // 頂点シェーダー (Terrain.hlsl の VSMain)
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);          // ピクセルシェーダー (同 PSMain)

    // ---- ピクセルシェーダーが使うテクスチャとサンプラー ----
    ID3D11ShaderResourceView* srv = m_rockTexSRV.Get();     // 岩肌テクスチャ
    ID3D11SamplerState* samp = gfx.GetSamplerLinearWrap();  // 異方性フィルタ + 繰り返し
    ctx->PSSetShaderResources(0, 1, &srv);             // HLSL の register(t0) に対応
    ctx->PSSetSamplers(0, 1, &samp);                   // HLSL の register(s0) に対応

    // ---- 固定機能の設定 ----
    ctx->RSSetState(gfx.GetRasterizerSolid());                        // 塗りつぶし + 裏面カリング
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDefault(), 0);       // 深度テストあり (手前を優先)
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);  // 不透明 (混ぜずに上書き)

    ctx->DrawIndexed(m_indexCount, 0, 0);   // 描画実行。ここで初めて GPU が動く
}
