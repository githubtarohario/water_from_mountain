//=============================================================================
// Camera.cpp
//   オービットカメラの実装。
//=============================================================================
#include "Camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

//-----------------------------------------------------------------------------
// コンストラクタ
//   初期状態: 谷の下流側 (+z 側) から上流 (-z 側) を見下ろす構図。
//   参考動画と同じく、水が奥から手前へ流れてくるように見える。
//-----------------------------------------------------------------------------
Camera::Camera()
    : m_target(0.0f, 1.0f, -2.0f)
    , m_yaw(0.0f)
    , m_pitch(XMConvertToRadians(32.0f))
    , m_distance(33.0f)
    , m_fovY(XMConvertToRadians(45.0f))
    , m_nearZ(0.5f)
    , m_farZ(400.0f)
{
}

//-----------------------------------------------------------------------------
// Rotate
//   pitch は真上 / 真下を超えないように ±89 度でクランプする
//   (真上を超えると LookAt の up ベクトルと視線が平行になり行列が壊れるため)
//-----------------------------------------------------------------------------
void Camera::Rotate(float dYaw, float dPitch)
{
    m_yaw   += dYaw;                        // 水平方向はぐるぐる回せるので制限しない
    m_pitch += dPitch;
    // 真上 (90 度) まで行くと視線と上方向ベクトルが平行になり、ビュー行列が作れなくなる。
    // そのため 89 度でとどめておく。
    const float limit = XMConvertToRadians(89.0f);
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

//-----------------------------------------------------------------------------
// Zoom
//-----------------------------------------------------------------------------
void Camera::Zoom(float factor)
{
    // 差ではなく倍率で変えると、遠いときは大きく、近いときは細かく寄れて操作しやすい
    m_distance = std::clamp(m_distance * factor, 3.0f, 300.0f);   // 近づきすぎ・離れすぎを防ぐ
}

//-----------------------------------------------------------------------------
// Pan
//   アルゴリズム:
//     カメラの前方ベクトル forward = normalize(target - pos) を求め、
//     right = normalize(cross(worldUp, forward)), up = cross(forward, right)
//     で画面の右・上方向を作り、その方向に注視点をずらす。
//-----------------------------------------------------------------------------
void Camera::Pan(float dx, float dy)
{
    XMFLOAT3 posF = GetPosition();                      // 現在のカメラ位置
    XMVECTOR pos = XMLoadFloat3(&posF);                 // 計算用の SIMD 型に載せ替える
    XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));  // 視線方向 (単位ベクトル)
    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);                // 世界の上方向
    // 外積で「視線と上方向の両方に垂直な向き」= 画面の右方向が得られる
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMVECTOR up = XMVector3Cross(forward, right);       // さらに外積を取ると画面の上方向

    target = XMVectorAdd(target, XMVectorScale(right, dx));   // 注視点を画面の右へ dx だけ動かす
    target = XMVectorAdd(target, XMVectorScale(up, dy));      // 同じく上へ dy だけ動かす
    XMStoreFloat3(&m_target, target);                   // 結果をメンバ変数に書き戻す
}

//-----------------------------------------------------------------------------
// GetPosition
//   極座標 (yaw, pitch, distance) → 直交座標
//-----------------------------------------------------------------------------
XMFLOAT3 Camera::GetPosition() const
{
    // 極座標 (方位角 yaw・仰角 pitch・距離 distance) から直交座標を求める。
    // 仰角ぶんだけ上に上がり、水平面に投影した長さが distance * cos(pitch) になる。
    const float cp = std::cos(m_pitch);     // 水平面へ投影したときの縮み率
    return XMFLOAT3(
        m_target.x + m_distance * cp * std::sin(m_yaw),   // 東西方向
        m_target.y + m_distance * std::sin(m_pitch),      // 高さ
        m_target.z + m_distance * cp * std::cos(m_yaw));  // 南北方向
}

//-----------------------------------------------------------------------------
// GetViewMatrix
//-----------------------------------------------------------------------------
XMMATRIX Camera::GetViewMatrix() const
{
    XMFLOAT3 posF = GetPosition();
    XMVECTOR pos = XMLoadFloat3(&posF);
    XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    return XMMatrixLookAtLH(pos, target, up);
}

//-----------------------------------------------------------------------------
// GetProjMatrix
//-----------------------------------------------------------------------------
XMMATRIX Camera::GetProjMatrix(float aspect) const
{
    return XMMatrixPerspectiveFovLH(m_fovY, aspect, m_nearZ, m_farZ);
}
