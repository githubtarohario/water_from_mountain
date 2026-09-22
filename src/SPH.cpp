//=============================================================================
// SPH.cpp
//   SPH 流体シミュレーションの実装。
//=============================================================================
#include "SPH.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float PI = 3.14159265358979f;

    //-------------------------------------------------------------------------
    // 関数名 : XorShift32
    // 概要   : 軽量な擬似乱数 (xorshift)。0～1 の float を返す。
    // 引数   : state : 乱数の内部状態 (呼ぶたびに更新される)
    // 戻り値 : 0.0 以上 1.0 未満の乱数
    //-------------------------------------------------------------------------
    inline float XorShift32(uint32_t& state)
    {
        // xorshift: シフトと XOR だけで次の乱数を作る軽い方式。
        // 13, 17, 5 は周期が最大になる組み合わせとして知られている値。
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;                          // 次回のために状態を更新
        return (x & 0xFFFFFF) / 16777216.0f;   // 下位 24 ビットを取り出し 0.0～1.0 に収める
    }
}

//-----------------------------------------------------------------------------
// Initialize
//   カーネル係数 (Müller 2003):
//     W_poly6(r)  = 315/(64 π h^9) (h^2 - r^2)^3          (0 ≤ r ≤ h)
//     ∇W_spiky(r) = -45/(π h^6) (h - r)^2 * r̂             (0 <  r ≤ h)
//     ∇²W_visc(r) =  45/(π h^6) (h - r)                   (0 ≤ r ≤ h)
//-----------------------------------------------------------------------------
void SPH::Initialize(const Terrain* terrain, const SPHParams& params)
{
    m_terrain = terrain;                    // 衝突判定に使う地形 (所有はしない。寿命は呼び出し側が管理)
    m_params  = params;                     // 調整パラメータ一式をコピーして保持

    m_h  = params.smoothingRadius;          // 平滑化半径 h [m] : この距離より遠い粒子は無視する
    m_h2 = m_h * m_h;                       // h^2 : 距離判定で平方根を避けるため 2 乗で持つ
    // 粒子 1 個の質量 m = 静止密度 × (粒子が占める立方体の体積)。
    // こうしておくと、粒子が間隔 spacing で整然と並んだ静止状態で密度がほぼ ρ0 になる。
    m_mass = params.restDensity * params.particleSpacing * params.particleSpacing * params.particleSpacing;

    // カーネルの正規化係数を h から事前計算する (毎ステップ何百万回も使うのでここで 1 回だけ)
    const float h3 = m_h2 * m_h;            // h^3
    const float h6 = h3 * h3;               // h^6
    const float h9 = h6 * h3;               // h^9
    m_poly6Coef     = 315.0f / (64.0f * PI * h9);   // W_poly6  の係数 (密度の計算に使う)
    m_spikyGradCoef = 45.0f / (PI * h6);            // ∇W_spiky の係数 (圧力力の計算に使う)
    m_viscLapCoef   = 45.0f / (PI * h6);            // ∇²W_visc の係数 (粘性力の計算に使う)

    // 粒子ごとの配列を最大数ぶん一度に確保する (実行中に確保し直さないので速度が安定する)
    const size_t n = static_cast<size_t>(params.maxParticles);
    m_pos.resize(n);        // 位置 [m]
    m_vel.resize(n);        // 速度 [m/s]
    m_acc.resize(n);        // 加速度 [m/s^2]
    m_density.resize(n);    // 密度 ρ [kg/m^3]
    m_pressure.resize(n);   // 圧力 p
    m_cell.resize(n);       // 所属する格子セルの座標
    m_cellKey.resize(n);    // そのセルのハッシュ値
    m_sorted.resize(n);     // ハッシュ値順に並べ替えた粒子番号
    m_renderData.reserve(n);// 描画用の配列 (中身は毎フレーム作り直すので reserve だけ)

    // ハッシュ表は粒子数の 2 倍以上の 2 のべき乗にして衝突を減らし、剰余をビットマスクで済ませる
    m_tableSize = 1;
    while (m_tableSize < n * 2)
        m_tableSize <<= 1;                  // 粒子数の 2 倍以上になる最小の 2 のべき乗
    m_tableMask = m_tableSize - 1;          // 2 のべき乗 - 1 = 下位ビットがすべて 1 のマスク
    m_cellStart.resize(m_tableSize + 1);    // ハッシュ値ごとの開始位置 (末尾番兵のため +1 個)
    m_cellFill.resize(m_tableSize);         // 振り分け作業用のカウンタ

    m_neighbors.resize(n * static_cast<size_t>(params.maxNeighbors));  // 近傍番号 [粒子 × 最大近傍数]
    m_neighborCount.resize(n, 0);           // 粒子ごとの実際の近傍数
    m_speed.resize(n);                      // 速さ |v| (描画色の平滑化に使う作業用)

    Reset();                                // 粒子数・時間・乱数を初期状態にする
}

