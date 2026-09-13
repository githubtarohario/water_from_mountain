//=============================================================================
// TextureLoader.h
//   画像ファイル (PNG / JPG / BMP / TIFF / GIF など) を読み込んで
//   D3D11 のテクスチャ (シェーダーリソースビュー) を作るモジュール。
//
//   Windows 標準の WIC (Windows Imaging Component) を使うため、
//   外部ライブラリ (DirectXTK や stb_image) は不要。
//
//   読み込みの流れ:
//     1. IWICImagingFactory を作成
//     2. CreateDecoderFromFilename でファイルを開き、先頭フレームを取得
//     3. IWICFormatConverter でピクセル形式を 32bpp RGBA に統一
//     4. CopyPixels でメモリに展開
//     5. ミップマップ付きの ID3D11Texture2D を作成し、レベル 0 に書き込み
//     6. GenerateMips で縮小版を自動生成し、SRV を返す
//=============================================================================
#pragma once

#include "Graphics.h"
#include <string>

namespace TextureLoader
{
    //-------------------------------------------------------------------------
    // 関数名 : LoadFromFile
    // 概要   : 画像ファイルを読み込み、ミップマップ付きテクスチャの SRV を作る
    // 引数   : gfx     : Graphics (デバイス・コンテキスト取得用)
    //          path    : 画像ファイルのパス (絶対パスまたはカレントディレクトリ相対)
    //          outSRV  : [出力] 作成したシェーダーリソースビュー
    //          outWidth, outHeight : [出力・省略可] 画像の幅と高さ [px]
    // 戻り値 : true = 成功, false = 失敗 (ファイルなし・形式非対応・デバイスエラーなど)
    //          失敗時は outSRV を変更しない。エラーはメッセージボックスを出さず、
    //          呼び出し側でフォールバックできるようにしている。
    //-------------------------------------------------------------------------
    bool LoadFromFile(Graphics& gfx, const std::wstring& path,
                      ComPtr<ID3D11ShaderResourceView>& outSRV,
                      UINT* outWidth = nullptr, UINT* outHeight = nullptr);

    //-------------------------------------------------------------------------
    // 関数名 : CreateFromPixels
    // 概要   : メモリ上の RGBA8 ピクセル配列からミップマップ付きテクスチャの SRV を作る
    //          (ファイル読み込みと手続き生成の両方が共通で使う)
    // 引数   : gfx      : Graphics
    //          pixels   : RGBA8 ピクセル (width * height * 4 バイト、行は詰めて並ぶ)
    //          width    : 幅 [px]
    //          height   : 高さ [px]
    //          outSRV   : [出力] 作成した SRV
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateFromPixels(Graphics& gfx, const void* pixels, UINT width, UINT height,
                          ComPtr<ID3D11ShaderResourceView>& outSRV);
}
