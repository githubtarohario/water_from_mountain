//=============================================================================
// FrameConstants.h
//   全シェーダーで共通に使う「1 フレームごとの定数」の構造体。
//   HLSL 側の cbuffer CBFrame (register b0) とメモリ配置を完全に一致させること。
//
//   注意:
//     - HLSL の定数バッファは 16 バイト単位でパックされるため、
//       float4 / float4x4 のみで構成して境界を揃えている。
//     - 行列は CPU 側 (DirectXMath, 行優先) で転置してから格納する。
//       こうすると HLSL 側で mul(float4(pos,1), Matrix) と書ける。
//=============================================================================
#pragma once

#include <DirectXMath.h>

struct FrameConstants
{
    DirectX::XMFLOAT4X4 View;        // ワールド → ビュー変換行列 (転置済み)
    DirectX::XMFLOAT4X4 Proj;        // ビュー → クリップ変換行列 (転置済み)
    DirectX::XMFLOAT4X4 ViewProj;    // View * Proj (転置済み)
    DirectX::XMFLOAT4   CameraPosW;  // xyz: カメラのワールド座標
    DirectX::XMFLOAT4   LightDirW;   // xyz: ワールド空間の「光源へ向かう」単位ベクトル
    DirectX::XMFLOAT4   LightDirV;   // xyz: 同じベクトルをビュー空間に変換したもの
    DirectX::XMFLOAT4   ScreenSize;  // x: 幅, y: 高さ, z: 1/幅, w: 1/高さ (ピクセル)
    DirectX::XMFLOAT4   Params;      // x: 粒子描画半径, y: 経過時間[秒], z,w: ぼかし方向 (1,0) or (0,1)
};

// 16 バイト境界のチェック (コンパイル時に検出)
static_assert(sizeof(FrameConstants) % 16 == 0, "FrameConstants must be 16-byte aligned");