//-----------------------------------------------------------------------------
// Reset
//-----------------------------------------------------------------------------
void SPH::Reset()
{
    m_count = 0;
    m_time = 0.0f;
    m_emitTimer = 0.0f;
    m_rng = 12345u;
}

//-----------------------------------------------------------------------------
// Update
//   処理落ちで frameTime が大きくなったときにサブステップ数が増え続けて
//   さらに遅くなる (スパイラル) のを防ぐため、maxSubsteps で頭打ちにする。
//   その場合シミュレーションは実時間より遅く進む。
//-----------------------------------------------------------------------------
void SPH::Update(float frameTime)
{
    // 実時間に追いつくのに必要なサブステップ数を求め、上限で頭打ちにする
    int substeps = static_cast<int>(std::ceil(frameTime / m_params.timeStep));  // 例: 16.7ms / 3ms → 6 回
    substeps = std::clamp(substeps, 1, m_params.maxSubsteps);                   // 最低 1 回、最大 maxSubsteps 回
    for (int s = 0; s < substeps; ++s)
        Step();                             // 1 サブステップ (Δt 秒) ぶん物理を進める
}

//-----------------------------------------------------------------------------
// Step
//-----------------------------------------------------------------------------
void SPH::Step()
{
    const float dt = m_params.timeStep;     // このサブステップで進める時間 [s]

    Emit(dt);                               // 上流のエミッタから粒子を投入する (時間が来ていれば)
    if (m_count > 0)                        // 粒子が 1 個も無いときは以降の計算を省く
    {
        BuildGrid();                        // 粒子を格子セルに振り分ける (近傍探索の下ごしらえ)
        BuildNeighborLists();               // 各粒子の近傍 (距離 < h) の番号を記録する
        ComputeDensityPressure();           // 近傍から密度 ρ を求め、状態方程式で圧力 p を決める
        ComputeForces();                    // 圧力力 + 粘性力 + 重力 から加速度 a を求める
        Integrate();                        // a から速度と位置を更新し、地形との衝突を解く
        RemoveDrained();                    // 範囲外へ流れ出た粒子を配列から取り除く
    }
    m_time += dt;                           // シミュレーション内の経過時間を進める
}

//-----------------------------------------------------------------------------
// Emit
//   投入した板が前の板と重ならないように、
//   「板が 1 粒子間隔ぶん進む時間 = spacing / emitSpeed」ごとに 1 枚投入する。
//-----------------------------------------------------------------------------
void SPH::Emit(float dt)
{
    m_emitTimer += dt;                      // 前回投入からの経過時間を積算
    // 投入間隔 = 板が粒子 1 個分だけ進むのにかかる時間。これより短いと板同士が重なる
    const float interval = m_params.particleSpacing / m_params.emitSpeed;
    while (m_emitTimer >= interval)         // 1 サブステップで複数枚ぶん時間が経つこともある
    {
        m_emitTimer -= interval;
        EmitSheet();                        // 粒子の板を 1 枚置く
    }
}

