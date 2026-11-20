# Werehog-Engine
A full remaster and restoration of Sonic X-Treme's actual game engine, using [the amazing C++ SGL Wrapper SaturnRingLib](https://github.com/ReyeMe/SaturnRingLib)

----

The only public build of Sonic X-treme that actually uses the engine the project would end up going with (Christina Coffin's "boss engine") is the "718 Build".
Werehog engine was built off of the Pseudocode spit out by Ghidra from that build.

What's the point of doing this if Sonic Jam's Sonic World exists? 
Well mostly to look at and preserve an absolutely fascinating codebase, but also because Jam's 3D engine is outrageously complex compared to the boss engine. A LOT of that engine was written in straight up assembly, and it makes use of a ton of proprietary and undocumented formats and stuff. I'll leave that to people who know what they're doing. Sonic X-Treme is also one of the most infamous cancelled games of all time, and one with some of the most misinformation surrounding it, so I'd prefer to help make this game a little more documented instead.

----
AI use disclaimer
I've been seeing a LOT of exciting fan projects that end up being pretty much entirely Vibecoded and barely disclose it. So what about Werehog Engine?

- Does this project use AI code?
Well yes... and no. The real answer would be "partially".
I won't deny that LRMs have been a very useful tool for this project, but any work done by these models is extremely closely monitored, combed through, and cleaned up by yours truly. Most of the code is human written however. Also, given that the AI isn't really writing entirely new code so much as translating from C to C++, it's not nearly as sloppified as you'd expect.
Also, the text and annotations ARE human written!! Unfortunately, I tend to write with lots of grammar, plus stuff like oxford commas so it looks like slop. :(


- Why use AI for parts of this?
Pretty simple, these models are really good at combing through a ton of text and data, and doing monotonous tasks over and over and over again until they work.
These models are also pretty good at creating tooling and giving write-ups on what different chunks of code are doing. Basically, because I can't afford a dev team, AI is getting used to troubleshoot stuff, translate slow functions into SH2 assembly, make tooling, write (some!) scripts, and generally serve as an abstraction layer between the original build's C, and Werehog's C++.

- Which AI models were used?
Claude Opus 5.5 for I'd say 75% of it. Fable 5.1 and 5.2 were used for a few stubborn things, and Haiku 5 was used for the writeups.


- Is any of Werehog's art or sound AI generated?
No. Unless you mean translated from the raw binary data back into workable images.

- Do you support further AI development?
Actual AI? Sure, but not LLMs/LRMs. They're decently impressive technology that works in possibly the most inefficient way possible, while also destroying everything around them. I'm using it because if we're getting screwed anyway, may at least use some of the water (that'll be all be gone in 10 years) for help with something cool and harmless.
----
