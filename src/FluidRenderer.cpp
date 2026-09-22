//=============================================================================
// FluidRenderer.cpp
//   粒子描画・スクリーンスペース流体レンダリングの実装。
//=============================================================================
#include "FluidRenderer.h"
#include <algorithm>

using namespace DirectX;

//-----------------------------------------------------------------------------
// Initialize
//-----------------------------------------------------------------------------
bool FluidRenderer::Initialize(Graphics& gfx, int maxParticles)
{
    m_maxParticles = maxParticles;          // バッファに入る最大粒子数 (以後変えられない)

    // ---- 粒子用 StructuredBuffer (毎フレーム CPU から書き換える) ----
    // StructuredBuffer は「構造体の配列」としてシェーダーから添え字で読めるバッファ。
    // 頂点バッファと違い、頂点ごとに 1 個ずつではなく、任意の要素を自由に参照できる。
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth           = static_cast<UINT>(sizeof(XMFLOAT4) * maxParticles);  // 全体のバイト数
    bd.Usage               = D3D11_USAGE_DYNAMIC;        // CPU が毎フレーム書き換える
    bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE; // シェーダーから読む用途
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;     // CPU からの書き込みを許可
    bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;  // 構造化バッファとして扱う
    bd.StructureByteStride = sizeof(XMFLOAT4);           // 要素 1 個のバイト数 (float4 = 16 B)
    if (FAILED(gfx.GetDevice()->CreateBuffer(&bd, nullptr, m_particleBuffer.GetAddressOf())))
        return false;

    // ビュー (SRV) は「このバッファをシェーダーからどう見せるか」の窓口
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format              = DXGI_FORMAT_UNKNOWN;          // 構造化バッファは UNKNOWN
    srvd.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;   // 1 次元のバッファとして見る
    srvd.Buffer.FirstElement = 0;                            // 先頭要素から
    srvd.Buffer.NumElements  = maxParticles;                 // 何個ぶん見せるか
    if (FAILED(gfx.GetDevice()->CreateShaderResourceView(m_particleBuffer.Get(), &srvd, m_particleSRV.GetAddressOf())))
        return false;

    if (!CreateShaders(gfx))      return false;
    if (!CreateDepthTargets(gfx)) return false;
    return true;
}

//-----------------------------------------------------------------------------
// CreateShaders
//-----------------------------------------------------------------------------
bool FluidRenderer::CreateShaders(Graphics& gfx)
{
    ID3D11Device* dev = gfx.GetDevice();
    ComPtr<ID3DBlob> blob;

    const std::wstring particlePath = Graphics::ShaderPath(L"Particle.hlsl");
    const std::wstring surfacePath  = Graphics::ShaderPath(L"FluidSurface.hlsl");

    // Particle.hlsl
    if (!Graphics::CompileShaderFromFile(particlePath, "VSMain", "vs_5_0", blob)) return false;
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_vsParticle.GetAddressOf()))) return false;

    if (!Graphics::CompileShaderFromFile(particlePath, "PSColor", "ps_5_0", blob)) return false;
    if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_psColor.GetAddressOf()))) return false;

    if (!Graphics::CompileShaderFromFile(particlePath, "PSDepth", "ps_5_0", blob)) return false;
    if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_psDepth.GetAddressOf()))) return false;

    // FluidSurface.hlsl (VSFullscreen は Common.hlsli で定義)
    if (!Graphics::CompileShaderFromFile(surfacePath, "VSFullscreen", "vs_5_0", blob)) return false;
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_vsFullscreen.GetAddressOf()))) return false;

    if (!Graphics::CompileShaderFromFile(surfacePath, "PSBlur", "ps_5_0", blob)) return false;
    if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_psBlur.GetAddressOf()))) return false;

    if (!Graphics::CompileShaderFromFile(surfacePath, "PSComposite", "ps_5_0", blob)) return false;
    if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_psComposite.GetAddressOf()))) return false;

    return true;
}

