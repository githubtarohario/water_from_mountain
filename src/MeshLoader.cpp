//=============================================================================
// MeshLoader.cpp
//   OBJ ファイル読み込みの実装。
//=============================================================================
#include "MeshLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace DirectX;

namespace
{
    //-------------------------------------------------------------------------
    // 構造体 : IndexTriple
    // 概要   : 面の 1 頂点を表す (頂点番号, UV 番号, 法線番号) の組。0 始まり、無しは -1
    //-------------------------------------------------------------------------
    struct IndexTriple
    {
        int v, vt, vn;
        bool operator==(const IndexTriple& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
    };

    //-------------------------------------------------------------------------
    // 構造体 : IndexTripleHash
    // 概要   : IndexTriple を unordered_map のキーにするためのハッシュ関数
    //-------------------------------------------------------------------------
    struct IndexTripleHash
    {
        size_t operator()(const IndexTriple& t) const
        {
            return (static_cast<size_t>(t.v) * 73856093u)
                 ^ (static_cast<size_t>(t.vt + 1) * 19349663u)
                 ^ (static_cast<size_t>(t.vn + 1) * 83492791u);
        }
    };

    //-------------------------------------------------------------------------
    // 関数名 : ResolveIndex
    // 概要   : OBJ の 1 始まり / 負の相対インデックスを 0 始まりの絶対インデックスにする
    // 引数   : raw   : ファイルに書かれた値 (0 は「無し」)
    //          count : その要素 (v / vt / vn) の現在の個数
    // 戻り値 : 0 始まりのインデックス。無し・範囲外は -1
    //-------------------------------------------------------------------------
    int ResolveIndex(int raw, int count)
    {
        if (raw == 0) return -1;
        const int idx = (raw > 0) ? raw - 1 : count + raw;   // 負なら末尾からの相対
        return (idx >= 0 && idx < count) ? idx : -1;
    }

    //-------------------------------------------------------------------------
    // 関数名 : ParseFaceVertex
    // 概要   : "a", "a/b", "a//c", "a/b/c" のいずれかの文字列を IndexTriple にする
    // 引数   : token : 面要素の 1 トークン
    //          nv, nvt, nvn : 現在の頂点・UV・法線の個数 (相対インデックス解決用)
    // 戻り値 : IndexTriple (v が -1 なら不正)
    //-------------------------------------------------------------------------
    IndexTriple ParseFaceVertex(const std::string& token, int nv, int nvt, int nvn)
    {
        int raw[3] = { 0, 0, 0 };   // v, vt, vn の生の値 (0 = 指定なし)
        size_t start = 0;           // いま読んでいる位置
        for (int k = 0; k < 3; ++k) // k = 0:頂点, 1:UV, 2:法線
        {
            const size_t slash = token.find('/', start);    // 次の '/' の位置
            // '/' までを取り出す。'a//c' のように空のときは raw[k] = 0 のまま (指定なし)
            const std::string part = token.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!part.empty())
                raw[k] = std::atoi(part.c_str());
            if (slash == std::string::npos)
                break;              // '/' が無ければここで終わり ("a" だけの形式)
            start = slash + 1;      // 次の区切りの後ろから読み進める
        }
        return { ResolveIndex(raw[0], nv), ResolveIndex(raw[1], nvt), ResolveIndex(raw[2], nvn) };
    }
}

