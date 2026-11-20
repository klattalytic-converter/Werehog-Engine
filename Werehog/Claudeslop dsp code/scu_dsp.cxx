#include "scu_dsp.hpp"
#include "transform_program.hpp"

namespace Engine::ScuDsp
{
    // SCU DSP ports (ST-097 3.3)
    static volatile uint32_t& PPAF = *reinterpret_cast<volatile uint32_t*>(0x25FE0080u);   // program control
    static volatile uint32_t& PPD  = *reinterpret_cast<volatile uint32_t*>(0x25FE0084u);   // program RAM data
    static volatile uint32_t& PDA  = *reinterpret_cast<volatile uint32_t*>(0x25FE0088u);   // data RAM address
    static volatile uint32_t& PDD  = *reinterpret_cast<volatile uint32_t*>(0x25FE008Cu);   // data RAM data

    static constexpr uint32_t EX = 1u << 16;   // execute / running
    static constexpr uint32_t LE = 1u << 15;   // load the program counter from bits 7-0

    void Stop()       { PPAF = 0; }
    bool Running()    { return (PPAF & EX) != 0; }   // program ends with END (not ENDI), so polling is safe

    void LoadProgram(const uint32_t* words, uint32_t count, uint8_t at)
    {
        Stop();
        PPAF = LE | at;                         // program RAM address = at (DSP halted)
        for (uint32_t i = 0; i < count; i++) PPD = words[i];
    }

    void WriteData(uint8_t bank, uint8_t address, const uint32_t* words, uint32_t count)
    {
        PDA = (uint32_t)((bank & 3) << 6) | (address & 63);
        for (uint32_t i = 0; i < count; i++) PDD = words[i];
    }

    void Start(uint8_t pc)
    {
        PPAF = LE | pc;                         // load PC while halted, then run  [PIN: one write
        PPAF = EX;                              //  with EX|LE may also work; two is the documented order]
    }

    void Transform::Init()
    {
        LoadProgram(Program::Transform, sizeof(Program::Transform) / sizeof(uint32_t));
    }

    void Transform::FromSglMatrix(const int32_t sgl[4][3], int32_t rows[12])
    {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++) rows[r * 4 + c] = sgl[c][r];
    }

    // True if the whole buffer is in high work RAM (0x06000000-0x060FFFFF, cached or not): the only
    // RAM the DSP's own DMA can reach.
    static bool InHighWorkRam(const void* p, uint32_t bytes)
    {
        const uint32_t a = reinterpret_cast<uintptr_t>(p) & 0x0FFFFFFFu;   // cached or cache-through
        return a >= 0x06000000u && a + bytes <= 0x06100000u;
    }

    alignas(16) static int32_t s_matrix[48];

    bool Transform::Begin(const int32_t rows[12], int32_t* stream, Vec26_6* out, uint32_t vertexCount)
    {
        if (vertexCount == 0 || (vertexCount & 7) != 0) return false;
        if (!InHighWorkRam(stream, StreamWords(vertexCount) * 4)) return false;
        if (!InHighWorkRam(out, vertexCount * sizeof(Vec26_6))) return false;
        if (!InHighWorkRam(s_matrix, sizeof(s_matrix))) return false;
        if (Running()) return false;

        stream[0] = (int32_t)(vertexCount / 8 - 1);
        for (int c = 0; c < 4; c++)
            for (int i = 0; i < 12; i++) s_matrix[c * 12 + i] = rows[i];

        uint32_t block[3];
        block[0] = (reinterpret_cast<uintptr_t>(out)      & 0x07FFFFFFu) >> 2;
        block[1] = (reinterpret_cast<uintptr_t>(stream)   & 0x07FFFFFFu) >> 2;
        block[2] = (reinterpret_cast<uintptr_t>(s_matrix) & 0x07FFFFFFu) >> 2;
        WriteData(0, 0, block, 3);
        Start(0);
        return true;
    }
}