//-----------------------------------------------------------------------------
// EmitSheet
//   谷の中心 x を中心に emitWidth 個を横並びにし、それを emitHeight 段積む。
//   各粒子には小さなランダムなずれ (ジッタ) を加えて、
//   完全な格子配置による不自然な規則性を崩す。
//-----------------------------------------------------------------------------
void SPH::EmitSheet()
{
    const float s  = m_params.particleSpacing;        // 粒子の間隔 [m] (板の中での格子の刻み)
    float cx, z;                                       // 投入位置: cx = 中心の x、z = 上流側の z
    m_terrain->GetEmitPoint(m_params.emitZ, cx, z);    // 地形に問い合わせる (設定値 or 谷底の自動検出)

    for (int iy = 0; iy < m_params.emitHeight; ++iy)   // iy: 下から何段目か
    {
        for (int ix = 0; ix < m_params.emitWidth; ++ix)// ix: 横方向に何番目か
        {
            if (m_count >= m_params.maxParticles)
                return;                                // 最大数に達したらこれ以上増やさない

            // 格子どおりに並べると規則的すぎるので、間隔の ±15% の範囲でランダムにずらす
            const float jitterX = (XorShift32(m_rng) - 0.5f) * s * 0.3f;   // 横方向のずれ [m]
            const float jitterY = (XorShift32(m_rng) - 0.5f) * s * 0.3f;   // 高さ方向のずれ [m]

            // 板の中心が cx に来るよう、(ix - (幅-1)/2) で -1.5, -0.5, +0.5, +1.5 … のように配る
            const float x = cx + (ix - (m_params.emitWidth - 1) * 0.5f) * s + jitterX;
            const float ground = m_terrain->GetHeight(x, z);   // その位置の地面の高さ [m]
            const float y = ground + m_params.emitLift + iy * s + jitterY;  // 地面から浮かせて段を積む

            const int i = m_count++;                   // 新しい粒子の番号 (配列の末尾に追加)
            m_pos[i] = XMFLOAT3(x, y, z);              // 初期位置
            m_vel[i] = XMFLOAT3(0.0f, 0.0f, m_params.emitSpeed);  // 初速は下流 (+z) 方向のみ
            m_acc[i] = XMFLOAT3(0.0f, 0.0f, 0.0f);     // 加速度は次のステップで計算されるので 0
            m_density[i]  = m_params.restDensity;      // 密度は静止密度から始める
            m_pressure[i] = 0.0f;                      // 圧力 0 (= 押し合っていない状態)
        }
    }
}

//-----------------------------------------------------------------------------
// CellCoord
//-----------------------------------------------------------------------------
XMINT3 SPH::CellCoord(const XMFLOAT3& p) const
{
    const float inv = 1.0f / m_h;           // 1/h : 割り算より掛け算のほうが速いので逆数を作る
    return XMINT3(                          // floor なので負の座標でも正しく切り下がる (-0.5/h → -1)
        static_cast<int>(std::floor(p.x * inv)),
        static_cast<int>(std::floor(p.y * inv)),
        static_cast<int>(std::floor(p.z * inv)));
}

//-----------------------------------------------------------------------------
// HashCell
//-----------------------------------------------------------------------------
uint32_t SPH::HashCell(int cx, int cy, int cz) const
{
    // 3 つの大きな素数を掛けて排他的論理和を取る (Teschner et al. 2003)。
    // 近いセル同士が違うハッシュ値になり、表の中に散らばるように選ばれた定数。
    const uint32_t h = (static_cast<uint32_t>(cx) * 73856093u)
                     ^ (static_cast<uint32_t>(cy) * 19349663u)
                     ^ (static_cast<uint32_t>(cz) * 83492791u);
    return h & m_tableMask;                 // 表のサイズが 2 のべき乗なので % の代わりにビットマスク
}

