# gram

gram is a structuralist avant-garde A/V toolkit that fuses three engines
behind one deterministic pipeline — **tj**, the offline EDL mixdown core
whose tj-compatible EDL strings drive tempo/keylock snapping, arc mastering
and 48 k stereo WAV renders; **michacka**, a seven-style stochastic planner
over texture roles (ambient / motion / pulse) whose `rng` engine is
reproduced bit-exactly and whose `omicron` engine derives structure from
deterministic enumeration rather than randomness; and **OperatorOmikron**,
combinatorial operator enumeration over `a..z = 1..26` with `+ − x /`,
repurposed as the *structure driver* — every enumerated expression becomes
slice points, spans, volumes, fades and blend gestures. On top of that gram
assembles finished films: a text-scored silent video editor (`edit`, letters
pick clips by hash, in-points scatter by golden ratio), a grayscale
crop/fill picture slideshow muxed with field recordings (`slides`), a
byte-for-byte reproducible 60 s stone-oracle film (`dada`, whose committed
noise-stone is the only source of entropy, with the oracle in the `dada`
submodule), Gomotor stroke-font intertitle cards (`title`, auto-fitted or
fixed px, over black or a greyscale stone), and the transport-DSL crosscut
renderer (`partition`, a C port of the partition.sh editing language —
`open/jf/jb/pf/x/H` — that spirit-cuts sources into mpv EDL v2, MLT XML or
silent native renders, optionally standardizing each source to the output
raster). `plan`/`compose` render and master through the shared av compositor,
which binds `file` / `scope` / `wave` generators to EDL entries and turns the
operator glyph plus-minus-times-divide into additive, difference, multiply
and split-screen blends at PAL SD 932x576@25 muxed by ffmpeg; a separate
`genmontage` submodule contributes mpv-pipeline montage generation from scene
fragments.

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
              [--span S] [--max N] [--edl FILE] [--mute]
    gram slides out.mp4 --img DIR [--fld DIR] [--w W] [--h H] [--fps N]
              [--dur S] [--seed N] [--max N] [--mute]
              [--dada] [--title T] [--title-slots N] [--title-px P]
              [--stone FILE] [--dada-bin PATH]
    gram dada out.mp4 [--img DIR] [--fld DIR] [--title T] [--title-slots N]
              [--title-px P] [--stone FILE] [--dada-bin PATH] [--mute]
    gram title out.mp4 -t 'LINE' [-t ...] [-s PX] [-d SECS] [--stone FILE]
              [--w W] [--h H] [--fps N] [--mute]
    gram partition out.mp4 -p PATTERN | --script F [-i FILE ...] [--unit N]
              [--std] [--edl F] [--mlt F] [--w W] [--h H] [--fps N] [--mute]
    gram compose <style> [seed] [...same options as plan]

All commands share one flag parser, so a fixed core set is accepted
everywhere and ignored where it does not apply:

    --w N --h N --fps N   output raster
    --max N               cap on files / images / expressions (omicron: = --limit)
    --mute                silent video output (no audio mux)
    --out PATH            output path or prefix (av/edit/slides/dada: the out.mp4)
    --seed N              RNG seed

Every value flag accepts both `--flag value` and `--flag=value`, including
the render forms (`--bpm`, `--keylock`, `--master`). `av`/`edit` resolve the
video pool in the same order: `--vid` > `$GRAM_VID` > `vid=` config > default.

Styles: `day | storm | drift | pulse | rupture | strata`.
`strata` is heavy multilayering: spans derive from slot width so 3–4
tracks (music + field recordings as equal co-stars) always overlap.
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

## Slides

`slides` crop/fills JPEGs to the output resolution, converts to grayscale,
shuffles with the RNG, and muxes with field recordings as audio:

    gram slides out.mp4 --img /path/to/jpegs --seed 42

Each image is scaled proportionally to the target width, center-cropped to
height, converted to BT.601 grayscale, and held for 0.432 s (`--dur S`).
Field recordings from `--fld DIR` (or `$GRAM_FLD` / `fld=` in config) are
concatenated and muxed as audio (`--mute` for silent output).

## The dada film