//-----------------------------------------------------------------------------
// CreateDepthTargets
//-----------------------------------------------------------------------------
bool FluidRenderer::CreateDepthTargets(Graphics& gfx)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width     = gfx.GetWidth();               // 画面と同じ解像度にする
    td.Height    = gfx.GetHeight();
    td.MipLevels = 1;                            // 縮小版は不要 (等倍でしか読まない)
    td.ArraySize = 1;
    td.Format    = DXGI_FORMAT_R32_FLOAT;        // ビュー空間深度を float で保持
    td.SampleDesc.Count = 1;                     // マルチサンプルなし
    td.Usage     = D3D11_USAGE_DEFAULT;          // GPU が読み書きする
    // 「描き込む先」と「読み取る元」の両方で使うので、2 つの用途を指定する
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    for (int i = 0; i < 2; ++i)
    {
        if (FAILED(gfx.GetDevice()->CreateTexture2D(&td, nullptr, m_depthTex[i].ReleaseAndGetAddressOf())))
            return false;
        if (FAILED(gfx.GetDevice()->CreateRenderTargetView(m_depthTex[i].Get(), nullptr, m_depthRTV[i].ReleaseAndGetAddressOf())))
            return false;
        if (FAILED(gfx.GetDevice()->CreateShaderResourceView(m_depthTex[i].Get(), nullptr, m_depthSRV[i].ReleaseAndGetAddressOf())))
            return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// OnResize
//-----------------------------------------------------------------------------
bool FluidRenderer::OnResize(Graphics& gfx)
{
    return CreateDepthTargets(gfx);
}

//-----------------------------------------------------------------------------
// UploadParticles
//-----------------------------------------------------------------------------
UINT FluidRenderer::UploadParticles(Graphics& gfx, const std::vector<XMFLOAT4>& particles)
{
    // バッファに入る数を超えないよう、少ない方を採用する
    const UINT count = static_cast<UINT>(std::min<size_t>(particles.size(), static_cast<size_t>(m_maxParticles)));
    if (count > 0)
        // Map / memcpy / Unmap で CPU のメモリから GPU のバッファへ一括コピーする
        gfx.UpdateBuffer(m_particleBuffer.Get(), particles.data(), sizeof(XMFLOAT4) * count);
    return count;                           // 実際に転送した粒子数 (描画する個数になる)
}

//-----------------------------------------------------------------------------
// DrawSpheres
//   頂点バッファなし: IASetVertexBuffers を呼ばず、入力レイアウトも nullptr。
//   DrawInstanced(4, count): 4 頂点のトライアングルストリップを粒子数だけ描く。
//-----------------------------------------------------------------------------
void FluidRenderer::DrawSpheres(Graphics& gfx, UINT count, ID3D11PixelShader* ps)
{
    ID3D11DeviceContext* ctx = gfx.GetContext();

    ctx->IASetInputLayout(nullptr);         // 頂点データを使わないのでレイアウトは不要
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);  // 4 頂点 → 三角形 2 枚
    ctx->VSSetShader(m_vsParticle.Get(), nullptr, 0);   // 頂点番号から四角形を組み立てる VS
    ctx->PSSetShader(ps, nullptr, 0);                   // 色を描く PSColor / 深度を描く PSDepth

    // 粒子の位置は頂点バッファではなく、頂点シェーダーが読む構造化バッファで渡す
    ID3D11ShaderResourceView* srv = m_particleSRV.Get();
    ctx->VSSetShaderResources(0, 1, &srv);  // HLSL の register(t0) に対応

    ctx->RSSetState(gfx.GetRasterizerNoCull());   // ビルボードは表裏を問わないのでカリングなし
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDefault(), 0);        // 球同士の前後関係を正しく
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);   // 不透明

    // 4 頂点の図形を count 個ぶん、1 回の命令で描く (インスタンシング)
    ctx->DrawInstanced(4, count, 0, 0);

    // SRV を外しておく (同じリソースを後で RTV にバインドするときの競合防止)
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSrv);
}

//-----------------------------------------------------------------------------
// DrawFullscreen
//-----------------------------------------------------------------------------
void FluidRenderer::DrawFullscreen(Graphics& gfx, ID3D11PixelShader* ps, ID3D11ShaderResourceView* src)
{
    ID3D11DeviceContext* ctx = gfx.GetContext();

    ctx->IASetInputLayout(nullptr);         // 頂点バッファなし (VS が頂点番号から座標を作る)
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_vsFullscreen.Get(), nullptr, 0);   // 画面全体を覆う三角形を 1 枚生成する VS
    ctx->PSSetShader(ps, nullptr, 0);                     // ぼかし (PSBlur) or 合成 (PSComposite)

    ID3D11SamplerState* samp = gfx.GetSamplerPointClamp();   // 深度は補間すると壊れるので点サンプル
    ctx->PSSetShaderResources(0, 1, &src);  // 入力テクスチャ (前のパスの結果) を t0 へ
    ctx->PSSetSamplers(0, 1, &samp);        // サンプラーを s0 へ

    ctx->RSSetState(gfx.GetRasterizerNoCull());
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDisabled(), 0);   // 画面全体を塗るので深度は不要

    ctx->Draw(3, 0);                        // 頂点 3 個 = 三角形 1 枚だけ描く

    // 次のパスで同じテクスチャに描き込めるよう、読み取り用の接続を外しておく
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSrv);
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------
void FluidRenderer::Render(Graphics& gfx, const std::vector<XMFLOAT4>& particles,
                           FluidRenderMode mode, FrameConstants& frame, ID3D11Buffer* cbFrame)
{
    const UINT count = UploadParticles(gfx, particles);
    if (count == 0)
        return;

    if (mode == FluidRenderMode::Particles)
        RenderParticlesMode(gfx, count, frame, cbFrame);
    else
        RenderSurfaceMode(gfx, count, frame, cbFrame);
}

