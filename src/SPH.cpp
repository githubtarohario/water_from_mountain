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
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return (x & 0xFFFFFF) / 16777216.0f;
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
    m_terrain = terrain;
    m_params  = params;

    m_h  = params.smoothingRadius;
    m_h2 = m_h * m_h;
    m_mass = params.restDensity * params.particleSpacing * params.particleSpacing * params.particleSpacing;

    const float h3 = m_h2 * m_h;
    const float h6 = h3 * h3;
    const float h9 = h6 * h3;
    m_poly6Coef     = 315.0f / (64.0f * PI * h9);
    m_spikyGradCoef = 45.0f / (PI * h6);
    m_viscLapCoef   = 45.0f / (PI * h6);

    const size_t n = static_cast<size_t>(params.maxParticles);
    m_pos.resize(n);
    m_vel.resize(n);
    m_acc.resize(n);
    m_density.resize(n);
    m_pressure.resize(n);
    m_cell.resize(n);
    m_cellKey.resize(n);
    m_sorted.resize(n);
    m_renderData.reserve(n);

    // ハッシュ表は粒子数の 2 倍以上の 2 のべき乗にして衝突を減らし、剰余をビットマスクで済ませる
    m_tableSize = 1;
    while (m_tableSize < n * 2)
        m_tableSize <<= 1;
    m_tableMask = m_tableSize - 1;
    m_cellStart.resize(m_tableSize + 1);
    m_cellFill.resize(m_tableSize);

    m_neighbors.resize(n * static_cast<size_t>(params.maxNeighbors));
    m_neighborCount.resize(n, 0);
    m_speed.resize(n);

    Reset();
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
    int substeps = static_cast<int>(std::ceil(frameTime / m_params.timeStep));
    substeps = std::clamp(substeps, 1, m_params.maxSubsteps);
    for (int s = 0; s < substeps; ++s)
        Step();
}

//-----------------------------------------------------------------------------
// Step
//-----------------------------------------------------------------------------
void SPH::Step()
{
    const float dt = m_params.timeStep;

    Emit(dt);
    if (m_count > 0)
    {
        BuildGrid();
        BuildNeighborLists();
        ComputeDensityPressure();
        ComputeForces();
        Integrate();
        RemoveDrained();
    }
    m_time += dt;
}

