#include "dsp_mesh.hpp"
#include "../fx.hpp"
#include "../lod_fog.hpp"


namespace Engine::ScuDsp
{
    static constexpr uint8_t OptTexture = 1 << 2, OptLight = 1 << 3, OptDepth = 1 << 4;
    static constexpr int32_t VdpMin = -1024, VdpMax = 1023;

    // ---------------------------------------------------------------- Projection

    int32_t Projection::ScreenDist(uint16_t screenWidth, int32_t tanHalf)
    {
        const int32_t t = tanHalf < 0 ? -tanHalf : tanHalf;
        return Engine::Div((int32_t)(screenWidth * 32768u), t);
    }

    Projection Projection::FromDist(int32_t dist, uint16_t width, uint16_t height,
                                    uint16_t zdspLevel, int16_t zlimit)
    {
        Projection p;
        p.dist   = dist;
        p.near26 = (dist >> zdspLevel) >> 10;
        p.far26  = (int32_t)zlimit << 6;
        p.left   = (int16_t)-(width / 2);  p.right  = (int16_t)(width - width / 2 - 1);
        p.top    = (int16_t)-(height / 2); p.bottom = (int16_t)(height - height / 2 - 1);
        return p;
    }

    Projection Projection::Make(uint16_t width, uint16_t height, uint16_t angle,
                                uint16_t zdspLevel, int16_t zlimit)
    {
        const int32_t t = slTan((ANGLE)((int16_t)angle >> 1));
        return FromDist(ScreenDist(width, t), width, height, zdspLevel, zlimit);
    }

    // ---------------------------------------------------------------- the steps

    uint32_t FillStream(const SRL::Math::Types::Vector3D* v, uint32_t count, int32_t* stream)
    {
        const uint32_t padded = (count + 7) & ~7u;
        Vec26_6* dst = Transform::Vertices(stream);
        uint32_t i = 0;
        for (; i < count; i++)
            dst[i] = { v[i].X.RawValue() >> 10, v[i].Y.RawValue() >> 10, v[i].Z.RawValue() >> 10 };
        for (; i < padded + 4; i++) dst[i] = { 0, 0, 0 };
        return padded;
    }

    void Project(const Vec26_6* view, uint32_t count, const Projection& p, ScreenPoint* out)
    {
        const int32_t d = p.dist >> 6;
        const int32_t nearZ = p.near26 > 0 ? p.near26 : 1;
        for (uint32_t i = 0; i < count; i++)
        {
            const Vec26_6 v = view[i];
            if (v.z < nearZ) { out[i] = { 0, 0, 0 }; continue; }
            const int32_t q = Engine::Div(d, v.z);
            out[i] = { Engine::Mul(v.x, q) >> 10, Engine::Mul(v.y, q) >> 10, v.z };
        }
    }

    bool BuildFace(const ScreenPoint* pts, const Vec26_6* view, const SRL::Types::Polygon& face,
                   const SRL::Types::Attribute& a, const TextureEntry* textures,
                   const int32_t rows[12], const Projection& p,
                   VdpCommand& cmd, int32_t& sortZ, int32_t prevZ)
    {
        const uint16_t* vi = face.Vertices;
        const ScreenPoint& A = pts[vi[0]];
        const ScreenPoint& B = pts[vi[1]];
        const ScreenPoint& C = pts[vi[2]];
        const ScreenPoint& D = pts[vi[3]];

        // 1. Any corner in front of the forward boundary: SGL's UseNearClip drops the face.
        if (A.z == 0 || B.z == 0 || C.z == 0 || D.z == 0) return false;

        // 2. Bounding box against the window, then against what VDP1 can address.
        auto mn = [](int32_t a, int32_t b, int32_t c, int32_t d) { int32_t m = a < b ? a : b; m = m < c ? m : c; return m < d ? m : d; };
        auto mx = [](int32_t a, int32_t b, int32_t c, int32_t d) { int32_t m = a > b ? a : b; m = m > c ? m : c; return m > d ? m : d; };
        const int32_t x0 = mn(A.x, B.x, C.x, D.x), x1 = mx(A.x, B.x, C.x, D.x);
        const int32_t y0 = mn(A.y, B.y, C.y, D.y), y1 = mx(A.y, B.y, C.y, D.y);
        if (x1 < p.left || x0 > p.right || y1 < p.top || y0 > p.bottom) return false;
        if (x0 < VdpMin || x1 > VdpMax || y0 < VdpMin || y1 > VdpMax) return false;

        // 3. Wholly beyond the rear boundary.
        const int32_t z0 = mn(A.z, B.z, C.z, D.z), z1 = mx(A.z, B.z, C.z, D.z);
        if (z0 > p.far26) return false;

        // 4. Single-sided faces: drop the ones facing away. The face normal, turned into view
        //    space by the matrix's rotation part, must point back towards the eye (the origin):
        //    dot(normal, corner A) < 0. Units don't matter, only the sign.
        if ((uint8_t)a.Visibility == 0)
        {
            const Engine::Raw n[3] = { face.Normal.X.RawValue(), face.Normal.Y.RawValue(), face.Normal.Z.RawValue() };
            const Engine::Raw rn[3] = { Engine::Dot3(&rows[0], n), Engine::Dot3(&rows[4], n), Engine::Dot3(&rows[8], n) };
            const Vec26_6& v = view[vi[0]];
            const Engine::Raw va[3] = { v.x, v.y, v.z };
            if (Engine::Dot3(rn, va) >= 0) return false;
        }

        // 5. The Z SGL sorts by (ATTR.sort's low 2 bits: SORT_BFR, SORT_MIN, SORT_MAX, SORT_CEN),
        //    as 16.16 for slSetSprite. 26.6 -> 16.16 is << 10.
        switch (a.Sort & 3)
        {
        case 0:  sortZ = prevZ >= 0 ? prevZ : ((A.z + B.z + C.z + D.z) >> 2) << 10; break;
        case 1:  sortZ = z0 << 10; break;
        case 2:  sortZ = z1 << 10; break;
        default: sortZ = ((A.z + B.z + C.z + D.z) >> 2) << 10; break;
        }

        // 6. The VDP1 command: the ATTR fields go straight in, as SGL puts them.
        cmd.ctrl = (uint16_t)(a.Direction & 0x3F);
        cmd.link = 0;
        cmd.pmod = a.Display;
        cmd.colr = a.ColorMode;
        if (a.Sort & OptTexture) { cmd.srca = textures[a.Texture].cgadr; cmd.size = textures[a.Texture].hvsize; }
        else                     { cmd.srca = 0; cmd.size = 0; }
        cmd.xa = (int16_t)A.x; cmd.ya = (int16_t)A.y; cmd.xb = (int16_t)B.x; cmd.yb = (int16_t)B.y;
        cmd.xc = (int16_t)C.x; cmd.yc = (int16_t)C.y; cmd.xd = (int16_t)D.x; cmd.yd = (int16_t)D.y;
        cmd.grda = a.Gouraud;
        cmd.dmmy = 0;
        return true;
    }

