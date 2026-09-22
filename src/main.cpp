//=============================================================================
// main.cpp
//   「山から粒子が落ちる」シミュレーションのエントリーポイント。
//
//   構成:
//     Graphics      : DirectX 11 の初期化と共通リソース
//     Terrain       : 蛇行する谷を持つ山の地形 (高さマップ + 岩テクスチャ)
//     SPH           : CPU (OpenMP) による粒子流体シミュレーション
//     FluidRenderer : 粒子の球描画 / スクリーンスペース流体による表面生成
//     Camera        : マウス操作のオービットカメラ
//
//   操作:
//     マウス左ドラッグ : カメラ回転
//     マウス右ドラッグ : カメラ平行移動
//     マウスホイール   : ズーム
//     1                : 粒子表示モード (速度で着色)
//     2                : 表面生成モード (水面として描画)
//     Space            : 一時停止 / 再開
//     R                : 粒子をリセット
//     Esc              : 終了
//
//   1 フレームの流れ:
//     入力処理 → SPH::Update (物理) → 定数バッファ更新 → 地形描画 → 粒子描画 → Present
//=============================================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <chrono>
#include <cmath>

#include "Graphics.h"
#include "FrameConstants.h"
#include "Camera.h"
#include "Terrain.h"
#include "SPH.h"
#include "FluidRenderer.h"

using namespace DirectX;

namespace
{
    //-------------------------------------------------------------------------
    // アプリケーション全体の状態 (ウィンドウプロシージャからも触るため名前空間スコープに置く)
    //-------------------------------------------------------------------------
    struct AppState
    {
        Graphics       gfx;          // D3D11
        Camera         camera;       // カメラ
        Terrain        terrain;      // 地形
        SPH            sph;          // 流体シミュレーション
        FluidRenderer  fluidRenderer;// 粒子描画
        ComPtr<ID3D11Buffer> cbFrame;// フレーム定数バッファ (b0)
        FrameConstants frame = {};   // 定数バッファの CPU 側コピー

        FluidRenderMode mode = FluidRenderMode::Particles;   // 表示モード
        bool  paused = false;        // 一時停止中か
        bool  initialized = false;   // 初期化完了フラグ (WM_SIZE などで未初期化アクセスを避ける)

        // マウス操作
        bool  lButtonDown = false;   // 左ボタン押下中
        bool  rButtonDown = false;   // 右ボタン押下中
        int   lastMouseX = 0;        // 前回のマウス x
        int   lastMouseY = 0;        // 前回のマウス y

        // 計測
        float fps = 0.0f;            // 直近の FPS
        float fpsTimer = 0.0f;       // FPS 計測用の経過時間
        int   fpsFrames = 0;         // FPS 計測用のフレーム数
    };

    AppState g_app;

    //-------------------------------------------------------------------------
    // 関数名 : FileExists
    // 概要   : ファイルが存在するか調べる
    // 引数   : path : ファイルパス
    // 戻り値 : true = 存在する
    //-------------------------------------------------------------------------
    bool FileExists(const std::wstring& path)
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    //-------------------------------------------------------------------------
    // 関数名 : FindProjectDirectories
    // 概要   : shaders フォルダと assets フォルダを探して Graphics に登録する。
    //          実行ファイルの場所が bin\ や bin\Release\ でも動くように、
    //          カレントディレクトリ → exe のフォルダ → その親 → さらに親 の順に探す。
    //          (shaders\Common.hlsli の存在で判定し、assets は同じ階層とみなす)
    // 引数   : なし
    // 戻り値 : true = shaders が見つかった (assets はなくてもよい: ノイズ生成にフォールバック)
    //-------------------------------------------------------------------------
    bool FindProjectDirectories()
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        const size_t slash = exeDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            exeDir = exeDir.substr(0, slash);

