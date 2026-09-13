//=============================================================================
// Graphics.cpp
//   Graphics クラスの実装。DirectX 11 の初期化処理と共通ヘルパー。
//=============================================================================
#include "Graphics.h"
#include <d3dcompiler.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

std::wstring Graphics::s_shaderDir = L"shaders";
std::wstring Graphics::s_assetDir  = L"assets";

//-----------------------------------------------------------------------------
// SetShaderDirectory
//-----------------------------------------------------------------------------
void Graphics::SetShaderDirectory(const std::wstring& dir)
{
    s_shaderDir = dir;
    // 末尾の区切り文字を取り除いて統一する
    while (!s_shaderDir.empty() && (s_shaderDir.back() == L'\\' || s_shaderDir.back() == L'/'))
        s_shaderDir.pop_back();
}

//-----------------------------------------------------------------------------
// ShaderPath
//-----------------------------------------------------------------------------
std::wstring Graphics::ShaderPath(const wchar_t* file)
{
    return s_shaderDir + L"\\" + file;
}

//-----------------------------------------------------------------------------
// SetAssetDirectory
//-----------------------------------------------------------------------------
void Graphics::SetAssetDirectory(const std::wstring& dir)
{
    s_assetDir = dir;
    while (!s_assetDir.empty() && (s_assetDir.back() == L'\\' || s_assetDir.back() == L'/'))
        s_assetDir.pop_back();
}

//-----------------------------------------------------------------------------
// AssetPath
//-----------------------------------------------------------------------------
std::wstring Graphics::AssetPath(const wchar_t* file)
{
    return s_assetDir + L"\\" + file;
}