//-----------------------------------------------------------------------------
// Emit
//   投入した板が前の板と重ならないように、
//   「板が 1 粒子間隔ぶん進む時間 = spacing / emitSpeed」ごとに 1 枚投入する。
//-----------------------------------------------------------------------------
void SPH::Emit(float dt)
{
    m_emitTimer += dt;
    const float interval = m_params.particleSpacing / m_params.emitSpeed;
    while (m_emitTimer >= interval)
    {
        m_emitTimer -= interval;
        EmitSheet();
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
    const float s  = m_params.particleSpacing;
    const float z  = m_params.emitZ;
    const float cx = Terrain::ValleyCenterX(z);

    for (int iy = 0; iy < m_params.emitHeight; ++iy)
    {
        for (int ix = 0; ix < m_params.emitWidth; ++ix)
        {
            if (m_count >= m_params.maxParticles)
                return;

            const float jitterX = (XorShift32(m_rng) - 0.5f) * s * 0.3f;
            const float jitterY = (XorShift32(m_rng) - 0.5f) * s * 0.3f;

            const float x = cx + (ix - (m_params.emitWidth - 1) * 0.5f) * s + jitterX;
            const float ground = m_terrain->GetHeight(x, z);
            const float y = ground + m_params.emitLift + iy * s + jitterY;

            const int i = m_count++;
            m_pos[i] = XMFLOAT3(x, y, z);
            m_vel[i] = XMFLOAT3(0.0f, 0.0f, m_params.emitSpeed);
            m_acc[i] = XMFLOAT3(0.0f, 0.0f, 0.0f);
            m_density[i]  = m_params.restDensity;
            m_pressure[i] = 0.0f;
        }
    }
}

//-----------------------------------------------------------------------------
// CellCoord
//-----------------------------------------------------------------------------
XMINT3 SPH::CellCoord(const XMFLOAT3& p) const
{
    const float inv = 1.0f / m_h;
    return XMINT3(
        static_cast<int>(std::floor(p.x * inv)),
        static_cast<int>(std::floor(p.y * inv)),
        static_cast<int>(std::floor(p.z * inv)));
}

//-----------------------------------------------------------------------------
// HashCell
//-----------------------------------------------------------------------------
uint32_t SPH::HashCell(int cx, int cy, int cz) const
{
    const uint32_t h = (static_cast<uint32_t>(cx) * 73856093u)
                     ^ (static_cast<uint32_t>(cy) * 19349663u)
                     ^ (static_cast<uint32_t>(cz) * 83492791u);
    return h & m_tableMask;
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
    const int n = m_count;

    // 1. セル座標とハッシュ値
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        m_cell[i] = CellCoord(m_pos[i]);
        m_cellKey[i] = HashCell(m_cell[i].x, m_cell[i].y, m_cell[i].z);
    }

    // 2. ハッシュ値ごとの粒子数
    std::fill(m_cellStart.begin(), m_cellStart.end(), 0u);
    for (int i = 0; i < n; ++i)
        ++m_cellStart[m_cellKey[i] + 1];   // +1 にしておくと次の累積和でそのまま開始位置になる

    // 3. 累積和 → 開始位置
    for (uint32_t k = 0; k < m_tableSize; ++k)
        m_cellStart[k + 1] += m_cellStart[k];

    // 4. 振り分け
    std::fill(m_cellFill.begin(), m_cellFill.end(), 0u);
    for (int i = 0; i < n; ++i)
    {
        const uint32_t key = m_cellKey[i];
        m_sorted[m_cellStart[key] + m_cellFill[key]++] = i;
    }
}

