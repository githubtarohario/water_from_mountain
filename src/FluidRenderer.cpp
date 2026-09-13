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
    m_maxParticles = maxParticles;

    // ---- 粒子用 StructuredBuffer (毎フレーム CPU から書き換える) ----
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth           = static_cast<UINT>(sizeof(XMFLOAT4) * maxParticles);
    bd.Usage               = D3D11_USAGE_DYNAMIC;
    bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(XMFLOAT4);
    if (FAILED(gfx.GetDevice()->CreateBuffer(&bd, nullptr, m_particleBuffer.GetAddressOf())))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format              = DXGI_FORMAT_UNKNOWN;          // 構造化バッファは UNKNOWN
    srvd.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    srvd.Buffer.FirstElement = 0;
    srvd.Buffer.NumElements  = maxParticles;
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
    td.Width     = gfx.GetWidth();
    td.Height    = gfx.GetHeight();
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format    = DXGI_FORMAT_R32_FLOAT;        // ビュー空間深度を float で保持
    td.SampleDesc.Count = 1;
    td.Usage     = D3D11_USAGE_DEFAULT;
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
    const UINT count = static_cast<UINT>(std::min<size_t>(particles.size(), static_cast<size_t>(m_maxParticles)));
    if (count > 0)
        gfx.UpdateBuffer(m_particleBuffer.Get(), particles.data(), sizeof(XMFLOAT4) * count);
    return count;
}

//-----------------------------------------------------------------------------
// DrawSpheres
//   頂点バッファなし: IASetVertexBuffers を呼ばず、入力レイアウトも nullptr。
//   DrawInstanced(4, count): 4 頂点のトライアングルストリップを粒子数だけ描く。
//-----------------------------------------------------------------------------
void FluidRenderer::DrawSpheres(Graphics& gfx, UINT count, ID3D11PixelShader* ps)
{
    ID3D11DeviceContext* ctx = gfx.GetContext();

    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(m_vsParticle.Get(), nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);

    ID3D11ShaderResourceView* srv = m_particleSRV.Get();
    ctx->VSSetShaderResources(0, 1, &srv);

    ctx->RSSetState(gfx.GetRasterizerNoCull());
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDefault(), 0);
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);

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

    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_vsFullscreen.Get(), nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);

    ID3D11SamplerState* samp = gfx.GetSamplerPointClamp();
    ctx->PSSetShaderResources(0, 1, &src);
    ctx->PSSetSamplers(0, 1, &samp);

    ctx->RSSetState(gfx.GetRasterizerNoCull());
    ctx->OMSetDepthStencilState(gfx.GetDepthStateDisabled(), 0);

    ctx->Draw(3, 0);

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
    frame.Params.x = m_particleRadius;
    gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));

    ID3D11RenderTargetView* rtv = gfx.GetBackBufferRTV();
    gfx.GetContext()->OMSetRenderTargets(1, &rtv, gfx.GetDepthStencilView());

    DrawSpheres(gfx, count, m_psColor.Get());
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
    frame.Params.x = m_surfaceRadius;
    frame.Params.z = 0.0f;
    frame.Params.w = 0.0f;
    gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));

    const float clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->ClearRenderTargetView(m_depthRTV[0].Get(), clearZero);

    ID3D11RenderTargetView* rtv0 = m_depthRTV[0].Get();
    ctx->OMSetRenderTargets(1, &rtv0, gfx.GetDepthStencilView());
    DrawSpheres(gfx, count, m_psDepth.Get());

    // ---- 2. ぼかしパス (ピンポン) ----
    for (int it = 0; it < m_blurIterations; ++it)
    {
        // 横方向: [0] → [1]
        frame.Params.z = 1.0f; frame.Params.w = 0.0f;
        gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));
        ID3D11RenderTargetView* rtv1 = m_depthRTV[1].Get();
        ctx->OMSetRenderTargets(1, &rtv1, nullptr);
        DrawFullscreen(gfx, m_psBlur.Get(), m_depthSRV[0].Get());

        // 縦方向: [1] → [0]
        frame.Params.z = 0.0f; frame.Params.w = 1.0f;
        gfx.UpdateBuffer(cbFrame, &frame, sizeof(frame));
        ctx->OMSetRenderTargets(1, &rtv0, nullptr);
        DrawFullscreen(gfx, m_psBlur.Get(), m_depthSRV[1].Get());
    }

    // ---- 3. 合成パス ----
    ID3D11RenderTargetView* back = gfx.GetBackBufferRTV();
    ctx->OMSetRenderTargets(1, &back, nullptr);
    ctx->OMSetBlendState(gfx.GetBlendAlpha(), nullptr, 0xFFFFFFFF);
    DrawFullscreen(gfx, m_psComposite.Get(), m_depthSRV[0].Get());
    ctx->OMSetBlendState(gfx.GetBlendOpaque(), nullptr, 0xFFFFFFFF);

    // 後続の描画のために深度バッファ付きのバックバッファに戻す
    ctx->OMSetRenderTargets(1, &back, gfx.GetDepthStencilView());
}