        const std::wstring roots[] = {
            L".",
            exeDir,
            exeDir + L"\\..",
            exeDir + L"\\..\\..",
        };
        for (const auto& root : roots)
        {
            if (FileExists(root + L"\\shaders\\Common.hlsli"))
            {
                Graphics::SetShaderDirectory(root + L"\\shaders");
                Graphics::SetAssetDirectory(root + L"\\assets");
                return true;
            }
        }
        return false;
    }

    //-------------------------------------------------------------------------
    // 関数名 : UpdateFrameConstants
    // 概要   : カメラと光源からフレーム定数を組み立てて GPU に転送する
    // 引数   : なし (g_app を使用)
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void UpdateFrameConstants()
    {
        Graphics& gfx = g_app.gfx;
        FrameConstants& fc = g_app.frame;

        // 画面の縦横比。これを射影行列に渡さないと、横長の画面で絵が引き伸ばされてしまう
        const float aspect = static_cast<float>(gfx.GetWidth()) / static_cast<float>(gfx.GetHeight());
        const XMMATRIX view = g_app.camera.GetViewMatrix();      // ワールド → カメラ基準へ移す行列
        const XMMATRIX proj = g_app.camera.GetProjMatrix(aspect);// カメラ基準 → 画面へ映す行列

        // HLSL 側で mul(float4(v,1), M) と書けるように転置して格納する
        // (DirectXMath は「行ベクトル × 行列」、HLSL の既定は列優先のため、そのままだと食い違う)
        XMStoreFloat4x4(&fc.View,     XMMatrixTranspose(view));
        XMStoreFloat4x4(&fc.Proj,     XMMatrixTranspose(proj));
        XMStoreFloat4x4(&fc.ViewProj, XMMatrixTranspose(view * proj));   // 2 つをまとめた行列

        const XMFLOAT3 camPos = g_app.camera.GetPosition();      // 鏡面反射の計算に使う視点の位置
        fc.CameraPosW = XMFLOAT4(camPos.x, camPos.y, camPos.z, 1.0f);

        // 光源: やや手前上方から谷を照らす (光源へ向かう単位ベクトル)
        const XMVECTOR lightW = XMVector3Normalize(XMVectorSet(-0.35f, 0.85f, 0.45f, 0.0f));
        // 同じ向きをカメラ基準に直したもの。粒子や水面の陰影はビュー空間で計算するので両方渡す。
        // TransformNormal は平行移動を無視するので、向きを変換するときはこちらを使う
        const XMVECTOR lightV = XMVector3Normalize(XMVector3TransformNormal(lightW, view));
        XMStoreFloat4(&fc.LightDirW, lightW);
        XMStoreFloat4(&fc.LightDirV, lightV);

        // 画面サイズと、その逆数 (ピクセル → 0～1 の座標に直すときに使う)
        fc.ScreenSize = XMFLOAT4(
            static_cast<float>(gfx.GetWidth()), static_cast<float>(gfx.GetHeight()),
            1.0f / gfx.GetWidth(), 1.0f / gfx.GetHeight());

        // x: 粒子の描画半径 (描画側で上書きされる)、y: 経過時間、z/w: ぼかし方向
        fc.Params = XMFLOAT4(0.15f, g_app.sph.GetSimTime(), 0.0f, 0.0f);

        gfx.UpdateBuffer(g_app.cbFrame.Get(), &fc, sizeof(fc));   // CPU 側の内容を GPU へ転送

        // 定数バッファを頂点シェーダーとピクセルシェーダーの両方に接続する (HLSL の register(b0))
        ID3D11Buffer* cb = g_app.cbFrame.Get();
        gfx.GetContext()->VSSetConstantBuffers(0, 1, &cb);
        gfx.GetContext()->PSSetConstantBuffers(0, 1, &cb);
    }

    //-------------------------------------------------------------------------
    // 関数名 : UpdateTitle
    // 概要   : ウィンドウタイトルに粒子数・FPS・モードを表示する
    // 引数   : hWnd : ウィンドウハンドル
    // 戻り値 : なし
    //-------------------------------------------------------------------------
    void UpdateTitle(HWND hWnd)
    {
        wchar_t buf[256];
        swprintf_s(buf, L"山から粒子が落ちる SPH  |  粒子: %d  FPS: %.0f  モード: %s%s  地形: %s  テクスチャ: %s  |  [1]粒子 [2]表面 [Space]停止 [R]リセット",
            g_app.sph.GetParticleCount(), g_app.fps,
            g_app.mode == FluidRenderMode::Particles ? L"粒子" : L"表面生成",
            g_app.paused ? L" (一時停止)" : L"",
            g_app.terrain.IsMeshFromFile() ? L"OBJ" : L"生成",
            g_app.terrain.IsTextureFromFile() ? L"画像" : L"ノイズ生成");
        SetWindowTextW(hWnd, buf);
    }

    //-------------------------------------------------------------------------
    // 関数名 : WndProc
    // 概要   : ウィンドウメッセージの処理 (入力・リサイズ・終了)
    // 引数   : Win32 標準
    // 戻り値 : メッセージ処理結果
    //-------------------------------------------------------------------------
    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (g_app.initialized && wParam != SIZE_MINIMIZED)
            {
                const int w = LOWORD(lParam);
                const int h = HIWORD(lParam);
                if (g_app.gfx.Resize(w, h))
                    g_app.fluidRenderer.OnResize(g_app.gfx);
            }
            return 0;

        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_ESCAPE: PostQuitMessage(0); break;
            case '1':       g_app.mode = FluidRenderMode::Particles; break;
            case '2':       g_app.mode = FluidRenderMode::Surface; break;
            case VK_SPACE:  g_app.paused = !g_app.paused; break;
            case 'R':       g_app.sph.Reset(); break;
            default: break;
            }
            return 0;

        case WM_LBUTTONDOWN:
            g_app.lButtonDown = true;
            g_app.lastMouseX = GET_X_LPARAM(lParam);
            g_app.lastMouseY = GET_Y_LPARAM(lParam);
            SetCapture(hWnd);
            return 0;

        case WM_RBUTTONDOWN:
            g_app.rButtonDown = true;
            g_app.lastMouseX = GET_X_LPARAM(lParam);
            g_app.lastMouseY = GET_Y_LPARAM(lParam);
            SetCapture(hWnd);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            g_app.lButtonDown = false;
            g_app.rButtonDown = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE:
        {
            const int x = GET_X_LPARAM(lParam);     // 今回のマウス位置 (ウィンドウ内のピクセル)
            const int y = GET_Y_LPARAM(lParam);
            const int dx = x - g_app.lastMouseX;    // 前回からの移動量。これが回転・移動の量になる
            const int dy = y - g_app.lastMouseY;
            if (g_app.lButtonDown)
            {
                // 1 ピクセルあたり 0.3 度回転。dx を負にすると「掴んで回す」感覚の向きになる
                g_app.camera.Rotate(-dx * 0.005f, dy * 0.005f);
            }
            else if (g_app.rButtonDown)
            {
                g_app.camera.Pan(-dx * 0.05f, dy * 0.05f);   // 注視点ごと平行移動
            }
            g_app.lastMouseX = x;                   // 次回の差分計算のために覚えておく
            g_app.lastMouseY = y;
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app.camera.Zoom(delta > 0 ? 0.9f : 1.1f);
            return 0;
        }

        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