//-----------------------------------------------------------------------------
// ForEachNeighbor
//-----------------------------------------------------------------------------
template <class F>
void SPH::ForEachNeighbor(int i, F&& func) const
{
    const XMFLOAT3 pi = m_pos[i];
    const XMINT3   ci = m_cell[i];

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int cx = ci.x + dx;
                const int cy = ci.y + dy;
                const int cz = ci.z + dz;
                const uint32_t key = HashCell(cx, cy, cz);

                const uint32_t begin = m_cellStart[key];
                const uint32_t end   = m_cellStart[key + 1];
                for (uint32_t k = begin; k < end; ++k)
                {
                    const int j = m_sorted[k];

                    // ハッシュ衝突対策: 本当に同じセルの粒子かを確認
                    const XMINT3& cj = m_cell[j];
                    if (cj.x != cx || cj.y != cy || cj.z != cz)
                        continue;

                    const float rx = pi.x - m_pos[j].x;
                    const float ry = pi.y - m_pos[j].y;
                    const float rz = pi.z - m_pos[j].z;
                    const float r2 = rx * rx + ry * ry + rz * rz;
                    if (r2 < m_h2)
                        func(j, rx, ry, rz, r2);
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
    const int maxN = m_params.maxNeighbors;

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        int* list = &m_neighbors[static_cast<size_t>(i) * maxN];
        int count = 0;
        ForEachNeighbor(i, [&](int j, float, float, float, float)
        {
            if (count < maxN)
                list[count++] = j;
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
    const int n = m_count;
    const int maxN = m_params.maxNeighbors;
    const float mass = m_mass;
    const float poly6 = m_poly6Coef;
    const float h2 = m_h2;
    const float k = m_params.stiffness;
    const float rho0 = m_params.restDensity;

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        const XMFLOAT3 pi = m_pos[i];
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN];
        const int count = m_neighborCount[i];

        float rho = 0.0f;
        for (int k2 = 0; k2 < count; ++k2)
        {
            const XMFLOAT3& pj = m_pos[list[k2]];
            const float rx = pi.x - pj.x, ry = pi.y - pj.y, rz = pi.z - pj.z;
            const float r2 = rx * rx + ry * ry + rz * rz;
            const float d = h2 - r2;   // 近傍リストは r2 < h2 を保証しているので d > 0
            rho += mass * poly6 * d * d * d;
        }
        m_density[i]  = rho;
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
    const float mass = m_mass;
    const float h = m_h;
    const float spikyCoef = m_spikyGradCoef;
    const float viscCoef = m_viscLapCoef;
    const float mu = m_params.viscosity;
    const float g = m_params.gravity;

    const int maxN = m_params.maxNeighbors;

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        const float pi = m_pressure[i];
        const XMFLOAT3 xi = m_pos[i];
        const XMFLOAT3 vi = m_vel[i];
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN];
        const int count = m_neighborCount[i];
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;

        for (int k2 = 0; k2 < count; ++k2)
        {
            const int j = list[k2];
            const XMFLOAT3& xj = m_pos[j];
            const float rx = xi.x - xj.x, ry = xi.y - xj.y, rz = xi.z - xj.z;
            const float r2 = rx * rx + ry * ry + rz * rz;
            if (j == i || r2 < 1e-12f)
                continue;   // 自分自身、または完全に重なった粒子は除外 (0 除算防止)

            const float r = std::sqrt(r2);
            const float invR = 1.0f / r;
            const float hr = h - r;
            const float rhoJ = m_density[j];
            const float invRhoJ = 1.0f / rhoJ;

            // 圧力力 (斥力)
            const float pTerm = mass * (pi + m_pressure[j]) * 0.5f * invRhoJ * spikyCoef * hr * hr * invR;
            fx += pTerm * rx;
            fy += pTerm * ry;
            fz += pTerm * rz;

            // 粘性力 (周囲との速度差を減らす)
            const float vTerm = mu * mass * invRhoJ * viscCoef * hr;
            const XMFLOAT3& vj = m_vel[j];
            fx += vTerm * (vj.x - vi.x);
            fy += vTerm * (vj.y - vi.y);
            fz += vTerm * (vj.z - vi.z);
        }

        const float invRhoI = 1.0f / m_density[i];
        float ax = fx * invRhoI, ay = fy * invRhoI, az = fz * invRhoI;

        // 圧力・粘性による加速度の大きさを上限で頭打ちにする
        // (壁際で密度が急上昇したときに粒子が弾き飛ばされるのを防ぐ。重力は別に足す)
        const float amax = m_params.maxAcceleration;
        const float a2 = ax * ax + ay * ay + az * az;
        if (a2 > amax * amax)
        {
            const float scale = amax / std::sqrt(a2);
            ax *= scale; ay *= scale; az *= scale;
        }
        m_acc[i] = XMFLOAT3(ax, ay - g, az);
    }
}

//-----------------------------------------------------------------------------
// Integrate
//-----------------------------------------------------------------------------
void SPH::Integrate()
{
    const int n = m_count;
    const float dt = m_params.timeStep;
    const float vmax = m_params.maxSpeed;

#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        XMFLOAT3& v = m_vel[i];
        XMFLOAT3& p = m_pos[i];
        const XMFLOAT3& a = m_acc[i];

        // 速度更新
        v.x += a.x * dt;
        v.y += a.y * dt;
        v.z += a.z * dt;

        // 速度の上限 (発散の安全弁)
        const float speed2 = v.x * v.x + v.y * v.y + v.z * v.z;
        if (speed2 > vmax * vmax)
        {
            const float scale = vmax / std::sqrt(speed2);
            v.x *= scale; v.y *= scale; v.z *= scale;
        }

        // 位置更新
        p.x += v.x * dt;
        p.y += v.y * dt;
        p.z += v.z * dt;

        ResolveCollision(i);
    }
}

