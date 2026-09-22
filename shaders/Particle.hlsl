//=============================================================================
// Particle.hlsl
//   SPH 粒子を「ビルボード上の球 (スフィア・インポスター)」として描画する。
//
//   仕組み:
//     1. 頂点シェーダー: 粒子ごとに 4 頂点の四角形 (ビルボード) をビュー空間で作る。
//        頂点バッファは使わず SV_VertexID / SV_InstanceID から座標を計算する。
//     2. ピクセルシェーダー: 四角形内の座標 uv (-1～1) から
//          r^2 = uv.x^2 + uv.y^2 > 1 なら球の外 → discard
//          法線 n = (uv.x, uv.y, -sqrt(1 - r^2))   (カメラ側を向く)
//        として球面上の点を求め、正しい深度 (SV_Depth) を書き込む。
//        こうすると平面の四角形なのに本物の球のように見え、球同士や地形と正しく交差する。
//
//   エントリーポイント:
//     VSMain     : 共通の頂点シェーダー
//     PSColor    : 速度で着色した球を描く (粒子表示モード)
//     PSDepth    : ビュー空間の深度 z を R32_FLOAT に出力する (表面生成モードの第 1 パス)
//=============================================================================
#include "Common.hlsli"

StructuredBuffer<float4> Particles : register(t0);   // xyz: 位置 (ワールド), w: 速さ

static const float MAX_SPEED_FOR_COLOR = 6.0;   // この速さ [m/s] で赤になる

//-----------------------------------------------------------------------------
// 頂点シェーダー → ピクセルシェーダー
//-----------------------------------------------------------------------------
struct PSIn
{
    float4 posH    : SV_POSITION;   // クリップ座標
    float2 uv      : TEXCOORD0;     // 四角形内の座標 (-1～1)
    float3 centerV : TEXCOORD1;     // 球の中心 (ビュー空間)
    float  speed   : TEXCOORD2;     // 速さ [m/s]
};

//-----------------------------------------------------------------------------
// 関数名 : VSMain
// 概要   : 粒子 iid の中心をビュー空間に変換し、頂点 vid に対応する四角形の角を出力する
// 引数   : vid : SV_VertexID (0～3、トライアングルストリップの順)
//          iid : SV_InstanceID (粒子番号)
// 戻り値 : PSIn
//-----------------------------------------------------------------------------
PSIn VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    float4 p = Particles[iid];      // 粒子データ (xyz = ワールド座標, w = 速さ)
    // ワールド座標をカメラ基準 (ビュー空間) に変換する。
    // ビュー空間では「画面の右 = +x、上 = +y」なので、四角形を作るのが簡単になる。
    float3 centerV = mul(float4(p.xyz, 1.0), View).xyz;

    // トライアングルストリップ用の 4 隅: 左下, 左上, 右下, 右上
    float2 corners[4] = { float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1) };
    float2 c = corners[vid];        // 今回の頂点がどの角かを番号で選ぶ

    float r = Params.x;   // 粒子の描画半径
    // 中心から xy 方向にだけずらす (z は動かさない) ので、四角形は常にカメラに正対する
    float3 posV = centerV + float3(c * r, 0.0);   // 常にカメラに正対する四角形

    PSIn o;
    o.posH    = mul(float4(posV, 1.0), Proj);   // 射影してクリップ座標へ (ここで遠近感が付く)
    o.uv      = c;                              // ピクセルシェーダーで球の形を作るのに使う
    o.centerV = centerV;                        // 球の中心 (ピクセル側で球面上の点を求めるため)
    o.speed   = p.w;                            // 着色に使う速さ
    return o;
}

