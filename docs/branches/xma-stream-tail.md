# feature/xma-stream-tail

Based on `feature/ps5`.

## Why

In Lara Croft and the Guardian of Light (full version), Temple of Light's exit
door opens, Lara says a line, and then the game stops: she can't move, though
the pause menu and weapon wheel still work. Same on Linux and on the PS5.

The level's exit sequence pauses the world and waits for that voice line to
finish. The game counts a streamed sound as finished once the samples it has
read from the XMA context reach the stream's declared length (200960 samples
for this line). The SDK delivered 200832: 128 short, so the line never ended.

`XmaContext::Decode` emits each frame one frame late. ffmpeg's output for a
frame is offset by `kDecoderStartPadding` (192) samples, so the SDK keeps the
frame's last 320 samples as a carry and emits them, together with the head of
the next decoded frame, as that frame's 512 samples. The last frame of a
stream has no next frame, so its carry was never emitted. Hardware outputs
every frame in full.

## What

- `XmaContext::FlushCarry` emits a pending carry, with silence in place of the
  next frame's head, once a context's input has run out: from `Decode` when no
  input buffer is valid, and from `Work`'s consume-only path (both packet
  counts zero), which is where this voice line ends up.
- `Work` keeps decoding while a flushed frame is still being written, instead
  of stopping as soon as no input buffer is valid.

A stream whose input runs dry mid-play (the game refilling it late) also gets
its carry flushed, with 192 samples of silence, and the next frame re-primes
the carry. That only happens on an input underrun, which is a glitch already.

## Verified

`tools/to_door.keys` in lcgol-mods replays to the door. Before: Lara frozen
and the exit sequence's state machine waiting forever. After: Lara moves again
after the door opens.