//-----------------------------------------------------------------------------
// Initialize
//   アルゴリズム (初期化手順):
//     1. スワップチェーンの設定 (DXGI_SWAP_CHAIN_DESC) を用意
//     2. D3D11CreateDeviceAndSwapChain でデバイスとスワップチェーンを同時作成
//     3. バックバッファ RTV と深度バッファ DSV を作成
//     4. 共通ステートを作成
//-----------------------------------------------------------------------------
bool Graphics::Initialize(HWND hWnd, int width, int height)
{
    m_width  = width;
    m_height = height;

    // ---- 1. スワップチェーンの設定 ----
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;                                   // ダブルバッファ
    sd.BufferDesc.Width  = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;          // 8bit RGBA
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hWnd;
    sd.SampleDesc.Count  = 1;                                   // マルチサンプルなし
    sd.SampleDesc.Quality= 0;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    // ---- 2. デバイスとスワップチェーンの作成 ----
    UINT createFlags = 0;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;   // デバッグビルドでは D3D のデバッグレイヤーを有効化
#endif
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                      // 既定のアダプタ
        D3D_DRIVER_TYPE_HARDWARE,     // ハードウェア GPU を使う
        nullptr,
        createFlags,
        featureLevels, 1,
        D3D11_SDK_VERSION,
        &sd,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        &obtained,
        m_context.GetAddressOf());

    if (FAILED(hr))
    {
        // デバッグレイヤーが入っていない環境ではフラグなしで再試行する
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
            featureLevels, 1, D3D11_SDK_VERSION, &sd,
            m_swapChain.GetAddressOf(), m_device.GetAddressOf(), &obtained, m_context.GetAddressOf());
        if (FAILED(hr))
        {
            MessageBoxW(hWnd, L"D3D11 デバイスの作成に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    // ---- 3. RTV / DSV ----
    if (!CreateBackBufferViews())
        return false;

    // ---- 4. 共通ステート ----
    if (!CreateCommonStates())
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// CreateBackBufferViews
//-----------------------------------------------------------------------------
bool Graphics::CreateBackBufferViews()
{
    // スワップチェーンからバックバッファのテクスチャを取得して RTV を作る
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // 深度バッファ: 24bit 深度 + 8bit ステンシル
    D3D11_TEXTURE2D_DESC td = {};
    td.Width      = m_width;
    td.Height     = m_height;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&td, nullptr, m_depthTex.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_device->CreateDepthStencilView(m_depthTex.Get(), nullptr, m_depthDSV.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // ビューポート: バックバッファ全体に描画する
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

//-----------------------------------------------------------------------------
// CreateCommonStates
//-----------------------------------------------------------------------------
bool Graphics::CreateCommonStates()
{
    HRESULT hr;

    // ラスタライザ: 塗りつぶし、裏面 (時計回り以外) をカリング
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode        = D3D11_FILL_SOLID;
    rd.CullMode        = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;   // 時計回りを表面とする (左手系)
    rd.DepthClipEnable = TRUE;
    hr = m_device->CreateRasterizerState(&rd, m_rsSolid.GetAddressOf());
    if (FAILED(hr)) return false;

    // ラスタライザ: カリングなし (向きを気にしなくてよいビルボードやフルスクリーン三角形用)
    rd.CullMode = D3D11_CULL_NONE;
    hr = m_device->CreateRasterizerState(&rd, m_rsNoCull.GetAddressOf());
    if (FAILED(hr)) return false;

    // 深度ステート: 通常 (テストあり・書き込みあり)
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable    = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc      = D3D11_COMPARISON_LESS;
    hr = m_device->CreateDepthStencilState(&dd, m_dsDefault.GetAddressOf());
    if (FAILED(hr)) return false;

    // 深度ステート: 無効 (フルスクリーンの後処理パス用)
    dd.DepthEnable    = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = m_device->CreateDepthStencilState(&dd, m_dsDisabled.GetAddressOf());
    if (FAILED(hr)) return false;

    // ブレンド: 不透明
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_device->CreateBlendState(&bd, m_bsOpaque.GetAddressOf());
    if (FAILED(hr)) return false;

    // ブレンド: アルファブレンド  result = src * srcAlpha + dst * (1 - srcAlpha)
    bd.RenderTarget[0].BlendEnable    = TRUE;
    bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    hr = m_device->CreateBlendState(&bd, m_bsAlpha.GetAddressOf());
    if (FAILED(hr)) return false;

    // サンプラー: 線形補間 + ミップマップ + 繰り返し (地形テクスチャ用)
    D3D11_SAMPLER_DESC sdesc = {};
    sdesc.Filter   = D3D11_FILTER_ANISOTROPIC;
    sdesc.MaxAnisotropy = 8;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.MaxLOD   = D3D11_FLOAT32_MAX;
    hr = m_device->CreateSamplerState(&sdesc, m_sampLinearWrap.GetAddressOf());
    if (FAILED(hr)) return false;

    // サンプラー: 補間なし + 端で固定 (深度テクスチャ用)
    sdesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sdesc.MaxAnisotropy = 1;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = m_device->CreateSamplerState(&sdesc, m_sampPointClamp.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

//-----------------------------------------------------------------------------
// Resize
//   手順: 既存の RTV/DSV を解放 → スワップチェーンのバッファをリサイズ → 再作成
//-----------------------------------------------------------------------------
bool Graphics::Resize(int width, int height)
{
    if (!m_swapChain || width <= 0 || height <= 0)
        return false;

    m_width  = width;
    m_height = height;

    // バックバッファを参照しているビューを先に解放しないとリサイズできない
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferRTV.Reset();
    m_depthDSV.Reset();
    m_depthTex.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;

    return CreateBackBufferViews();
}

//-----------------------------------------------------------------------------
// BeginFrame
//-----------------------------------------------------------------------------
void Graphics::BeginFrame(const float clearColor[4])
{
    ID3D11RenderTargetView* rtv = m_backBufferRTV.Get();
    m_context->OMSetRenderTargets(1, &rtv, m_depthDSV.Get());
    m_context->ClearRenderTargetView(rtv, clearColor);
    m_context->ClearDepthStencilView(m_depthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // ビューポートは Resize で変わる可能性があるので毎フレーム設定し直す
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

//-----------------------------------------------------------------------------
// EndFrame
//-----------------------------------------------------------------------------
void Graphics::EndFrame(bool vsync)
{
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

//-----------------------------------------------------------------------------
// CompileShaderFromFile
//   D3DCompileFromFile を呼び、失敗時はエラーメッセージを表示する。
//-----------------------------------------------------------------------------
bool Graphics::CompileShaderFromFile(const std::wstring& path, const char* entryPoint,
                                     const char* target, ComPtr<ID3DBlob>& outBlob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr,                                   // マクロ定義なし
        D3D_COMPILE_STANDARD_FILE_INCLUDE,         // #include を許可
        entryPoint,
        target,
        flags,
        0,
        outBlob.ReleaseAndGetAddressOf(),
        errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        std::string msg = "シェーダーのコンパイルに失敗しました:\n";
        if (errorBlob)
            msg += static_cast<const char*>(errorBlob->GetBufferPointer());
        else
            msg += "(ファイルが見つからない可能性があります)";

        // UTF-8 → ワイド文字に変換してメッセージボックスで表示
        int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
        std::wstring wmsg(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, &wmsg[0], len);
        MessageBoxW(nullptr, (wmsg + L"\n" + path).c_str(), L"HLSL エラー", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// CreateConstantBuffer
//-----------------------------------------------------------------------------
bool Graphics::CreateConstantBuffer(UINT byteSize, ComPtr<ID3D11Buffer>& outBuf)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = (byteSize + 15) & ~15u;       // 定数バッファは 16 バイト単位
    bd.Usage          = D3D11_USAGE_DYNAMIC;          // CPU から毎フレーム書き換える
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_device->CreateBuffer(&bd, nullptr, outBuf.ReleaseAndGetAddressOf()));
}

//-----------------------------------------------------------------------------
// UpdateBuffer
//   D3D11_MAP_WRITE_DISCARD: GPU が前の内容を使い終わるのを待たずに
//   新しいメモリ領域を割り当ててもらう (毎フレーム更新するバッファの定石)
//-----------------------------------------------------------------------------
void Graphics::UpdateBuffer(ID3D11Buffer* buf, const void* data, size_t byteSize)
{
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, data, byteSize);
        m_context->Unmap(buf, 0);
    }
}