//-----------------------------------------------------------------------------
// BuildGrid
//   アルゴリズム (カウンティングソート):
//     1. 各粒子のセル座標とハッシュ値を計算            (並列)
//     2. ハッシュ値ごとの粒子数を数える                   count[key]++
//     3. 累積和を取り、各ハッシュ値の開始位置にする       cellStart[key] = Σ count[0..key-1]
//     4. 粒子番号を開始位置から順に書き込む               sorted[cellStart[key] + fill[key]++] = i
//-----------------------------------------------------------------------------
void SPH::BuildGrid()
{
    const int n = m_count;                  // 有効な粒子数

    // 1. セル座標とハッシュ値 (粒子ごとに独立なので並列化できる)
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        m_cell[i] = CellCoord(m_pos[i]);                                   // 位置 → 格子セルの整数座標
        m_cellKey[i] = HashCell(m_cell[i].x, m_cell[i].y, m_cell[i].z);    // セル座標 → 表の添え字
    }

    // 2. ハッシュ値ごとの粒子数を数える (同じ添え字に複数スレッドが書くため、ここは逐次で行う)
    std::fill(m_cellStart.begin(), m_cellStart.end(), 0u);   // カウンタを 0 に戻す
    for (int i = 0; i < n; ++i)
        ++m_cellStart[m_cellKey[i] + 1];   // +1 にしておくと次の累積和でそのまま開始位置になる

    // 3. 累積和を取ると、cellStart[k] が「ハッシュ値 k の粒子が sorted[] のどこから始まるか」になる
    for (uint32_t k = 0; k < m_tableSize; ++k)
        m_cellStart[k + 1] += m_cellStart[k];

    // 4. 粒子番号を開始位置から順に書き込む (cellFill は「そのセルに何個書いたか」)
    std::fill(m_cellFill.begin(), m_cellFill.end(), 0u);
    for (int i = 0; i < n; ++i)
    {
        const uint32_t key = m_cellKey[i];                      // この粒子のセルのハッシュ値
        m_sorted[m_cellStart[key] + m_cellFill[key]++] = i;     // 開始位置 + 書いた個数 の場所へ
    }
}

