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
    m_yaw   += dYaw;
    m_pitch += dPitch;
    const float limit = XMConvertToRadians(89.0f);
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

//-----------------------------------------------------------------------------
// Zoom
//-----------------------------------------------------------------------------
void Camera::Zoom(float factor)
{
    m_distance = std::clamp(m_distance * factor, 3.0f, 300.0f);
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
    XMFLOAT3 posF = GetPosition();
    XMVECTOR pos = XMLoadFloat3(&posF);
    XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));
    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMVECTOR up = XMVector3Cross(forward, right);

    target = XMVectorAdd(target, XMVectorScale(right, dx));
    target = XMVectorAdd(target, XMVectorScale(up, dy));
    XMStoreFloat3(&m_target, target);
}

//-----------------------------------------------------------------------------
// GetPosition
//   極座標 (yaw, pitch, distance) → 直交座標
//-----------------------------------------------------------------------------
XMFLOAT3 Camera::GetPosition() const
{
    const float cp = std::cos(m_pitch);
    return XMFLOAT3(
        m_target.x + m_distance * cp * std::sin(m_yaw),
        m_target.y + m_distance * std::sin(m_pitch),
        m_target.z + m_distance * cp * std::cos(m_yaw));
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