//-----------------------------------------------------------------------------
// 関数名 : SphereSurface
// 概要   : 四角形内の座標から球面上の点と法線を求める (共通処理)
// 引数   : i       : ピクセル入力
//          normalV : [出力] 球面の法線 (ビュー空間)
//          posV    : [出力] 球面上の点 (ビュー空間)
//          depth   : [出力] 正規化デバイス座標の深度 (SV_Depth 用)
// 戻り値 : true = 球の内側, false = 球の外側 (discard すべき)
//-----------------------------------------------------------------------------
bool SphereSurface(PSIn i, out float3 normalV, out float3 posV, out float depth)
{
    float r2 = dot(i.uv, i.uv);     // 四角形の中心からの距離の 2 乗 (中心で 0、角で 2)
    normalV = 0; posV = 0; depth = 0;   // out 引数は必ず初期化しておく (未設定だとコンパイルエラー)
    if (r2 > 1.0)
        return false;               // 半径 1 の円の外 = 球からはみ出した部分なので描かない

    // 球を正面から見たとき、中心から r 離れた点の高さは sqrt(1 - r²)。
    // 左手系のビュー空間ではカメラは +z を向くので、カメラ側の半球は z < 0
    normalV = float3(i.uv, -sqrt(1.0 - r2));    // そのまま法線になる (長さ 1)
    posV    = i.centerV + normalV * Params.x;   // 球面上の実際の位置 = 中心 + 法線 × 半径

    // その点を射影して深度を求める。w で割るのが透視除算で、結果は 0 (手前) ～ 1 (奥)
    float4 clip = mul(float4(posV, 1.0), Proj);
    depth = clip.z / clip.w;
    return true;
}

//-----------------------------------------------------------------------------
// PSColor の出力: 色と深度
//-----------------------------------------------------------------------------
struct PSColorOut
{
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

//-----------------------------------------------------------------------------
// 関数名 : PSColor
// 概要   : 速度で着色した球を描く。拡散反射 + 鏡面反射でつやのある見た目にする。
// 引数   : i : ピクセル入力
// 戻り値 : 色と深度
//-----------------------------------------------------------------------------
PSColorOut PSColor(PSIn i)
{
    float3 n, posV; float depth;
    if (!SphereSurface(i, n, posV, depth))
        discard;

    float3 L = normalize(LightDirV.xyz);    // 表面から光源へ向かう向き
    float3 V = normalize(-posV);            // 表面からカメラへ (カメラは原点なので位置の逆向き)
    float3 H = normalize(L + V);            // ハーフベクトル (Blinn-Phong)

    float diff = saturate(dot(n, L));           // 拡散反射: 正面から当たるほど明るい (0～1)
    float spec = pow(saturate(dot(n, H)), 40.0);// 鏡面反射: 指数が大きいほどハイライトが小さく鋭い

    float3 base = JetColor(i.speed / MAX_SPEED_FOR_COLOR);   // 速さ 0～1 を青→赤の色に変換
    float3 col = base * (0.35 + 0.75 * diff) + spec * 0.5;   // 0.35 は影の部分が黒く潰れないための環境光

    PSColorOut o;
    o.color = float4(col, 1.0);
    o.depth = depth;
    return o;
}

//-----------------------------------------------------------------------------
// PSDepth の出力: ビュー空間深度と深度バッファ
//-----------------------------------------------------------------------------
struct PSDepthOut
{
    float viewZ : SV_Target;   // ビュー空間の z (カメラからの距離に相当、正の値)
    float depth : SV_Depth;
};

//-----------------------------------------------------------------------------
// 関数名 : PSDepth
// 概要   : 表面生成用に球面のビュー空間 z を書き出す。
//          深度テストにより地形の後ろや他の球の後ろは自動的に隠れる。
// 引数   : i : ピクセル入力
// 戻り値 : ビュー空間 z と深度
//-----------------------------------------------------------------------------
PSDepthOut PSDepth(PSIn i)
{
    float3 n, posV; float depth;
    if (!SphereSurface(i, n, posV, depth))
        discard;

    PSDepthOut o;
    o.viewZ = posV.z;
    o.depth = depth;
    return o;
}