//-----------------------------------------------------------------------------
// ForEachNeighbor
//-----------------------------------------------------------------------------
template <class F>
void SPH::ForEachNeighbor(int i, F&& func) const
{
    const XMFLOAT3 pi = m_pos[i];           // 注目している粒子の位置
    const XMINT3   ci = m_cell[i];           // その粒子が入っているセルの座標

    // セルの一辺は h なので、半径 h の球は「自分のセル + 隣接 26 セル」に必ず収まる
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int cx = ci.x + dx;    // いま調べているセルの座標
                const int cy = ci.y + dy;
                const int cz = ci.z + dz;
                const uint32_t key = HashCell(cx, cy, cz);      // そのセルの表の添え字

                const uint32_t begin = m_cellStart[key];        // このセルの粒子は sorted[begin..end) に並ぶ
                const uint32_t end   = m_cellStart[key + 1];
                for (uint32_t k = begin; k < end; ++k)
                {
                    const int j = m_sorted[k];                  // 候補となる相手の粒子番号

                    // ハッシュ衝突対策: 別のセルが同じ添え字になることがあるので座標を確認する
                    // (これを省くと、遠くの粒子を近傍として二重に数えてしまう)
                    const XMINT3& cj = m_cell[j];
                    if (cj.x != cx || cj.y != cy || cj.z != cz)
                        continue;

                    const float rx = pi.x - m_pos[j].x;         // i から見た j への相対位置 (x_i - x_j)
                    const float ry = pi.y - m_pos[j].y;
                    const float rz = pi.z - m_pos[j].z;
                    const float r2 = rx * rx + ry * ry + rz * rz;   // 距離の 2 乗 (平方根は使わない)
                    if (r2 < m_h2)
                        func(j, rx, ry, rz, r2);                // 近傍が見つかったので呼び出し側へ渡す
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// BuildNeighborLists
//   近傍数が maxNeighbors を超えた分は捨てる (極端に圧縮された場合のみ起こる)。
//-----------------------------------------------------------------------------
void SPH::BuildNeighborLists()
{
    const int n = m_count;
    const int maxN = m_params.maxNeighbors; // 1 粒子あたり記録できる近傍数の上限

    // schedule(dynamic, 64): 粒子ごとに近傍数が違い処理時間が不揃いなので、
    // 64 個ずつの塊を空いたスレッドから取っていく方式にすると効率がよい
#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        int* list = &m_neighbors[static_cast<size_t>(i) * maxN];   // この粒子ぶんの記録領域の先頭
        int count = 0;                                             // 見つかった近傍の数
        ForEachNeighbor(i, [&](int j, float, float, float, float)  // 27 セルを走査し近傍ごとに呼ばれる
        {
            if (count < maxN)
                list[count++] = j;          // 上限を超えた分は捨てる (極端に密集したときのみ)
        });
        m_neighborCount[i] = count;
    }
}

//-----------------------------------------------------------------------------
// ComputeDensityPressure
//   ρ_i = Σ_j m W_poly6(r_ij)   (j = i 自身も含む)
//   p_i = k * max(ρ_i - ρ0, 0)
//     負の圧力 (引力) を許すと粒子が塊になって不自然になるため 0 で打ち切る。
//-----------------------------------------------------------------------------
void SPH::ComputeDensityPressure()
{
    // ループの中で毎回メンバ変数を読むと遅いので、先にローカル変数へ写しておく
    const int n = m_count;
    const int maxN = m_params.maxNeighbors;   // 近傍リストの 1 粒子あたりの長さ
    const float mass = m_mass;                // 粒子 1 個の質量 m [kg]
    const float poly6 = m_poly6Coef;          // W_poly6 の係数 315/(64πh^9)
    const float h2 = m_h2;                    // h^2
    const float k = m_params.stiffness;       // 圧力の硬さ k (大きいほど縮みにくい)
    const float rho0 = m_params.restDensity;  // 静止密度 ρ0 [kg/m^3]

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        const XMFLOAT3 pi = m_pos[i];                                  // 自分の位置
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN];  // 自分の近傍リスト
        const int count = m_neighborCount[i];                           // 近傍の数

        float rho = 0.0f;                       // 密度の累積 (自分自身の寄与も含む)
        for (int k2 = 0; k2 < count; ++k2)
        {
            const XMFLOAT3& pj = m_pos[list[k2]];                       // 近傍粒子の位置
            const float rx = pi.x - pj.x, ry = pi.y - pj.y, rz = pi.z - pj.z;
            const float r2 = rx * rx + ry * ry + rz * rz;               // 距離の 2 乗
            const float d = h2 - r2;   // 近傍リストは r2 < h2 を保証しているので d > 0
            rho += mass * poly6 * d * d * d;    // ρ += m * 315/(64πh^9) * (h²-r²)³
        }
        m_density[i]  = rho;
        // 状態方程式: 静止密度より詰まっているぶんだけ圧力が生じる。
        // max(…, 0) で負圧 (引力) を打ち切らないと、自由表面で粒子が塊になってしまう。
        m_pressure[i] = k * std::max(rho - rho0, 0.0f);
    }
}

