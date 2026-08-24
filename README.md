# gram

Structuralist avantgarde AV toolkit — merges three instruments into one:

- **tj** — offline EDL mixdown engine (48k stereo WAV, tempo/keylock/snapping,
  arc mastering). EDL strings are fully tj-compatible.
- **michacka** — stochastic composition planner over texture roles
  (ambient / motion / pulse), five styles, movement arcs.
- **OperatorOmikron** — combinatorial operator enumeration (`a..z = 1..26`,
  ops `+ - x /`) repurposed as a *structure driver*: expressions become
  slice points, spans, volumes, fades, blend gestures.

## Commands

    gram omicron [-n N] [--reverse R] [--limit K] [--force]
    gram analyze FILE...
    gram render "<edl>" [out.wav] [--bpm auto|N] [--snap] [--keylock auto|K]
                [--arc t:g,...] [--master pop|subtle]
    gram plan <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]
              [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]
    gram av "<edl>" out.mp4 [--vedl F] [--arc t:g,...] [--vid DIR]
            [--w W] [--h H] [--fps N]
    gram edit out.mp4 [--vid DIR] [--w W] [--h H] [--fps N]
              [--span S] [--max N] [--edl FILE]
    gram compose <style> [seed] [...same options as plan]

Styles: `day | storm | drift | pulse | rupture`.
Engines: `rng` (bit-exact michacka reproduction) or `omicron`
(deterministic enumeration-driven structure, no randomness).

## Text edits

`edit` turns a string into a silent video edit — the text is the score:

    echo "this is source string" | gram edit out.mp4 --edl out.edl

Each letter a..z (= 1..26) picks one clip from the path-sorted video pool
via `(v-1) mod pool` — the same letter always lands on the same clip. Its
position among the text's letters sets the in-point inside that clip by
golden-ratio scatter, so identical input yields a byte-identical EDL.
Slices are 0.432 s (`--span S`), butt-joined continuously; non-letters
are ignored. Output is a silent MP4.

## Audio/visual pipeline

`compose` scans libraries, analyzes them (cache shared in `~/.cache/tj`),
plans movements, renders pass A (music) and pass B (+field, mastered),
then optionally renders video:

- `.vedl` sidecar lines `<idx> <op> <gen>` bind visual generators to EDL
  entries: `file` = clip from the video pool paired by hash,
  `scope` = XY phosphor oscilloscope of the slice PCM,
  `wave` = envelope waveform strip. Operators become blend gestures:
  `+` additive, `-` difference, `x` multiply, `/` right-half split.
- Frames are composited at PAL SD 720x576@25 and piped to ffmpeg
  (libx264 + AAC) muxed with the part's audio.

## Config

`~/.config/gram.conf` (falls back to `michacka.conf`):

    mus=/home/kof/recordings
    fld=/mnt/data/recordings/field
    vid=/mnt/data/recordings/video8

Env overrides: `GRAM_MUS`, `GRAM_FLD`, `GRAM_VID`.

## Build

    make            # bin: ./gram
    make test       # unit tests
    make smoke      # omicron sanity
    make smoke-av   # tiny end-to-end AV render

## Example

    gram compose drift 4242 --engine omicron --parts 1 --len 120 --max 40 --av
    # -> drift_*_part01.wav / .mp4 / .edl / .vedl / .arc + drift_mix.mp4
