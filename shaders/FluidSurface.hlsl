//=============================================================================
// FluidSurface.hlsl
//   スクリーンスペース流体レンダリング (Screen Space Fluid Rendering) の
//   「ぼかし」と「合成」パス。
//
//   表面生成の流れ (van der Laan et al., 2009 を簡略化):
//     1. Particle.hlsl の PSDepth で各粒子を球として描き、
//        ビュー空間の深度をテクスチャ (R32_FLOAT) に書く。
//        → この段階では球がボコボコ並んだ見た目
//     2. PSBlur で深度テクスチャをバイラテラルフィルタで平滑化する。
//        深度の差が大きい場所 (流体の輪郭) はぼかさないので、
//        輪郭を保ったまま球同士の継ぎ目だけが滑らかになる。
//        横方向・縦方向を分けて掛ける (分離可能フィルタ) ことで高速化。
//     3. PSComposite で平滑化された深度からビュー空間座標を復元し、
//        隣接ピクセルとの差分 (偏微分) から法線を求めて水面として陰影を付け、
//        地形の上に半透明で合成する。
//=============================================================================
#include "Common.hlsli"

Texture2D<float> DepthTex   : register(t0);   // 流体の深度 (ビュー空間 z、0 = 流体なし)
SamplerState     PointClamp : register(s0);

static const float BLUR_WORLD_RADIUS = 0.90;   // ぼかし半径 (ワールド単位)。粒子径の 2～3 倍程度
static const float BLUR_DEPTH_SIGMA  = 1.50;   // この深度差 [m] 程度までは同じ面として滑らかにつなぐ
                                               // (流体の外側 (深度 0) は別途除外するので、球の凹凸より十分大きくしてよい)
static const int   BLUR_MAX_TAPS     = 24;     // 片側の最大サンプル数 (ピクセル)

//-----------------------------------------------------------------------------
// 関数名 : PSBlur
// 概要   : 1 方向 (Params.zw で指定) のバイラテラルぼかし
//          アルゴリズム:
//            radiusPx = ワールド半径を画面に投影したピクセル数 (近いほど大きい)
//            重み w = exp(-k^2 / 2σs^2) * exp(-(d_k - d_0)^2 / 2σd^2)
//            出力   = Σ w d_k / Σ w
// 引数   : i : フルスクリーン三角形の補間値 (posH.xy がピクセル座標)
// 戻り値 : ぼかした深度
//-----------------------------------------------------------------------------
float PSBlur(FullscreenVSOut i) : SV_Target
{
    int2 pix = int2(i.posH.xy);
    float d0 = DepthTex.Load(int3(pix, 0));
    if (d0 <= 0.0)
        return 0.0;   // 流体がないピクセル

    // ワールド半径 → ピクセル半径 (透視投影: 画面上の大きさ ∝ 1/深度)
    float radiusPx = BLUR_WORLD_RADIUS / d0 * Proj._m11 * ScreenSize.y * 0.5;
    radiusPx = clamp(radiusPx, 2.0, float(BLUR_MAX_TAPS));

    float sigmaS = radiusPx * 0.5;                    // 空間方向の標準偏差 [px]
    float invS2 = 1.0 / (2.0 * sigmaS * sigmaS);
    float invD2 = 1.0 / (2.0 * BLUR_DEPTH_SIGMA * BLUR_DEPTH_SIGMA);

    int2 dir = int2(Params.zw);                        // (1,0) または (0,1)
    int2 maxPix = int2(ScreenSize.xy) - 1;

    float sum = 0.0;
    float wsum = 0.0;

    [loop]
    for (int k = -BLUR_MAX_TAPS; k <= BLUR_MAX_TAPS; ++k)
    {
        if (abs(k) > radiusPx)
            continue;

        int2 q = clamp(pix + dir * k, int2(0, 0), maxPix);
        float d = DepthTex.Load(int3(q, 0));
        if (d <= 0.0)
            continue;   // 流体外は無視 (輪郭が内側へ縮まないように)

        float dd = d - d0;
        float w = exp(-float(k * k) * invS2) * exp(-dd * dd * invD2);
        sum  += d * w;
        wsum += w;
    }
    return sum / max(wsum, 1e-6);
}