//-----------------------------------------------------------------------------
// ComputeForces
//   圧力力: f_p = Σ_j m (p_i + p_j)/(2 ρ_j) * 45/(π h^6) (h - r)^2 * r̂_ij
//           (r̂_ij = (x_i - x_j)/r。粒子 j から離れる向きに押される)
//   粘性力: f_v = μ Σ_j m (v_j - v_i)/ρ_j * 45/(π h^6) (h - r)
//   加速度: a_i = (f_p + f_v)/ρ_i + g
//-----------------------------------------------------------------------------
void SPH::ComputeForces()
{
    const int n = m_count;
    const float mass = m_mass;                // 粒子 1 個の質量 m
    const float h = m_h;                      // 平滑化半径 h
    const float spikyCoef = m_spikyGradCoef;  // ∇W_spiky の係数 45/(πh^6)
    const float viscCoef = m_viscLapCoef;     // ∇²W_visc の係数 45/(πh^6)
    const float mu = m_params.viscosity;      // 粘性係数 μ (大きいほど粘る)
    const float g = m_params.gravity;         // 重力加速度 [m/s^2]

    const int maxN = m_params.maxNeighbors;

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        const float pi = m_pressure[i];                                // 自分の圧力
        const XMFLOAT3 xi = m_pos[i];                                  // 自分の位置
        const XMFLOAT3 vi = m_vel[i];                                  // 自分の速度
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN]; // 近傍リスト
        const int count = m_neighborCount[i];
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;  // 力の累積 [N] (圧力力 + 粘性力)

        for (int k2 = 0; k2 < count; ++k2)
        {
            const int j = list[k2];                                    // 相手の粒子番号
            const XMFLOAT3& xj = m_pos[j];
            const float rx = xi.x - xj.x, ry = xi.y - xj.y, rz = xi.z - xj.z;  // x_i - x_j
            const float r2 = rx * rx + ry * ry + rz * rz;
            if (j == i || r2 < 1e-12f)
                continue;   // 自分自身、または完全に重なった粒子は除外 (0 除算防止)

            const float r = std::sqrt(r2);      // 距離 r (ここでは方向が必要なので平方根を取る)
            const float invR = 1.0f / r;        // 1/r : (rx,ry,rz) を単位ベクトルに直すのに使う
            const float hr = h - r;             // h - r : カーネルの効き具合 (r = h で 0)
            const float rhoJ = m_density[j];    // 相手の密度
            const float invRhoJ = 1.0f / rhoJ;

            // 圧力力: 2 粒子の圧力の平均に比例し、相手から離れる向きに押される (斥力)。
            // pTerm に (rx,ry,rz) を掛けることで「大きさ × 単位ベクトル」になる。
            const float pTerm = mass * (pi + m_pressure[j]) * 0.5f * invRhoJ * spikyCoef * hr * hr * invR;
            fx += pTerm * rx;
            fy += pTerm * ry;
            fz += pTerm * rz;

            // 粘性力: 相手との速度差に比例し、速度差を減らす向きにはたらく (流れをまとめる)
            const float vTerm = mu * mass * invRhoJ * viscCoef * hr;
            const XMFLOAT3& vj = m_vel[j];      // 相手の速度
            fx += vTerm * (vj.x - vi.x);
            fy += vTerm * (vj.y - vi.y);
            fz += vTerm * (vj.z - vi.z);
        }

        const float invRhoI = 1.0f / m_density[i];                      // 1/ρ_i
        float ax = fx * invRhoI, ay = fy * invRhoI, az = fz * invRhoI;  // a = f / ρ

        // 圧力・粘性による加速度の大きさを上限で頭打ちにする
        // (壁際で密度が急上昇したときに粒子が弾き飛ばされるのを防ぐ。重力は別に足す)
        const float amax = m_params.maxAcceleration;    // 加速度の上限 [m/s^2]
        const float a2 = ax * ax + ay * ay + az * az;   // 加速度の大きさの 2 乗
        if (a2 > amax * amax)
        {
            const float scale = amax / std::sqrt(a2);   // 向きは変えず、長さだけ amax に縮める
            ax *= scale; ay *= scale; az *= scale;
        }
        m_acc[i] = XMFLOAT3(ax, ay - g, az);            // 最後に重力 (下向き) を加える
    }
}

//-----------------------------------------------------------------------------
// Integrate
//-----------------------------------------------------------------------------
void SPH::Integrate()
{
    const int n = m_count;
    const float dt = m_params.timeStep;     // 1 サブステップの時間 [s]
    const float vmax = m_params.maxSpeed;   // 速度の上限 [m/s] (発散を防ぐ安全弁)

#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        XMFLOAT3& v = m_vel[i];             // 速度 (参照なので書き換えると配列に反映される)
        XMFLOAT3& p = m_pos[i];             // 位置 (同上)
        const XMFLOAT3& a = m_acc[i];       // ComputeForces が求めた加速度

        // 速度更新 (シンプレクティック・オイラー法: 速度を先に更新するのがポイント)
        v.x += a.x * dt;
        v.y += a.y * dt;
        v.z += a.z * dt;

        // 速度の上限 (発散の安全弁)。2 乗同士で比べて、必要なときだけ平方根を計算する
        const float speed2 = v.x * v.x + v.y * v.y + v.z * v.z;   // 速さの 2 乗 |v|^2
        if (speed2 > vmax * vmax)
        {
            const float scale = vmax / std::sqrt(speed2);         // 向きは保ったまま長さを vmax に
            v.x *= scale; v.y *= scale; v.z *= scale;
        }

        // 位置更新 (更新後の速度を使うのがシンプレクティック・オイラー法)
        p.x += v.x * dt;
        p.y += v.y * dt;
        p.z += v.z * dt;

        ResolveCollision(i);                // 地形・壁との衝突と、飛び出し防止の減衰を適用
    }
}

