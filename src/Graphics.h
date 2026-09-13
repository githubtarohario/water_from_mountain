//=============================================================================
// Graphics.h
//   DirectX 11 の初期化と、各モジュールで共通に使う描画リソースをまとめたクラス。
//
//   担当すること:
//     - デバイス / デバイスコンテキスト / スワップチェーンの作成
//     - バックバッファ用レンダーターゲットビュー (RTV) と深度バッファ (DSV)
//     - ウィンドウサイズ変更時のバッファ再作成
//     - よく使うステート (ラスタライザ・深度・ブレンド・サンプラー) の作成
//     - シェーダーのコンパイル、定数バッファ作成などのヘルパー関数
//=============================================================================
#pragma once

#ifndef NOMINMAX
#define NOMINMAX   // windows.h の min/max マクロが std::min/std::max と衝突するのを防ぐ
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

class Graphics
{
public:
    Graphics() = default;
    ~Graphics() = default;

    //-------------------------------------------------------------------------
    // 関数名 : Initialize
    // 概要   : D3D11 デバイス・スワップチェーン・各種ステートを作成する
    // 引数   : hWnd   : 描画先ウィンドウのハンドル
    //          width  : バックバッファの幅 (ピクセル)
    //          height : バックバッファの高さ (ピクセル)
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool Initialize(HWND hWnd, int width, int height);

    //-------------------------------------------------------------------------
    // 関数名 : Resize
    // 概要   : ウィンドウサイズ変更に合わせてバックバッファと深度バッファを作り直す
    // 引数   : width, height : 新しいサイズ (ピクセル)
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool Resize(int width, int height);

    //-------------------------------------------------------------------------
    // 関数名 : BeginFrame
    // 概要   : バックバッファと深度バッファをクリアし、描画先として設定する
    // 引数   : clearColor : クリア色 (RGBA, 各 0～1)
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void BeginFrame(const float clearColor[4]);

    //-------------------------------------------------------------------------
    // 関数名 : EndFrame
    // 概要   : 描画結果を画面に表示する (Present)
    // 引数   : vsync : true なら垂直同期を待つ
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void EndFrame(bool vsync);

    //-------------------------------------------------------------------------
    // 関数名 : CompileShaderFromFile
    // 概要   : HLSL ファイルを読み込んでコンパイルし、バイトコードを返す
    // 引数   : path       : HLSL ファイルのパス
    //          entryPoint : エントリーポイント関数名 (例 "VSMain")
    //          target     : シェーダーモデル (例 "vs_5_0", "ps_5_0")
    //          outBlob    : [出力] コンパイル済みバイトコード
    // 戻り値 : true = 成功, false = 失敗 (エラー内容はメッセージボックスに表示)
    //-------------------------------------------------------------------------
    static bool CompileShaderFromFile(const std::wstring& path, const char* entryPoint,
                                      const char* target, ComPtr<ID3DBlob>& outBlob);

    //-------------------------------------------------------------------------
    // 関数名 : SetShaderDirectory / ShaderPath
    // 概要   : HLSL ファイルを置いたフォルダを登録し、ファイル名からフルパスを作る
    // 引数   : dir  : シェーダーフォルダ (末尾の区切り文字はあってもなくてもよい)
    //          file : ファイル名 (例 L"Terrain.hlsl")
    // 戻り値 : ShaderPath はフルパス文字列
    //-------------------------------------------------------------------------
    static void SetShaderDirectory(const std::wstring& dir);
    static std::wstring ShaderPath(const wchar_t* file);

    //-------------------------------------------------------------------------
    // 関数名 : SetAssetDirectory / AssetPath
    // 概要   : 画像などのアセットを置いたフォルダを登録し、ファイル名からフルパスを作る
    // 引数   : dir  : アセットフォルダ
    //          file : ファイル名 (例 L"rock.png")
    // 戻り値 : AssetPath はフルパス文字列
    //-------------------------------------------------------------------------
    static void SetAssetDirectory(const std::wstring& dir);
    static std::wstring AssetPath(const wchar_t* file);

    //-------------------------------------------------------------------------
    // 関数名 : CreateConstantBuffer
    // 概要   : 毎フレーム CPU から更新する定数バッファ (DYNAMIC) を作る
    // 引数   : byteSize : バッファサイズ (16 の倍数に切り上げられる)
    //          outBuf   : [出力] 作成したバッファ
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateConstantBuffer(UINT byteSize, ComPtr<ID3D11Buffer>& outBuf);

