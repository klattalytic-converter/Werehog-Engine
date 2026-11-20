#pragma once
#include <cstdint>
#include <srl.hpp>
#include "scu_dsp.hpp"

namespace Engine { struct LodRange; } 

namespace Engine::ScuDsp
{
    // A projected vertex. x, y: pixels from the screen centre (VDP1 local coordinates, which SGL
    // puts at the centre). z: view-space depth, 26.6. z == 0 marks "in front of the forward
    // boundary": not drawable.
    struct ScreenPoint { int32_t x, y, z; };

    // The viewing volume, in the numbers SGL itself uses.
    struct Projection
    {
        int32_t dist   = 0;     // screen distance, 16.16 (SGL's MsScreenDist)
        int32_t near26 = 0;     // forward boundary (slZdspLevel), 26.6
        int32_t far26  = 0;     // rear boundary (slWindow's Zlimit), 26.6
        int16_t left = 0, top = 0, right = 0, bottom = 0;   // window, relative to the centre

        // SGL 2.0a's slPerspective, step for step: t = |tan(angle/2)|, then (width/2 as 16.16)
        // << 16 over t. Engine::Div is ((a << 16) / b) on the SH-2 divider: the same division.
        static int32_t ScreenDist(uint16_t screenWidth, int32_t tanHalf);

        // From a screen distance (tests use this directly).
        static Projection FromDist(int32_t dist, uint16_t width, uint16_t height,
                                   uint16_t zdspLevel, int16_t zlimit);

        // What the game uses: the same angle given to SRL::Scene3D::SetPerspective (SGL ANGLE).
        static Projection Make(uint16_t width, uint16_t height, uint16_t angle,
                               uint16_t zdspLevel, int16_t zlimit = 0x7FFF);
    };

    struct VdpCommand
    {
        uint16_t ctrl, link, pmod, colr, srca, size;
        int16_t  xa, ya, xb, yb, xc, yc, xd, yd;
        uint16_t grda, dmmy;
    };
    static_assert(sizeof(VdpCommand) == 32, "VDP1 command tables are 32 bytes");

    struct TextureEntry { uint16_t hsize, vsize, cgadr, hvsize; };

    // ------------------------------------------------------------------ the steps, testable alone

    uint32_t FillStream(const SRL::Math::Types::Vector3D* v, uint32_t count, int32_t* stream);

    void Project(const Vec26_6* view, uint32_t count, const Projection& p, ScreenPoint* out);

    bool BuildFace(const ScreenPoint* pts, const Vec26_6* view, const SRL::Types::Polygon& face,
                   const SRL::Types::Attribute& a, const TextureEntry* textures,
                   const int32_t rows[12], const Projection& p,
                   VdpCommand& cmd, int32_t& sortZ, int32_t prevZ);

    // ------------------------------------------------------------------ the whole model

    template <uint32_t N> struct DspMeshBuffers
    {
        static constexpr uint32_t Capacity = (N + 7) & ~7u;
        alignas(16) int32_t     stream[Transform::StreamWords(Capacity)];
        alignas(16) Vec26_6     view[Capacity];
        ScreenPoint             points[Capacity];
    };

    class DspMesh
    {
    public:
        template <uint32_t N> explicit DspMesh(DspMeshBuffers<N>& b)
            : stream(b.stream), view(b.view), points(b.points), capacity(b.Capacity) {}

        // The model's attributes are all ones DspMesh can draw (no UseLight / UseDepth).
        static bool Supports(const SRL::Types::Mesh& m);

        // Compare against SGL's own projection; false = don't use DspMesh (see the header).
        // Call once after SetPerspective, before drawing.
        static bool Calibrate(const Projection& p);

        bool Begin(const SRL::Types::Mesh& m);            // false: DSP busy or model too big
        // lod (optional, lod_fog.hpp): faces whose depth (the sortZ SGL sorts by) is past
        // lod->textured become flat polygons in their far colour, faded by lod->fog; faces past
        // lod->end are dropped. Per face, one compare (one multiply more inside the fog).
        uint32_t Finish(const SRL::Types::Mesh& m, const Projection& p,
                        const TextureEntry* textures,
                        const LodRange* lod = nullptr);   // returns the faces drawn

    private:
        int32_t*     stream;
        Vec26_6*     view;
        ScreenPoint* points;
        uint32_t     capacity;
        uint32_t     padded = 0;
        int32_t      rows[12] = {};
    };
}