//-----------------------------------------------------------------------------
// ResolveCollision
//   アルゴリズム:
//     1. 上流の壁 (z = -limit) を越えたら壁面に戻し、壁に向かう速度成分を 0 にする
//        (左右と下流は壁ではなく排水口: RemoveDrained で消去)
//     2. 地形: 粒子の中心が「地面の高さ + 粒子半径」より低ければ
//        その高さまで持ち上げ、地面法線 n に対して
//          v ← v - (1 + e)(v・n) n   (v・n < 0 のときのみ)
//        で地面にめり込む速度成分を除去する (e = 反発係数)。
//        さらに接地中はわずかな摩擦として速度全体を (1 - friction) 倍する。
//     3. 接触ゾーン: 地面から contactZone 以内にある粒子は、地面に近いほど強く
//        減衰させる (最下部で contactDamping)。これにより壁に当たった粒子が
//        跳ね上がって谷の外へ飛び出すのを抑える。
//     4. 空気抵抗: 近傍粒子が sparseNeighbors 未満の孤立粒子 (飛沫) には
//        airDrag の減衰を掛け、勢いを失ってすぐ流れに戻るようにする。
//-----------------------------------------------------------------------------
void SPH::ResolveCollision(int i)
{
    XMFLOAT3& p = m_pos[i];                                 // 位置 (書き換える)
    XMFLOAT3& v = m_vel[i];                                 // 速度 (書き換える)
    const float radius = m_params.particleSpacing * 0.5f;   // 粒子の半径 [m] (地面へのめり込み判定に使う)
    const float limit  = Terrain::HALF_SIZE - 0.5f;         // 地形の端 (±30 m) から少し内側 [m]

    // 1. 上流の壁 (z = -limit) のみ壁にする。左右 (x) と下流 (z = +limit) は
    //    RemoveDrained で「範囲外に流れ出た粒子」として消去する (実在地形では
    //    どの辺からも水が流れ出るため)
    if (p.z < -limit) { p.z = -limit; if (v.z < 0.0f) v.z = 0.0f; }

    // 2. 地形との衝突
    // 粒子の「中心」が地面 + 半径より低ければ、球が地面にめり込んでいる
    const float floorY = m_terrain->GetHeight(p.x, p.z) + radius;   // この高さより下は地面の中
    if (p.y < floorY)
    {
        p.y = floorY;                                       // 地面の上へ押し戻す
        const XMFLOAT3 n = m_terrain->GetNormal(p.x, p.z);  // その地点の地面の法線 (上向き単位ベクトル)
        const float vn = v.x * n.x + v.y * n.y + v.z * n.z; // 速度の法線方向成分 v・n (負 = 地面へ向かう)
        if (vn < 0.0f)
        {
            // v から法線方向の成分を取り除くと、地面に沿って滑る成分だけが残る。
            // 係数 (1+e) は反発係数 e = 0 なら 1 倍 (跳ね返らない)、e = 1 なら 2 倍 (完全弾性)。
            const float s = (1.0f + m_params.restitution) * vn;
            v.x -= s * n.x;
            v.y -= s * n.y;
            v.z -= s * n.z;
        }
        const float damp = 1.0f - m_params.friction;        // 接地中の摩擦 (1 ステップあたりの減衰率)
        v.x *= damp; v.y *= damp; v.z *= damp;
    }

    // 3. 接触ゾーン: 地面からの高さ (0 ～ contactZone) に応じて減衰を強める
    //    t = 1 (地面に接している) → contactDamping、t = 0 (ゾーン上端) → 0
    const float height = p.y - floorY;                      // 地面からの高さ [m] (0 なら接地)
    if (height < m_params.contactZone)
    {
        const float t = 1.0f - std::max(height, 0.0f) / m_params.contactZone;  // 地面で 1、ゾーン上端で 0
        const float damp = 1.0f - m_params.contactDamping * t;                 // 地面に近いほど強く減衰
        v.x *= damp; v.y *= damp; v.z *= damp;
    }

    // 4. 空気抵抗: 近傍が少ない = 流れから離れて飛び散った粒子なので、勢いを削いで流れに戻す
    if (m_neighborCount[i] < m_params.sparseNeighbors)
    {
        const float damp = 1.0f - m_params.airDrag;
        v.x *= damp; v.y *= damp; v.z *= damp;
    }
}