    // ---------------------------------------------------------------- DspMesh

    bool DspMesh::Supports(const SRL::Types::Mesh& m)
    {
        for (size_t f = 0; f < m.FaceCount; f++)
            if (m.Attributes[f].Sort & (OptLight | OptDepth)) return false;
        return m.VertexCount > 0 && m.VertexCount <= 0xFFFF;
    }

    bool DspMesh::Calibrate(const Projection& p)
    {
        // Three points, straight ahead of the eye (unit matrix), well inside the screen.
        static const int32_t test[3][3] = {
            {  50 << 16, -30 << 16,  400 << 16 },
            { -120 << 16, 80 << 16,  900 << 16 },
            {  10 << 16,  25 << 16,   60 << 16 },
        };
        if (!slPushUnitMatrix()) return false;
        int32_t offX = 0, offY = 0;
        bool same = true;
        for (int i = 0; i < 3; i++)
        {
            FIXED pos[3] = { test[i][0], test[i][1], test[i][2] };
            int32_t sgl[2] = { 0, 0 };
            slConvert3Dto2D(pos, sgl);
            const Vec26_6 v = { pos[0] >> 10, pos[1] >> 10, pos[2] >> 10 };
            ScreenPoint ours;
            Project(&v, 1, p, &ours);
            const int32_t dx = sgl[0] - ours.x, dy = sgl[1] - ours.y;
            if (i == 0) { offX = dx; offY = dy; }
            else if (dx - offX > 1 || offX - dx > 1 || dy - offY > 1 || offY - dy > 1) same = false;
        }
        slPopMatrix();
        auto near1 = [](int32_t a, int32_t b) { return a - b <= 1 && b - a <= 1; };
        const bool centred = near1(offX, 0) && near1(offY, 0);
        const bool corner  = near1(offX, -p.left) && near1(offY, -p.top);
        return same && (centred || corner);
    }

    bool DspMesh::Begin(const SRL::Types::Mesh& m)
    {
        if (m.VertexCount == 0 || ((m.VertexCount + 7) & ~7u) > capacity || Transform::Busy()) return false;
        padded = FillStream(m.Vertices, (uint32_t)m.VertexCount, stream);
        MATRIX current;
        slGetMatrix(current);
        Transform::FromSglMatrix(current, rows);
        return Transform::Begin(rows, stream, view, padded);
    }

    uint32_t DspMesh::Finish(const SRL::Types::Mesh& m, const Projection& p, const TextureEntry* textures,
                             const LodRange* lod)
    {
        Transform::Wait();
        // The DSP wrote these by DMA, behind the SH-2's cache: read them cache-through.
        const Vec26_6* v = Transform::Uncached(view);
        Project(v, (uint32_t)m.VertexCount, p, points);

        uint32_t drawn = 0;
        int32_t prevZ = -1;
        for (size_t f = 0; f < m.FaceCount; f++)
        {
            VdpCommand cmd;
            int32_t z;
            if (!BuildFace(points, v, m.Faces[f], m.Attributes[f], textures, rows, p, cmd, z, prevZ)) continue;
            // far: a flat polygon in the face's far colour, faded; past the fog: not drawn
            if (lod && !FarFace(cmd.ctrl, cmd.pmod, cmd.colr, z, *lod)) continue;
            // slSetSprite copies the command into SGL's list, Z-sorted with SGL's own polygons
            // (SRL's Scene2D::DrawPolygon passes a stack SPRITE the same way).
            if (!slSetSprite(reinterpret_cast<SPRITE*>(&cmd), z)) break;   // SGL's list is full
            prevZ = z;
            drawn++;
        }
        return drawn;
    }
}