//-----------------------------------------------------------------------------
// RenderParticlesMode
//   バックバッファに直接、着色した球を描く。
//-----------------------------------------------------------------------------
void FluidRenderer::RenderParticlesMode(Graphics& gfx, UINT count, FrameConstants& frame, ID3D11Buffer* cbFrame)
{
    frame.Params.x = m_particleRadius;      // シェーダーが使う球の半径をこのモード用の値にする
    gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));   // 変更した定数バッファを GPU へ送り直す

    // 描き込み先を画面 (バックバッファ) + 地形を描いた深度バッファに設定する。
    // 深度バッファを共有しているので、地形の裏に回った粒子は自動的に隠れる。
    ID3D11RenderTargetView* rtv = gfx.GetBackBufferRTV();
    gfx.GetContext()->OMSetRenderTargets(1, &rtv, gfx.GetDepthStencilView());

    DrawSpheres(gfx, count, m_psColor.Get());   // 速度で着色した球を描く
}

//-----------------------------------------------------------------------------
// RenderSurfaceMode
//   1. 深度テクスチャ [0] をクリア (0 = 流体なし) し、メインの深度バッファと組で
//      球を描く → 地形に隠れる部分は自動的に除かれる
//   2. [0]→[1] 横ぼかし, [1]→[0] 縦ぼかし を m_blurIterations 回
//   3. バックバッファに [0] を入力として水面を合成 (アルファブレンド)
//-----------------------------------------------------------------------------
void FluidRenderer::RenderSurfaceMode(Graphics& gfx, UINT count, FrameConstants& frame, ID3D11Buffer* cbFrame)
{
    ID3D11DeviceContext* ctx = gfx.GetContext();

    // ---- 1. 深度パス ----
    frame.Params.x = m_surfaceRadius;       // 表面用は少し大きい半径 (隣の球と重ねて隙間を消す)
    frame.Params.z = 0.0f;                  // ぼかし方向は深度パスでは使わないので 0 にしておく
    frame.Params.w = 0.0f;
    gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));

    // 0 で塗りつぶす = 「ここには流体がない」という印。シェーダー側はこの 0 を見て判定する
    const float clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->ClearRenderTargetView(m_depthRTV[0].Get(), clearZero);

    // 書き込み先を深度テクスチャに変えつつ、深度バッファは地形のものを使い続ける
    ID3D11RenderTargetView* rtv0 = m_depthRTV[0].Get();
    ctx->OMSetRenderTargets(1, &rtv0, gfx.GetDepthStencilView());
    DrawSpheres(gfx, count, m_psDepth.Get());   // 色ではなく「カメラからの距離」を書き込む

    // ---- 2. ぼかしパス (ピンポン) ----
    // 同じテクスチャを読みながら書くことはできないので、2 枚を交互に使う (ピンポン)。
    // 横 1 回 + 縦 1 回で 2 次元のぼかし 1 回分に相当し、それを繰り返して滑らかにする。
    for (int it = 0; it < m_blurIterations; ++it)
    {
        // 横方向: [0] → [1]
        frame.Params.z = 1.0f; frame.Params.w = 0.0f;   // シェーダーへ「横方向」と伝える
        gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));
        ID3D11RenderTargetView* rtv1 = m_depthRTV[1].Get();
        ctx->OMSetRenderTargets(1, &rtv1, nullptr);     // 深度バッファは不要 (nullptr)
        DrawFullscreen(gfx, m_psBlur.Get(), m_depthSRV[0].Get());

        // 縦方向: [1] → [0]
        frame.Params.z = 0.0f; frame.Params.w = 1.0f;   // 「縦方向」に切り替える
        gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));
        ctx->OMSetRenderTargets(1, &rtv0, nullptr);
        DrawFullscreen(gfx, m_psBlur.Get(), m_depthSRV[1].Get());
    }

    // ---- 3. 合成パス ----
    ID3D11RenderTargetView* back = gfx.GetBackBufferRTV();
    ctx->OMSetRenderTargets(1, &back, nullptr);        // 画面へ描く (深度テストは使わない)
    ctx->OMSetBlendState(gfx.GetBlendAlpha(), nullptr, 0xFFFFFFFF);   // 地形の上に半透明で重ねる
    DrawFullscreen(gfx, m_psComposite.Get(), m_depthSRV[0].Get());    // 深度→法線→陰影を計算
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);  // ブレンド設定を元に戻す

    // 後続の描画のために深度バッファ付きのバックバッファに戻す
    ctx->OMSetRenderTargets(1, &back, gfx.GetDepthStencilView());
}