//-----------------------------------------------------------------------------
// RemoveDrained
//   下流端 (z > limit) または左右端 (|x| > limit) から流れ出た粒子を消す。
//   有効粒子は配列の先頭に詰めて管理しているので、
//   消す粒子を末尾の粒子で上書きし m_count を 1 減らす (順序は保たれなくてよい)。
//-----------------------------------------------------------------------------
void SPH::RemoveDrained()
{
    const float drainZ = Terrain::HALF_SIZE - 0.5f;   // 地形の端の手前 [m] (ここを越えたら流出とみなす)
    int i = 0;
    while (i < m_count)
    {
        const XMFLOAT3& p = m_pos[i];
        // 下流端・左右端を越えた粒子、または何らかの異常で地形のはるか下へ落ちた粒子を消す
        const bool drained = (p.z > drainZ) || (p.x < -drainZ) || (p.x > drainZ) || (p.y < -60.0f);
        if (drained)
        {
            // 配列の途中を詰め直すと O(N) かかるので、末尾の粒子を持ってきて上書きする (O(1))。
            // 粒子の並び順に意味はないので、順序が変わっても問題ない。
            const int last = m_count - 1;
            m_pos[i] = m_pos[last];
            m_vel[i] = m_vel[last];
            m_acc[i] = m_acc[last];
            m_density[i]  = m_density[last];
            m_pressure[i] = m_pressure[last];
            --m_count;
            // i は進めない (末尾から持ってきた粒子もこの位置で判定する必要があるため)
        }
        else
        {
            ++i;
        }
    }
}

//-----------------------------------------------------------------------------
// GetRenderData
//   アルゴリズム:
//     1. 各粒子の速さ |v_i| を計算
//     2. 直前のステップで作った近傍リストを使い、自分と近傍の速さの単純平均を取る
//        (近傍リストは RemoveDrained で粒子が入れ替わると一部ずれるが、
//         描画の色にしか使わないので許容する。範囲外の番号だけは除外する)
//-----------------------------------------------------------------------------
const std::vector<XMFLOAT4>& SPH::GetRenderData()
{
    const int n = m_count;
    const int maxN = m_params.maxNeighbors;
    m_renderData.resize(n);                 // 転送するのは有効な粒子ぶんだけ

    // 1. 各粒子の速さ |v| を求める
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        const XMFLOAT3& v = m_vel[i];
        m_speed[i] = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    // 2. 近傍との平均を取って速さを滑らかにする。
    //    SPH の速度は粒子ごとに細かく揺らぐため、そのまま色にすると斑模様になってしまう。
    //    物理計算には使わない、見た目だけのための処理。
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN];
        const int count = m_neighborCount[i];
        float sum = m_speed[i];             // 自分自身の速さから始める
        int   num = 1;                      // 平均を取る個数
        for (int k = 0; k < count; ++k)
        {
            const int j = list[k];
            // 近傍リストは RemoveDrained より前に作られているので、消えた粒子の番号が
            // 残っていることがある。範囲内かどうかを確かめてから使う。
            if (j >= 0 && j < n)
            {
                sum += m_speed[j];
                ++num;
            }
        }
        const XMFLOAT3& p = m_pos[i];
        m_renderData[i] = XMFLOAT4(p.x, p.y, p.z, sum / num);   // w 成分に平滑化した速さを入れる
    }
    return m_renderData;
}
