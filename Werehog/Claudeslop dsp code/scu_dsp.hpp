// Formats
//   Matrix: 3 rows {a, b, c, t}, 16.16 - the layout SGL keeps internally.
//   Vertices in and out: Vec26_6 {x, y, z}, 26.6 fixed point (value * 64). The 6 fraction bits
//   are what lets the translation column cost nothing: "w = 1.0" is the immediate 64, the
//   largest power of two the D1 bus's 8-bit immediate can load (Sonic R does the same).
//
// Rules (SCU manual ST-097 fig. 2.3; ST-TECH SCU DSP bulletin)
//   - In and out buffers must be in HIGH work RAM (0x06000000-0x060FFFFF): DSP DMA reaches only
//     high work RAM and the A/B buses, not low work RAM.
//   - Input stream: one header word (Begin writes the loop count there), the vertices, then 4
//     vertices of readable padding (the loop prefetches one group ahead): StreamWords(n) longs.
//     Vertex count: a multiple of 8 (two 4-vertex groups per loop).
//   - Results are written by DMA behind the SH-2 cache: read them through the cache-through
//     mirror (Uncached()) or purge the lines first.
//   - SGL 2.x transforms polygons on the SH-2 (SGL_SMPC_AND_INTERRUPTS.md 4.1) and SRL has no
//     SCU DSP code, so the DSP is free; anything else that loads a DSP program must not run at
//     the same time.
#pragma once
#include <cstdint>

namespace Engine::ScuDsp
{
    struct Vec26_6 { int32_t x, y, z; };

    inline int32_t To26_6(int32_t fixed16)  { return fixed16 >> 10; }
    inline int32_t To16_16(int32_t v26_6)   { return v26_6 * 1024; }

    // ---- low level (SCU manual 3.3, DSP control ports)
    void Stop();
    void LoadProgram(const uint32_t* words, uint32_t count, uint8_t at = 0);
    void WriteData(uint8_t bank, uint8_t address, const uint32_t* words, uint32_t count);
    void Start(uint8_t pc = 0);
    bool Running();

    // ---- world-matrix transform
    class Transform
    {
    public:
        static void Init();

        static void FromSglMatrix(const int32_t sgl[4][3], int32_t rows[12]);

        static constexpr uint32_t StreamWords(uint32_t vertexCount) { return 1 + 3 * vertexCount + 12; }
        static Vec26_6* Vertices(int32_t* stream) { return reinterpret_cast<Vec26_6*>(stream + 1); }

        // Returns false if the buffers or the count break the rules above.
        static bool Begin(const int32_t rows[12], int32_t* stream, Vec26_6* out, uint32_t vertexCount);
        static bool Busy() { return Running(); }
        static void Wait()  { while (Running()) { } }

        template <typename T> static const T* Uncached(const T* p)
        { return reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(p) | 0x20000000u); }
    };
}