//-----------------------------------------------------------------------------
// ResolveCollision
//   アルゴリズム:
//     1. 側壁 (x = ±limit) と上流の壁 (z = -limit) を越えたら壁面に戻し、
//        壁に向かう速度成分を 0 にする
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
    XMFLOAT3& p = m_pos[i];
    XMFLOAT3& v = m_vel[i];
    const float radius = m_params.particleSpacing * 0.5f;
    const float limit  = Terrain::HALF_SIZE - 0.5f;

    // 1. 側壁・上流の壁
    if (p.x < -limit) { p.x = -limit; if (v.x < 0.0f) v.x = 0.0f; }
    if (p.x >  limit) { p.x =  limit; if (v.x > 0.0f) v.x = 0.0f; }
    if (p.z < -limit) { p.z = -limit; if (v.z < 0.0f) v.z = 0.0f; }

    // 2. 地形
    const float floorY = m_terrain->GetHeight(p.x, p.z) + radius;
    if (p.y < floorY)
    {
        p.y = floorY;
        const XMFLOAT3 n = m_terrain->GetNormal(p.x, p.z);
        const float vn = v.x * n.x + v.y * n.y + v.z * n.z;
        if (vn < 0.0f)
        {
            const float s = (1.0f + m_params.restitution) * vn;
            v.x -= s * n.x;
            v.y -= s * n.y;
            v.z -= s * n.z;
        }
        const float damp = 1.0f - m_params.friction;
        v.x *= damp; v.y *= damp; v.z *= damp;
    }

    // 3. 接触ゾーン: 地面からの高さ (0 ～ contactZone) に応じて減衰を強める
    //    t = 1 (地面に接している) → contactDamping、t = 0 (ゾーン上端) → 0
    const float height = p.y - floorY;
    if (height < m_params.contactZone)
    {
        const float t = 1.0f - std::max(height, 0.0f) / m_params.contactZone;
        const float damp = 1.0f - m_params.contactDamping * t;
        v.x *= damp; v.y *= damp; v.z *= damp;
    }

    // 4. 空気抵抗: 孤立した粒子 (飛沫) の勢いを削ぐ
    if (m_neighborCount[i] < m_params.sparseNeighbors)
    {
        const float damp = 1.0f - m_params.airDrag;
        v.x *= damp; v.y *= damp; v.z *= damp;
    }
}

//-----------------------------------------------------------------------------
// RemoveDrained
//   有効粒子は配列の先頭に詰めて管理しているので、
//   消す粒子を末尾の粒子で上書きし m_count を 1 減らす (順序は保たれなくてよい)。
//-----------------------------------------------------------------------------
void SPH::RemoveDrained()
{
    const float drainZ = Terrain::HALF_SIZE - 0.5f;
    int i = 0;
    while (i < m_count)
    {
        const XMFLOAT3& p = m_pos[i];
        const bool drained = (p.z > drainZ) || (p.y < -60.0f);
        if (drained)
        {
            const int last = m_count - 1;
            m_pos[i] = m_pos[last];
            m_vel[i] = m_vel[last];
            m_acc[i] = m_acc[last];
            m_density[i]  = m_density[last];
            m_pressure[i] = m_pressure[last];
            --m_count;
            // i は進めない (入れ替えた粒子を再チェック)
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
    m_renderData.resize(n);

    // 1. 速さ
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        const XMFLOAT3& v = m_vel[i];
        m_speed[i] = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    // 2. 近傍平均
#pragma omp parallel for
    for (int i = 0; i < n; ++i)
    {
        const int* list = &m_neighbors[static_cast<size_t>(i) * maxN];
        const int count = m_neighborCount[i];
        float sum = m_speed[i];
        int   num = 1;
        for (int k = 0; k < count; ++k)
        {
            const int j = list[k];
            if (j >= 0 && j < n)
            {
                sum += m_speed[j];
                ++num;
            }
        }
        const XMFLOAT3& p = m_pos[i];
        m_renderData[i] = XMFLOAT4(p.x, p.y, p.z, sum / num);
    }
    return m_renderData;
}