//-----------------------------------------------------------------------------
// 関数名 : ViewPosFromDepth
// 概要   : ピクセル座標と深度からビュー空間座標を復元する
//          NDC x = x_view * Proj._m00 / z_view  →  x_view = ndc.x * z / Proj._m00
// 引数   : pix : ピクセル座標 (整数)
//          d   : そのピクセルのビュー空間深度
// 戻り値 : ビュー空間座標
//-----------------------------------------------------------------------------
float3 ViewPosFromDepth(int2 pix, float d)
{
    float2 ndc = (float2(pix) + 0.5) * ScreenSize.zw * 2.0 - 1.0;
    ndc.y = -ndc.y;   // 画面座標は下向きが正、NDC は上向きが正
    return float3(ndc.x * d / Proj._m00, ndc.y * d / Proj._m11, d);
}

//-----------------------------------------------------------------------------
// 関数名 : PSComposite
// 概要   : 平滑化された深度から法線を求めて水面をシェーディングし、
//          アルファブレンドで地形の上に描く。
//          法線の求め方:
//            右隣・左隣との位置差 ddx, 上隣・下隣との位置差 ddy を計算し、
//            輪郭 (深度が飛ぶ場所) を避けるため差の小さい方を採用して
//            n = normalize(cross(ddx, ddy)) とする。
// 引数   : i : フルスクリーン三角形の補間値
// 戻り値 : 水面の色 (アルファ付き)
//-----------------------------------------------------------------------------
float4 PSComposite(FullscreenVSOut i) : SV_Target
{
    int2 pix = int2(i.posH.xy);
    float d0 = DepthTex.Load(int3(pix, 0));
    if (d0 <= 0.0)
        discard;

    int2 maxPix = int2(ScreenSize.xy) - 1;
    float3 p = ViewPosFromDepth(pix, d0);

    // ---- x 方向の偏微分 ----
    float dR = DepthTex.Load(int3(min(pix + int2(1, 0), maxPix), 0));
    float dL = DepthTex.Load(int3(max(pix - int2(1, 0), int2(0, 0)), 0));
    float3 ddxR = ViewPosFromDepth(pix + int2(1, 0), dR) - p;
    float3 ddxL = p - ViewPosFromDepth(pix - int2(1, 0), dL);
    float3 ddx;
    if (dR <= 0.0)       ddx = ddxL;
    else if (dL <= 0.0)  ddx = ddxR;
    else                 ddx = (abs(ddxR.z) < abs(ddxL.z)) ? ddxR : ddxL;

    // ---- y 方向の偏微分 ----
    float dD = DepthTex.Load(int3(min(pix + int2(0, 1), maxPix), 0));
    float dU = DepthTex.Load(int3(max(pix - int2(0, 1), int2(0, 0)), 0));
    float3 ddyD = ViewPosFromDepth(pix + int2(0, 1), dD) - p;
    float3 ddyU = p - ViewPosFromDepth(pix - int2(0, 1), dU);
    float3 ddy;
    if (dD <= 0.0)       ddy = ddyU;
    else if (dU <= 0.0)  ddy = ddyD;
    else                 ddy = (abs(ddyD.z) < abs(ddyU.z)) ? ddyD : ddyU;

    // 画面の +x は右 (ビュー +x)、ピクセルの +y は下 (ビュー -y) なので
    // cross(ddx, ddy) = cross(+x, -y) = -z となり、カメラ側を向く法線になる
    float3 n = normalize(cross(ddx, ddy));
    if (n.z > 0.0) n = -n;   // 念のためカメラ側を向かせる

    // ---- シェーディング ----
    float3 L = normalize(LightDirV.xyz);
    float3 V = normalize(-p);
    float3 H = normalize(L + V);

    float diff = saturate(dot(n, L));
    float spec = pow(saturate(dot(n, H)), 48.0);
    float fresnel = 0.06 + 0.94 * pow(1.0 - saturate(dot(n, V)), 4.0);   // Schlick の近似

    const float3 waterColor = float3(0.64, 0.68, 0.78);   // 参考動画の灰青色
    const float3 skyColor   = float3(0.90, 0.93, 0.98);   // 反射する空の色

    float3 col = waterColor * (0.50 + 0.50 * diff);
    col = lerp(col, skyColor, fresnel * 0.5);
    col += spec * 0.35;

    float alpha = 0.90;
    return float4(col, alpha);
}
