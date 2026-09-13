//=============================================================================
// FluidRenderer.h
//   SPH 粒子の描画を担当するクラス。2 つの表示モードを持つ。
//
//   [Particles モード] 参考動画の前半
//     各粒子を速度で着色した球 (スフィア・インポスター) として描く。
//
//   [Surface モード] 参考動画の後半「表面生成」
//     スクリーンスペース流体レンダリング:
//       1. 粒子を球として深度テクスチャ (R32_FLOAT) に描く
//       2. 深度テクスチャをバイラテラルフィルタで平滑化 (横→縦を数回)
//       3. 平滑化した深度から法線を復元し、水面として陰影を付けて合成
//
//   粒子データは毎フレーム CPU から StructuredBuffer に転送し、
//   頂点バッファなしの DrawInstanced (4 頂点 × 粒子数) で描画する。
//=============================================================================
#pragma once

#include "Graphics.h"
#include "FrameConstants.h"
#include <DirectXMath.h>
#include <vector>

enum class FluidRenderMode
{
    Particles,   // 粒子を球として表示
    Surface      // 表面を生成して水として表示
};

class FluidRenderer
{
public:
    //-------------------------------------------------------------------------
    // 関数名 : Initialize
    // 概要   : シェーダー・粒子バッファ・深度テクスチャを作成する
    // 引数   : gfx          : Graphics
    //          maxParticles : 粒子バッファの最大要素数
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool Initialize(Graphics& gfx, int maxParticles);

    //-------------------------------------------------------------------------
    // 関数名 : OnResize
    // 概要   : 画面サイズに依存する深度テクスチャを作り直す
    // 引数   : gfx : Graphics (新しいサイズを持っている)
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool OnResize(Graphics& gfx);

    //-------------------------------------------------------------------------
    // 関数名 : Render
    // 概要   : 粒子を指定モードで描画する。
    //          地形の描画後 (バックバッファと深度バッファが有効な状態) に呼ぶこと。
    // 引数   : gfx       : Graphics
    //          particles : (x, y, z, 速さ) の配列
    //          mode      : 表示モード
    //          frame     : フレーム定数 (Params を書き換えて再アップロードするため参照で受ける)
    //          cbFrame   : フレーム定数バッファ (b0)
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void Render(Graphics& gfx, const std::vector<DirectX::XMFLOAT4>& particles,
                FluidRenderMode mode, FrameConstants& frame, ID3D11Buffer* cbFrame);

    // 表示半径の設定 (粒子モード / 表面モード)
    void SetParticleRadius(float r) { m_particleRadius = r; }
    void SetSurfaceRadius(float r)  { m_surfaceRadius = r; }
    void SetBlurIterations(int n)   { m_blurIterations = n; }

private:
    //-------------------------------------------------------------------------
    // 関数名 : CreateShaders
    // 概要   : Particle.hlsl / FluidSurface.hlsl をコンパイルしてシェーダーを作る
    // 引数   : gfx : Graphics
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateShaders(Graphics& gfx);

    //-------------------------------------------------------------------------
    // 関数名 : CreateDepthTargets
    // 概要   : 画面サイズの R32_FLOAT テクスチャを 2 枚 (ピンポン用) 作る
    // 引数   : gfx : Graphics
    // 戻り値 : true = 成功, false = 失敗
    //-------------------------------------------------------------------------
    bool CreateDepthTargets(Graphics& gfx);

    //-------------------------------------------------------------------------
    // 関数名 : UploadParticles
    // 概要   : 粒子配列を StructuredBuffer にコピーする
    // 引数   : gfx       : Graphics
    //          particles : 粒子配列
    // 戻り値 : 転送した粒子数 (最大数を超えた分は切り捨て)
    //-------------------------------------------------------------------------
    UINT UploadParticles(Graphics& gfx, const std::vector<DirectX::XMFLOAT4>& particles);

    //-------------------------------------------------------------------------
    // 関数名 : DrawSpheres
    // 概要   : 現在バインドされているレンダーターゲットに粒子の球を描く
    // 引数   : gfx   : Graphics
    //          count : 粒子数
    //          ps    : 使うピクセルシェーダー (色 or 深度)
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void DrawSpheres(Graphics& gfx, UINT count, ID3D11PixelShader* ps);

    //-------------------------------------------------------------------------
    // 関数名 : DrawFullscreen
    // 概要   : フルスクリーン三角形を 1 枚描く (ぼかし・合成パス用)
    // 引数   : gfx : Graphics
    //          ps  : ピクセルシェーダー
    //          src : 入力テクスチャ (t0)
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void DrawFullscreen(Graphics& gfx, ID3D11PixelShader* ps, ID3D11ShaderResourceView* src);

    //-------------------------------------------------------------------------
    // 関数名 : RenderParticlesMode / RenderSurfaceMode
    // 概要   : 各表示モードの描画本体
    //-------------------------------------------------------------------------
    void RenderParticlesMode(Graphics& gfx, UINT count, FrameConstants& frame, ID3D11Buffer* cbFrame);
    void RenderSurfaceMode(Graphics& gfx, UINT count, FrameConstants& frame, ID3D11Buffer* cbFrame);

    // ---- 粒子バッファ ----
    int                               m_maxParticles = 0;   // バッファの最大要素数
    ComPtr<ID3D11Buffer>              m_particleBuffer;     // StructuredBuffer<float4> (DYNAMIC)
    ComPtr<ID3D11ShaderResourceView>  m_particleSRV;        // その SRV (t0)

    // ---- シェーダー ----
    ComPtr<ID3D11VertexShader>        m_vsParticle;   // ビルボード生成
    ComPtr<ID3D11PixelShader>         m_psColor;      // 着色した球
    ComPtr<ID3D11PixelShader>         m_psDepth;      // 深度出力
    ComPtr<ID3D11VertexShader>        m_vsFullscreen; // フルスクリーン三角形
    ComPtr<ID3D11PixelShader>         m_psBlur;       // バイラテラルぼかし
    ComPtr<ID3D11PixelShader>         m_psComposite;  // 水面の合成

    // ---- 深度テクスチャ (ピンポン用に 2 枚) ----
    ComPtr<ID3D11Texture2D>           m_depthTex[2];
    ComPtr<ID3D11RenderTargetView>    m_depthRTV[2];
    ComPtr<ID3D11ShaderResourceView>  m_depthSRV[2];

    // ---- 設定 ----
    float m_particleRadius = 0.15f;   // 粒子モードでの球の半径
    float m_surfaceRadius  = 0.30f;   // 表面モードでの球の半径 (隣と重なるよう大きめ)
    int   m_blurIterations = 4;       // ぼかしの反復回数 (横+縦で 1 回)
};