    //-------------------------------------------------------------------------
    // 関数名 : UpdateBuffer
    // 概要   : DYNAMIC バッファに Map/Unmap でデータを書き込む
    // 引数   : buf      : 書き込み先バッファ
    //          data     : 書き込むデータの先頭アドレス
    //          byteSize : 書き込むバイト数
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void UpdateBuffer(ID3D11Buffer* buf, const void* data, size_t byteSize);

    // ---- アクセサ (各モジュールが描画に使う) ----
    ID3D11Device*            GetDevice()  const { return m_device.Get(); }
    ID3D11DeviceContext*     GetContext() const { return m_context.Get(); }
    ID3D11RenderTargetView*  GetBackBufferRTV() const { return m_backBufferRTV.Get(); }
    ID3D11DepthStencilView*  GetDepthStencilView() const { return m_depthDSV.Get(); }
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }

    // ---- 共通ステート ----
    ID3D11RasterizerState*   GetRasterizerSolid()   const { return m_rsSolid.Get(); }
    ID3D11RasterizerState*   GetRasterizerNoCull()  const { return m_rsNoCull.Get(); }
    ID3D11DepthStencilState* GetDepthStateDefault() const { return m_dsDefault.Get(); }
    ID3D11DepthStencilState* GetDepthStateDisabled()const { return m_dsDisabled.Get(); }
    ID3D11BlendState*        GetBlendOpaque()       const { return m_bsOpaque.Get(); }
    ID3D11BlendState*        GetBlendAlpha()        const { return m_bsAlpha.Get(); }
    ID3D11SamplerState*      GetSamplerLinearWrap() const { return m_sampLinearWrap.Get(); }
    ID3D11SamplerState*      GetSamplerPointClamp() const { return m_sampPointClamp.Get(); }

private:
    //-------------------------------------------------------------------------
    // 関数名 : CreateBackBufferViews
    // 概要   : スワップチェーンのバックバッファから RTV を作り、
    //          同じサイズの深度バッファ (D24S8) と DSV を作る
    // 引数   : なし (m_width, m_height を使用)
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateBackBufferViews();

    //-------------------------------------------------------------------------
    // 関数名 : CreateCommonStates
    // 概要   : ラスタライザ・深度・ブレンド・サンプラーの各ステートを作成する
    // 引数   : なし
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateCommonStates();

    ComPtr<ID3D11Device>            m_device;         // D3D11 デバイス (リソース作成用)
    ComPtr<ID3D11DeviceContext>     m_context;        // 即時コンテキスト (描画コマンド発行用)
    ComPtr<IDXGISwapChain>          m_swapChain;      // 画面表示用スワップチェーン
    ComPtr<ID3D11RenderTargetView>  m_backBufferRTV;  // バックバッファのレンダーターゲットビュー
    ComPtr<ID3D11Texture2D>         m_depthTex;       // 深度バッファ本体
    ComPtr<ID3D11DepthStencilView>  m_depthDSV;       // 深度バッファのビュー

    ComPtr<ID3D11RasterizerState>   m_rsSolid;        // 通常の塗りつぶし描画 (裏面カリングあり)
    ComPtr<ID3D11RasterizerState>   m_rsNoCull;       // カリングなし (ビルボード・フルスクリーン用)
    ComPtr<ID3D11DepthStencilState> m_dsDefault;      // 深度テストあり・書き込みあり
    ComPtr<ID3D11DepthStencilState> m_dsDisabled;     // 深度テストなし (フルスクリーンパス用)
    ComPtr<ID3D11BlendState>        m_bsOpaque;       // ブレンドなし
    ComPtr<ID3D11BlendState>        m_bsAlpha;        // 通常のアルファブレンド
    ComPtr<ID3D11SamplerState>      m_sampLinearWrap; // テクスチャ用 (線形補間・繰り返し)
    ComPtr<ID3D11SamplerState>      m_sampPointClamp; // 深度テクスチャ用 (補間なし・端で固定)

    int m_width  = 0;   // 現在のバックバッファ幅
    int m_height = 0;   // 現在のバックバッファ高さ

    static std::wstring s_shaderDir;   // シェーダーフォルダ (SetShaderDirectory で設定)
    static std::wstring s_assetDir;    // アセットフォルダ (SetAssetDirectory で設定)
};