namespace MeshLoader
{
    //-------------------------------------------------------------------------
    // LoadObj
    //   アルゴリズム:
    //     1. 行ごとに先頭キーワードで分岐し、v / vt / vn を一時配列に貯める
    //     2. f 行は各トークンを IndexTriple に変換し、
    //        (v, vt, vn) が未登録なら新しい頂点を作ってマップに登録、既存なら再利用
    //     3. 多角形は (0, i, i+1) の扇状分割で三角形にする
    //-------------------------------------------------------------------------
    bool LoadObj(const std::wstring& path, MeshData& out, std::string* errorMsg)
    {
        std::ifstream file(path);   // MSVC は wstring パスを受け付ける
        if (!file)
        {
            if (errorMsg) *errorMsg = "file open failed";
            return false;
        }

        std::vector<XMFLOAT3> positions;   // v
        std::vector<XMFLOAT2> texcoords;   // vt
        std::vector<XMFLOAT3> normals;     // vn
        std::unordered_map<IndexTriple, uint32_t, IndexTripleHash> vertexMap;   // (v,vt,vn) → 出力頂点番号

        out = MeshData();

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream ss(line);
            std::string key;
            ss >> key;

            if (key == "v")
            {
                XMFLOAT3 p = { 0, 0, 0 };
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            }
            else if (key == "vt")
            {
                XMFLOAT2 t = { 0, 0 };
                ss >> t.x >> t.y;
                // OBJ の v は下が 0、D3D のテクスチャ座標は上が 0 なので反転する
                t.y = 1.0f - t.y;
                texcoords.push_back(t);
            }
            else if (key == "vn")
            {
                XMFLOAT3 n = { 0, 1, 0 };
                ss >> n.x >> n.y >> n.z;
                normals.push_back(n);
            }
            else if (key == "f")
            {
                std::vector<uint32_t> faceIndices;   // この面の出力頂点番号
                std::string token;
                while (ss >> token)
                {
                    const IndexTriple t = ParseFaceVertex(token,
                        static_cast<int>(positions.size()),
                        static_cast<int>(texcoords.size()),
                        static_cast<int>(normals.size()));
                    if (t.v < 0)
                        continue;   // 不正な頂点参照は無視

                    auto it = vertexMap.find(t);
                    if (it == vertexMap.end())
                    {
                        MeshVertex mv;
                        mv.pos    = positions[t.v];
                        mv.normal = (t.vn >= 0) ? normals[t.vn] : XMFLOAT3(0, 1, 0);
                        mv.uv     = (t.vt >= 0) ? texcoords[t.vt] : XMFLOAT2(0, 0);
                        const uint32_t newIndex = static_cast<uint32_t>(out.vertices.size());
                        out.vertices.push_back(mv);
                        it = vertexMap.emplace(t, newIndex).first;
                        if (t.vn >= 0) out.hasNormals = true;
                        if (t.vt >= 0) out.hasUVs = true;
                    }
                    faceIndices.push_back(it->second);
                }

                // 扇状三角形分割: 最初の頂点を軸に (0,1,2), (0,2,3), (0,3,4) … と分ける。
                // Blender が出力する四角形は 2 枚の三角形になる。凸な多角形なら常に正しい。
                for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
                {
                    out.indices.push_back(faceIndices[0]);
                    out.indices.push_back(faceIndices[i]);
                    out.indices.push_back(faceIndices[i + 1]);
                }
            }
            // その他 (o, g, s, usemtl, mtllib, l ...) は無視
        }

        if (out.indices.size() < 3)
        {
            if (errorMsg) *errorMsg = "no faces";
            return false;
        }

        if (!out.hasNormals)
            ComputeSmoothNormals(out);
        ComputeBounds(out);
        return true;
    }

    //-------------------------------------------------------------------------
    // ComputeSmoothNormals
    //   面法線 = (p1 - p0) × (p2 - p0) は面積に比例した長さを持つので、
    //   そのまま頂点に足し込むと面積重み付き平均になる。
    //-------------------------------------------------------------------------
    void ComputeSmoothNormals(MeshData& mesh)
    {
        for (auto& v : mesh.vertices)
            v.normal = XMFLOAT3(0, 0, 0);

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            MeshVertex& a = mesh.vertices[mesh.indices[i]];
            MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
            MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];

            const XMVECTOR p0 = XMLoadFloat3(&a.pos);
            const XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&b.pos), p0);   // 辺ベクトル b - a
            const XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&c.pos), p0);   // 辺ベクトル c - a
            // 外積は面に垂直で、長さが三角形の面積の 2 倍になる。
            // 正規化せずに足し込むことで、大きい面ほど強く影響する平均になる。
            const XMVECTOR fn = XMVector3Cross(e1, e2);

            for (MeshVertex* v : { &a, &b, &c })
            {
                XMVECTOR n = XMVectorAdd(XMLoadFloat3(&v->normal), fn);
                XMStoreFloat3(&v->normal, n);
            }
        }

        for (auto& v : mesh.vertices)
        {
            XMVECTOR n = XMLoadFloat3(&v.normal);
            if (XMVectorGetX(XMVector3LengthSq(n)) < 1e-12f)
                n = XMVectorSet(0, 1, 0, 0);   // 孤立頂点などは上向きにしておく
            XMStoreFloat3(&v.normal, XMVector3Normalize(n));
        }
        mesh.hasNormals = true;
    }

    //-------------------------------------------------------------------------
    // ComputeBounds
    //-------------------------------------------------------------------------
    void ComputeBounds(MeshData& mesh)
    {
        if (mesh.vertices.empty())
        {
            mesh.boundsMin = mesh.boundsMax = XMFLOAT3(0, 0, 0);
            return;
        }
        XMFLOAT3 mn = mesh.vertices[0].pos;
        XMFLOAT3 mx = mesh.vertices[0].pos;
        for (const auto& v : mesh.vertices)
        {
            mn.x = std::min(mn.x, v.pos.x); mn.y = std::min(mn.y, v.pos.y); mn.z = std::min(mn.z, v.pos.z);
            mx.x = std::max(mx.x, v.pos.x); mx.y = std::max(mx.y, v.pos.y); mx.z = std::max(mx.z, v.pos.z);
        }
        mesh.boundsMin = mn;
        mesh.boundsMax = mx;
    }
}
