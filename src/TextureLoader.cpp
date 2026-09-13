//=============================================================================
// TextureLoader.cpp
//   WIC による画像ファイル読み込みの実装。
//=============================================================================
#include "TextureLoader.h"
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    //-------------------------------------------------------------------------
    // 関数名 : GetWICFactory
    // 概要   : WIC ファクトリを 1 回だけ作成して使い回す
    //          (COM はスレッドごとに初期化が必要なので、ここで CoInitializeEx を呼ぶ。
    //           すでに初期化済み (S_FALSE / RPC_E_CHANGED_MODE) でも続行してよい)
    // 引数   : なし
    // 戻り値 : ファクトリ (失敗時は nullptr)
    //-------------------------------------------------------------------------
    IWICImagingFactory* GetWICFactory()
    {
        static ComPtr<IWICImagingFactory> s_factory;
        if (s_factory)
            return s_factory.Get();

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(s_factory.GetAddressOf()));
        if (FAILED(hr))
            return nullptr;
        return s_factory.Get();
    }
}

namespace TextureLoader
{
    //-------------------------------------------------------------------------
    // CreateFromPixels
    //   ミップマップ自動生成のため、RENDER_TARGET バインドと GENERATE_MIPS フラグを付ける。
    //   MipLevels = 0 にすると 1x1 までの全レベルが確保される。
    //-------------------------------------------------------------------------
    bool CreateFromPixels(Graphics& gfx, const void* pixels, UINT width, UINT height,
                          ComPtr<ID3D11ShaderResourceView>& outSRV)
    {
        if (!pixels || width == 0 || height == 0)
            return false;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width     = width;
        td.Height    = height;
        td.MipLevels = 0;                                   // 0 = 全ミップレベル
        td.ArraySize = 1;
        td.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage     = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(gfx.GetDevice()->CreateTexture2D(&td, nullptr, tex.GetAddressOf())))
            return false;

        // レベル 0 にピクセルを書き込む (行ピッチ = 幅 × 4 バイト)
        gfx.GetContext()->UpdateSubresource(tex.Get(), 0, nullptr, pixels, width * 4, 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = static_cast<UINT>(-1);   // 全ミップレベル

        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(gfx.GetDevice()->CreateShaderResourceView(tex.Get(), &srvd, srv.GetAddressOf())))
            return false;

        gfx.GetContext()->GenerateMips(srv.Get());
        outSRV = srv;
        return true;
    }

    //-------------------------------------------------------------------------
    // LoadFromFile
    //-------------------------------------------------------------------------
    bool LoadFromFile(Graphics& gfx, const std::wstring& path,
                      ComPtr<ID3D11ShaderResourceView>& outSRV,
                      UINT* outWidth, UINT* outHeight)
    {
        IWICImagingFactory* factory = GetWICFactory();
        if (!factory)
            return false;

        // 1. デコーダでファイルを開く (拡張子ではなく中身で形式を判定してくれる)
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
        if (FAILED(hr))
            return false;

        // 2. 先頭フレーム (静止画は 1 枚、GIF などは複数枚のうち最初)
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr))
            return false;

        // 3. ピクセル形式を 32bpp RGBA (R が先頭バイト) に統一
        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr))
            return false;
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
            return false;

        UINT width = 0, height = 0;
        hr = converter->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0)
            return false;

        // 4. メモリへ展開
        const UINT stride = width * 4;                       // 1 行のバイト数
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr))
            return false;

        // 5-6. テクスチャ作成 + ミップマップ生成
        if (!CreateFromPixels(gfx, pixels.data(), width, height, outSRV))
            return false;

        if (outWidth)  *outWidth  = width;
        if (outHeight) *outHeight = height;
        return true;
    }
}