//-----------------------------------------------------------------------------
// 関数名 : wWinMain
// 概要   : ウィンドウ作成 → 各モジュール初期化 → メインループ
// 引数   : Win32 標準
// 戻り値 : 終了コード
//-----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // ---- ウィンドウクラス登録 ----
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MountainParticlesWindow";
    RegisterClassExW(&wc);

    // ---- ウィンドウ作成 (クライアント領域が 1800x1100 になるよう調整。画面に収まらなければ縮める) ----
    int clientW = 1800;
    int clientH = 1100;
    {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (clientW > screenW - 80)  clientW = screenW - 80;
        if (clientH > screenH - 160) clientH = screenH - 160;
    }
    RECT rc = { 0, 0, clientW, clientH };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"山から粒子が落ちる SPH",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
        return 1;

    // ---- 初期化 ----
    if (!FindProjectDirectories())
    {
        MessageBoxW(hWnd, L"shaders フォルダが見つかりません。\n実行ファイルと同じ場所か、その親フォルダに shaders を置いてください。",
                    L"エラー", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!g_app.gfx.Initialize(hWnd, clientW, clientH))       // D3D11 デバイス・スワップチェーン
        return 1;
    if (!g_app.gfx.CreateConstantBuffer(sizeof(FrameConstants), g_app.cbFrame))   // 定数バッファ b0
        return 1;
    if (!g_app.terrain.Initialize(g_app.gfx))               // 地形・テクスチャ・地形シェーダー
        return 1;

    SPHParams params;   // 既定値を使用 (SPH.h を参照)
    g_app.sph.Initialize(&g_app.terrain, params);           // 粒子配列を確保 (地形は衝突判定に使う)

    if (!g_app.fluidRenderer.Initialize(g_app.gfx, params.maxParticles))   // 粒子バッファ・シェーダー
        return 1;
    // 描画する球の半径。粒子モードは間隔の半分 (粒同士が離れて見える)、
    // 表面モードは少し大きめにして隣の球と重ね、面に隙間ができないようにする
    g_app.fluidRenderer.SetParticleRadius(params.particleSpacing * 0.5f);
    g_app.fluidRenderer.SetSurfaceRadius(params.particleSpacing * 1.3f);   // 隣の粒子と重なるよう大きめ

    g_app.initialized = true;
    ShowWindow(hWnd, nCmdShow);

    // ---- メインループ ----
    using Clock = std::chrono::steady_clock;
    auto prevTime = Clock::now();
    float titleTimer = 0.0f;

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        // 経過時間。steady_clock は時刻合わせで巻き戻らないので、こういう用途に向く
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - prevTime).count();   // 前フレームからの秒数
        prevTime = now;

        // 物理更新 (内部で dt からサブステップ数を決めて、その回数だけ計算する)
        if (!g_app.paused)
            g_app.sph.Update(dt);

        // 描画
        const float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // 参考動画と同じ白背景
        g_app.gfx.BeginFrame(clearColor);       // 画面と深度バッファを消し、描画先を設定する
        UpdateFrameConstants();                 // カメラ・光源の情報を GPU へ送る
        g_app.terrain.Render(g_app.gfx);        // 先に地形を描く (深度バッファが埋まる)
        // 粒子を描く。GetRenderData は位置と速さを描画用の配列にまとめたもの
        g_app.fluidRenderer.Render(g_app.gfx, g_app.sph.GetRenderData(), g_app.mode,
                                   g_app.frame, g_app.cbFrame.Get());
        g_app.gfx.EndFrame(true);               // 描き上がった絵を画面に出す (true = 垂直同期)

        // FPS 計測 (0.5 秒ごとに更新)。毎フレーム計算すると数字が激しく動いて読めないため
        g_app.fpsTimer += dt;                   // 計測区間の経過時間
        g_app.fpsFrames++;                      // 計測区間に描いたフレーム数
        if (g_app.fpsTimer >= 0.5f)
        {
            g_app.fps = g_app.fpsFrames / g_app.fpsTimer;   // フレーム数 ÷ 秒数 = 1 秒あたりの枚数
            g_app.fpsTimer = 0.0f;
            g_app.fpsFrames = 0;
        }

        // タイトル更新 (0.25 秒ごと)
        titleTimer += dt;
        if (titleTimer >= 0.25f)
        {
            titleTimer = 0.0f;
            UpdateTitle(hWnd);
        }
    }

    return static_cast<int>(msg.wParam);
}
