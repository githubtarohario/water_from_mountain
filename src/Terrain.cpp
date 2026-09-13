//=============================================================================
// Terrain.cpp
//   山の地形の生成と描画の実装。
//=============================================================================
#include "Terrain.h"
#include "Noise.h"
#include <algorithm>
#include <cmath>

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
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
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
    const float cx    = ValleyCenterX(z);
    const float d     = std::fabs(x - cx);
    const float wallT = SmoothStep(1.6f, 8.5f, d);

    const float base     = -0.20f * z;
    const float dFloor   = std::min(d, 1.6f);
    const float floorU   = dFloor * dFloor * 0.3f;
    const float mountain = 11.0f * wallT;

    const float fbm   = Noise::Fbm2D(x * 0.05f + 7.3f, z * 0.05f + 2.1f, 6);
    const float rough = fbm * (0.25f + 5.0f * wallT);

    const float ridge = Noise::Ridged2D(x * 0.12f + 3.0f, z * 0.12f + 5.0f, 4) * 1.5f * wallT;

    return base + floorU + mountain + rough + ridge;
}

//-----------------------------------------------------------------------------
// Initialize
//-----------------------------------------------------------------------------
bool Terrain::Initialize(Graphics& gfx)
{
    BuildHeightMap();
    if (!BuildMesh(gfx))          return false;
    if (!CreateRockTexture(gfx))  return false;
    if (!CreateShaders(gfx))      return false;
    return true;
}

