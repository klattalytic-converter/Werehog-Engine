// frame_clock.hpp - 30 fps drawing with the game logic kept at 60 steps a second.
//
// For a learner: why two rates
//   The 1996 build drew a frame every V-blank (60 fps: slInitSystem(..., 1)), and all its
//   physics constants are "per 1/60 s" (jump 7.2 per frame, gravity -0.25 per frame...). The
//   remake draws every 2 V-blanks (SRL_FRAMERATE = 2 in the makefile: 30 fps), which gives the
//   CPUs and VDP1 twice the time per picture. If the logic also ran once per drawn frame,
//   everything would move at half speed. So the logic runs once per V-blank that went by:
//   normally 2 steps per drawn frame, 3 when a frame ran late (20 fps for that frame). The
//   game keeps its speed and its exact disc numbers whatever the drawing rate does, and an
//   occasional late frame costs a little smoothness, not slow motion. "24 fps minimum" then
//   means: at most 1 late frame in 4 (2+2+2+3 V-blanks = 9 per 4 frames = 26.7 fps).
//
// The frame budget at 30 fps (NTSC): 2 V-blanks = 33.4 ms. Per SH-2 at 28.6 MHz that is about
//   954,000 cycles, and there are two SH-2s (the slave runs jobs given with SRL::Slave). VDP1
//   draws the previous frame's command list during the same time, in parallel.
//
// Use (main loop):
//   Engine::FrameClock clock;                      // OnVblank: clock.Vblank()
//   for (;;) {
//       const int steps = clock.BeginFrame();      // 1..MaxSteps
//       for (int i = 0; i < steps; i++) Logic();   // physics, AI: exact 60 Hz steps
//       Draw();                                    // once
//       SRL::Core::Synchronize();                  // waits for the 2nd V-blank
//   }
#pragma once
#include <cstdint>

namespace Engine
{
    class FrameClock
    {
    public:
        static constexpr int MaxSteps = 4;      // after a long stall (loading), don't run 100 steps

        // Call from the V-blank handler (SRL::Core::OnVblank).
        void Vblank() { vblanks = vblanks + 1; }

        // Call once at the top of each drawn frame: how many 60 Hz logic steps to run.
        int BeginFrame()
        {
            const uint32_t now = vblanks;
            int steps = (int)(now - last);
            last = now;
            if (steps < 1) steps = 1;            // the first frame, or a very fast one
            if (steps > MaxSteps) steps = MaxSteps;
            lastSteps = steps;
            if (steps > 2) lateFrames++;
            frames++;
            return steps;
        }

        int LastSteps() const { return lastSteps; }
        // How many drawn frames took more than 2 V-blanks (show it in a debug overlay).
        uint32_t LateFrames() const { return lateFrames; }
        uint32_t Frames() const { return frames; }

    private:
        volatile uint32_t vblanks = 0;
        uint32_t last = 0, frames = 0, lateFrames = 0;
        int lastSteps = 2;
    };

    // Cycle counter for measuring code on the Saturn: the SH-2's free-running timer (FRT).
    // The FRT block starts at 0xFFFFFE10 (TIER; FTCSR at 0xFFFFFE11: the disc's slave loop uses
    // both, SGL_SYSTEM_ROUTINES.md 3). FRC, the 16-bit counter, follows at 0xFFFFFE12 (high byte)
    // / 0xFFFFFE13 (low byte): reading the high byte first latches the low byte, and TCR at
    // 0xFFFFFE16 selects the clock (internal / 8, 32 or 128). ST-TECH sends readers to the SH7095
    // hardware manual's FRT chapter for these, which isn't in the project, and SBL's TIM_FRT_*
    // calls (ST-135) wrap the same registers. [PIN: confirm FRC/TCR there, and what TCR SGL
    // leaves on the master; SetDivider8() sets internal / 8]. At / 8 one count = 8 CPU cycles,
    // and the 16-bit counter wraps every 524,288 cycles: time sections shorter than half a frame.
    struct Perf
    {
        static uint16_t Now()
        {
#ifdef __sh__
            volatile uint8_t* frt = (volatile uint8_t*)0xFFFFFE12;
            const uint8_t hi = frt[0];
            const uint8_t lo = frt[1];
            return (uint16_t)(hi << 8 | lo);
#else
            return 0;
#endif
        }
        static void SetDivider8()
        {
#ifdef __sh__
            volatile uint8_t* tcr = (volatile uint8_t*)0xFFFFFE16;
            *tcr = (uint8_t)(*tcr & ~3);          // CKS1:0 = 00: internal clock / 8
#endif
        }
        // Cycles between two Now() readings at divider 8.
        static uint32_t Cycles(uint16_t from, uint16_t to) { return (uint32_t)(uint16_t)(to - from) * 8; }
    };
}