`gram dada` is the committed, deterministic film: an exact 60 s run at
932x576@25 with 0.216 s slides, an opening `kof26` title drawn in the
embedded GoMotor stroke font over the noise stone, pictures from the B&W
film scans in `~/DCIM/Kinofilm`, and a dense field-mix soundtrack.

The stone image (`dada/noise.png`, committed with the `dada` oracle
submodule) is the *only* source of randomness — every choice is derived
from it, so identical inputs replay the film byte-for-byte:

- picture for slide *n* is picked from the path-sorted pool as
  `bytes(n) % poolsize` (same folder, same film; new files rerandomise);
- the soundtrack is a deterministic 12-slice EDL mix from the field
  recordings (`--fld DIR` or config), mastered and padded to 60 s
  (`--mute` for silent).

Run it as-is:

    gram dada kf_film.mp4

`slides --dada` is the same engine with the default 0.432 s slide length —
useful for previews at small resolutions (`--w 64 --h 64 --mute`).
`--title`, `--title-slots` (6), `--title-px` (12), `--stone`, `--fld`
(`> GRAM_FLD > fld=` config), `--dada-bin` (default: `exe/dada/dada`, then
`dada/dada`, then `$PATH`, overridden by `$GRAM_DADA`) tune the run.

## Partition DSL

`partition` is the C port of the partition.sh editing language — a tiny
"transport" grammar over a tape `t` that starts at `0` in the current
source and jumps or plays forward/backward, cutting exact frame spans:

- `open N` selects source `N` (0-based; the files listed with `-i`, or the
  resolved `open <path>` lines of a `--script`).
- `jf N` / `jb N` move `t` by `N` units forward/backward (no recording).
- `pf N` plays `N` units forward — records the span `t..t+N*unit` as a cut.
- `x N` repeats the preceding verb `N` times.
- `H S P` lays down `S*P` unit cuts at harmonic fractions of the source.

All lengths are counted in `unit` frames (`--unit`, default 11 = 0.44 s at
25 fps); the tape clamps to the source. Output is a silent native render, or
`--edl F` / `--mlt F` sidecars in mpv-EDL-v2 / MLT-XML form. With `--std`
each source is cover-crop/scale-normalized to the output raster, cached by
hash in `~/.cache/partition_std` (or `$XDG_CACHE_HOME/partition_std`).

## Audio/visual pipeline

`compose` scans libraries, analyzes them (cache shared in `~/.cache/tj`),
plans movements, renders pass A (music) and pass B (+field, mastered),
then optionally renders video:

- `.vedl` sidecar lines `<idx> <op> <gen>` bind visual generators to EDL
  entries: `file` = clip from the video pool paired by hash,
  `scope` = XY phosphor oscilloscope of the slice PCM,
  `wave` = envelope waveform strip. Operators become blend gestures:
  `+` additive, `-` difference, `x` multiply, `/` right-half split.
- Frames are composited at PAL SD 932x576@25 and piped to ffmpeg
  (libx264 + AAC) muxed with the part's audio.

## Config

`~/.config/gram.conf` is the central config, shared by gram and its submodules.
Override the path itself with `GRAM_CONF`. There are no built-in path defaults —
keys are required (gram dies with a clear message) so every machine names its
own libraries:

    mus=/home/kof/recordings
    fld=/mnt/data/recordings/field
    vid=/mnt/data/recordings/video8
    img=/home/kof/DCIM/Kinofilm

The michacka submodule reads `mus=`, `fld=`, and `tj=` from the same file (gram
ignores `tj=`; michacka ignores `vid=` and `img=`).

Env overrides (next after explicit flags): `GRAM_MUS`, `GRAM_FLD`, `GRAM_VID`,
`GRAM_IMG`.

## Build

    make            # bin: ./gram (also builds the dada oracle submodule)
    make test       # unit tests
    make smoke      # omicron sanity
    make smoke-av   # tiny end-to-end AV render

## Example

    gram compose drift 4242 --engine omicron --parts 1 --len 120 --max 40 --av
    # -> drift_*_part01.wav / .mp4 / .edl / .vedl / .arc + drift_mix.mp4