//-----------------------------------------------------------------------------
// BuildHeightMap
//   格子点 (ix, iz) のワールド座標は
//     x = -HALF_SIZE + ix * cellSize,  z = -HALF_SIZE + iz * cellSize
//-----------------------------------------------------------------------------
void Terrain::BuildHeightMap()
{
    m_heights.resize(static_cast<size_t>(GRID_N) * GRID_N);
    for (int iz = 0; iz < GRID_N; ++iz)
    {
        const float z = -HALF_SIZE + iz * m_cellSize;
        for (int ix = 0; ix < GRID_N; ++ix)
        {
            const float x = -HALF_SIZE + ix * m_cellSize;
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
    // 格子座標へ変換し、範囲内にクランプ
    float fx = (x + HALF_SIZE) / m_cellSize;
    float fz = (z + HALF_SIZE) / m_cellSize;
    fx = std::clamp(fx, 0.0f, static_cast<float>(GRID_N - 1) - 0.001f);
    fz = std::clamp(fz, 0.0f, static_cast<float>(GRID_N - 1) - 0.001f);

    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float tx = fx - ix;
    const float tz = fz - iz;

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
    const float e  = m_cellSize;
    const float hx = (GetHeight(x + e, z) - GetHeight(x - e, z)) / (2.0f * e);
    const float hz = (GetHeight(x, z + e) - GetHeight(x, z - e)) / (2.0f * e);

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
    std::vector<Vertex> vertices(static_cast<size_t>(GRID_N) * GRID_N);
    const float uvScale = 0.12f;   // ワールド 1 単位あたりの UV 進み (約 8.3 単位でテクスチャ 1 周)

    for (int iz = 0; iz < GRID_N; ++iz)
    {
        const float z = -HALF_SIZE + iz * m_cellSize;
        for (int ix = 0; ix < GRID_N; ++ix)
        {
            const float x = -HALF_SIZE + ix * m_cellSize;
            Vertex& v = vertices[static_cast<size_t>(iz) * GRID_N + ix];
            v.pos    = XMFLOAT3(x, m_heights[static_cast<size_t>(iz) * GRID_N + ix], z);
            v.normal = GetNormal(x, z);
            v.uv     = XMFLOAT2(x * uvScale, z * uvScale);
        }
    }

    // ---- インデックス ----
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(GRID_N - 1) * (GRID_N - 1) * 6);
    for (int iz = 0; iz < GRID_N - 1; ++iz)
    {
        for (int ix = 0; ix < GRID_N - 1; ++ix)
        {
            const uint32_t i00 = static_cast<uint32_t>(iz * GRID_N + ix);          // (x0, z0)
            const uint32_t i10 = i00 + 1;                                           // (x1, z0)
            const uint32_t i01 = i00 + GRID_N;                                      // (x0, z1)
            const uint32_t i11 = i01 + 1;                                           // (x1, z1)

            // 三角形 1: (x0,z0) → (x1,z1) → (x1,z0)
            indices.push_back(i00); indices.push_back(i11); indices.push_back(i10);
            // 三角形 2: (x0,z0) → (x0,z1) → (x1,z1)
            indices.push_back(i00); indices.push_back(i01); indices.push_back(i11);
        }
    }
    m_indexCount = static_cast<UINT>(indices.size());

    // ---- GPU バッファ作成 (IMMUTABLE: 作成後に変更しない) ----
    D3D11_BUFFER_DESC vbd = {};
    vbd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    vbd.Usage     = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = { vertices.data(), 0, 0 };
    if (FAILED(gfx.GetDevice()->CreateBuffer(&vbd, &vinit, m_vertexBuffer.GetAddressOf())))
        return false;

    D3D11_BUFFER_DESC ibd = {};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibd.Usage     = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = { indices.data(), 0, 0 };
    if (FAILED(gfx.GetDevice()->CreateBuffer(&ibd, &iinit, m_indexBuffer.GetAddressOf())))
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// CreateRockTexture
//   アルゴリズム (ピクセルごと):
//     f1     = タイリング fBm (大きなまだら)      → 明るい肌色と茶色を混ぜる
//     f2     = タイリング fBm (別位相・細かい)    → 赤みのある斑点を加える
//     crack  = 尾根状ノイズを閾値処理            → 細い暗い亀裂線
//     fine   = 高周波ノイズ                       → ざらつき
//     color  = lerp(dark, light, f1) → 赤斑点を混合 → 亀裂で暗く → ざらつき
//   その後、ミップマップ付きテクスチャを作り GenerateMips で縮小版を自動生成する。
//-----------------------------------------------------------------------------
bool Terrain::CreateRockTexture(Graphics& gfx)
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
            const float f1 = Noise::FbmTileable(u, v, 4, 6);                      // -1～1
            const float f2 = Noise::FbmTileable(u + 0.37f, v + 0.11f, 9, 4);      // -1～1
            const float rg = Noise::RidgedTileable(u + 0.5f, v + 0.25f, 10, 3);   // 0～1 (細かい亀裂)
            const float fine = Noise::FbmTileable(u + 0.2f, v + 0.7f, 64, 2);     // -1～1

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
            const uint32_t R = static_cast<uint32_t>(r * 255.0f + 0.5f);
            const uint32_t G = static_cast<uint32_t>(g * 255.0f + 0.5f);
            const uint32_t B = static_cast<uint32_t>(b * 255.0f + 0.5f);
            pixels[static_cast<size_t>(y) * texSize + x] = R | (G << 8) | (B << 16) | (0xFFu << 24);
        }
    }

    // ミップマップ自動生成のため RENDER_TARGET も付けて DEFAULT で作る
    D3D11_TEXTURE2D_DESC td = {};
    td.Width     = texSize;
    td.Height    = texSize;
    td.MipLevels = 0;                                   // 0 = 全ミップレベルを作る
    td.ArraySize = 1;
    td.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage     = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(gfx.GetDevice()->CreateTexture2D(&td, nullptr, tex.GetAddressOf())))
        return false;

    // 最上位ミップ (レベル 0) にピクセルを書き込む
    gfx.GetContext()->UpdateSubresource(tex.Get(), 0, nullptr, pixels.data(), texSize * sizeof(uint32_t), 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = static_cast<UINT>(-1);   // 全ミップレベル
    if (FAILED(gfx.GetDevice()->CreateShaderResourceView(tex.Get(), &srvd, m_rockTexSRV.GetAddressOf())))
        return false;

    gfx.GetContext()->GenerateMips(m_rockTexSRV.Get());
    return true;
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
    ID3D11DeviceContext* ctx = gfx.GetContext();

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    ID3D11Buffer* vb = m_vertexBuffer.Get();

    ctx->IASetInputLayout(m_inputLayout.Get());
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srv = m_rockTexSRV.Get();
    ID3D11SamplerState* samp = gfx.GetSamplerLinearWrap();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &samp);

    ctx->RSSetState(gfx.GetRasterizerSolid());
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDefault(), 0);
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);

    ctx->DrawIndexed(m_indexCount, 0, 0);
}
