#!/usr/bin/env bash
# mix_last_month.sh - rapid black-and-white slideshow of last month's
# pictures over last month's field recordings, rendered by `gram slides`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRAM="${GRAM:-$SCRIPT_DIR/../gram}"
W="${W:-932}"
H="${H:-576}"
FPS="${FPS:-25}"
FRAMES_PER_SLIDE="${FRAMES_PER_SLIDE:-3}"
MAX_SECONDS="${MAX_SECONDS:-180}"
IMG_DIRS="${IMG_DIRS:-$HOME/DCIM/Camera $HOME/DCIM/OpenCamera}"
FLD_DIR="${FLD_DIR:-$HOME/MUSIC}"
FADE_IN="${FADE_IN:-1}"
FADE_OUT="${FADE_OUT:-2}"

MONTH=""
OUT=""
SEED=""
MUTE=0
KEEP=0
DRY=0

usage() {
  cat <<EOF
usage: mix_last_month.sh [-m YYYYMM] [-o out.mp4] [-s SEED] [--mute] [--keep] [-n]

Rapid black-and-white slideshow of last month's pictures over last month's
field recordings, rendered by \`gram slides\`.

  -m YYYYMM   month to mix (default: last month)
  -o FILE     output mp4 (default: \$HOME/Videos/last_month_YYYYMM_bw.mp4)
  -s SEED     picture order seed (default: taken from the clock)
  --mute      no audio
  --keep      keep the staging directory
  -n          dry run: print the plan and the gram command, render nothing

gram slides takes one picture directory, so the month's pictures are staged
from every source folder as symlinks (merged, chronological) and handed over
as a single pool. gram crop/fills them to the output raster, converts them to
grayscale and shuffles them with the seed.

Caps: at most FRAMES_PER_SLIDE frames per slide (3 = --dur 0.12 at 25 fps)
and at most MAX_SECONDS of film (180); the pool is trimmed to the seconds
cap before rendering, so the encoder never runs past it. When the month holds
fewer pictures than the cap allows - the usual case - every picture gets
exactly FRAMES_PER_SLIDE frames and the film is as long as the pool allows.
Raise FRAMES_PER_SLIDE (env) to fill more of the three minutes.

Audio: the month's field recordings (YYMMDD_NNNN.wav in FLD_DIR) are cut into
one equal slice each at a fixed offset into the file (the head of a field take
is usually handling noise) and concatenated into a bed exactly as long as the
picture track, faded in and out. gram muxes it with -shortest, so picture and
sound always end together.

Environment overrides: GRAM, IMG_DIRS, FLD_DIR, W, H, FPS, FRAMES_PER_SLIDE,
MAX_SECONDS, FADE_IN, FADE_OUT.
EOF
}

die() { echo "mix_last_month: $*" >&2; exit 1; }
need_val() { [ "$2" -ge 2 ] || die "option $1 needs a value"; }

while [ $# -gt 0 ]; do
  case "$1" in
    -m) need_val "$1" $#; MONTH="$2"; shift 2 ;;
    -m*) MONTH="${1#-m}"; shift ;;
    -o) need_val "$1" $#; OUT="$2"; shift 2 ;;
    -o*) OUT="${1#-o}"; shift ;;
    -s) need_val "$1" $#; SEED="$2"; shift 2 ;;
    -s*) SEED="${1#-s}"; shift ;;
    --mute) MUTE=1; shift ;;
    --keep) KEEP=1; shift ;;
    -n|--dry-run) DRY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unexpected argument: $1" ;;
  esac
done

[ -n "$MONTH" ] || MONTH="$(date -d 'last month' +%Y%m)"
[[ "$MONTH" =~ ^[0-9]{6}$ ]] || die "month must be YYYYMM, got: $MONTH"
YM="${MONTH:0:4}${MONTH:4:2}"
YYMM="${MONTH:2:4}"
[ -n "$OUT" ] || OUT="$HOME/Videos/last_month_${MONTH}_bw.mp4"
[ -x "$GRAM" ] || die "gram binary not found or not executable: $GRAM"

DUR="$(awk -v f="$FRAMES_PER_SLIDE" -v fps="$FPS" 'BEGIN{printf "%.6f", f/fps}')"
MAX_SLIDES="$(awk -v s="$MAX_SECONDS" -v fps="$FPS" -v f="$FRAMES_PER_SLIDE" \
  'BEGIN{n=int(s*fps/f); print (n>0?n:1)}')"

echo "mix_last_month: month $MONTH, ${FRAMES_PER_SLIDE} frames/slide (${DUR}s)," \
     "cap ${MAX_SECONDS}s = ${MAX_SLIDES} slides, ${W}x${H}@${FPS}"

# ----------------------------------------------------------------- pictures
# basenames of the form IMG_YYYYMDMG_HHMMSS... sort chronologically and both
# camera folders use the same pattern, so one merged sort is the month in
# order. A symlink per picture, numbered, forms the gram pool.
POOL="$(mktemp)"
for dir in $IMG_DIRS; do
  [ -d "$dir" ] || { echo "mix_last_month: no such picture folder: $dir" >&2; continue; }
  for f in "$dir"/IMG_"$YM"*.jpg "$dir"/IMG_"$YM"*.jpeg; do
    [ -f "$f" ] || continue
    printf '%s\t%s\n' "$(basename "$f")" "$f" >> "$POOL"
  done
done
sort -o "$POOL" "$POOL"

NPICS="$(wc -l < "$POOL")"
[ "$NPICS" -gt 0 ] || die "no pictures for $MONTH in: $IMG_DIRS"
NSAVED=$(( NPICS > MAX_SLIDES ? MAX_SLIDES : NPICS ))
echo "mix_last_month: $NPICS picture(s) of $MONTH, using $NSAVED"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/mix_last_month.XXXXXX")"
mkdir -p "$STAGE/img" "$STAGE/fld"
cleanup() {
  [ "$KEEP" -eq 1 ] || rm -rf "$STAGE"
  rm -f "$POOL"
}
trap cleanup EXIT

n=0
while IFS=$'\t' read -r name path; do
  n=$(( n + 1 ))
  printf -v link '%s/img/%04d_%s' "$STAGE" "$n" "$name"
  ln -s "$path" "$link"
  [ "$n" -ge "$NSAVED" ] && break
done < "$POOL"

FILM_SECS="$(awk -v n="$NSAVED" -v f="$FRAMES_PER_SLIDE" -v fps="$FPS" \
  'BEGIN{printf "%.3f", n*f/fps}')"
echo "mix_last_month: picture track ${FILM_SECS}s ($NSAVED slides x $FRAMES_PER_SLIDE frames)"

# ------------------------------------------------------------------- audio
# One equal slice per field recording of the month, taken a third of the way
# into the file, so the concatenation is exactly FILM_SECS long and no take
# starts on its own head.
BED=""
if [ "$MUTE" -eq 0 ]; then
  shopt -s nullglob
  FLD_FILES=("$FLD_DIR/$YYMM"*.wav)
  shopt -u nullglob
  [ "${#FLD_FILES[@]}" -gt 0 ] || die "no field recordings ($YYMM*.wav) in $FLD_DIR"
  echo "mix_last_month: ${#FLD_FILES[@]} field recording(s) of $MONTH from $FLD_DIR"

  SHARE="$(awk -v t="$FILM_SECS" -v n="${#FLD_FILES[@]}" 'BEGIN{printf "%.3f", t/n}')"
  LIST="$STAGE/bed.txt"
  : > "$LIST"
  k=0
  for f in "${FLD_FILES[@]}"; do
    fdur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")"
    if ! awk -v d="$fdur" -v s="$SHARE" 'BEGIN{exit !(d >= s + 1)}'; then
      echo "mix_last_month: skipping short take $(basename "$f") (${fdur}s)" >&2
      continue
    fi
    off="$(awk -v d="$fdur" -v s="$SHARE" 'BEGIN{v=(d-s)*0.35; if (v<0) v=0; printf "%.3f", v}')"
    k=$(( k + 1 ))
    part="$(printf '%s/bed%02d.wav' "$STAGE" "$k")"
    ffmpeg -v error -y -ss "$off" -t "$SHARE" -i "$f" \
      -ar 48000 -ac 2 -c:a pcm_s16le "$part"
    printf "file '%s'\n" "$part" >> "$LIST"
  done
  [ "$k" -gt 0 ] || die "every field recording of $MONTH is shorter than ${SHARE}s"

  fade_at="$(awk -v t="$FILM_SECS" -v f="$FADE_OUT" 'BEGIN{v=t-f; if (v<0) v=0; printf "%.3f", v}')"
  BED="$STAGE/bed.wav"
  ffmpeg -v error -y -f concat -safe 0 -i "$LIST" \
    -af "afade=t=in:st=0:d=$FADE_IN,afade=t=out:st=$fade_at:d=$FADE_OUT" \
    -ar 48000 -ac 2 -c:a pcm_s16le "$BED"
  echo "mix_last_month: bed ${SHARE}s x $k take(s) = $(ffprobe -v error \
    -show_entries format=duration -of csv=p=0 "$BED")s"
fi

# ------------------------------------------------------------------ render
mkdir -p "$(dirname "$OUT")"
ARGS=(slides "$OUT" --img "$STAGE/img" --w "$W" --h "$H" --fps "$FPS" --dur "$DUR")
[ -n "$SEED" ] && ARGS+=(--seed "$SEED")
if [ -n "$BED" ]; then
  ARGS+=(--fld "$STAGE/fld")
  cp -f "$BED" "$STAGE/fld/00_bed.wav"
else
  ARGS+=(--mute)
fi

printf '%q ' "$GRAM" "${ARGS[@]}"; echo
if [ "$DRY" -eq 1 ]; then
  KEEP=1
  echo "mix_last_month: dry run, nothing rendered (stage: $STAGE)"
  exit 0
fi

"$GRAM" "${ARGS[@]}"

echo "mix_last_month: $(ffprobe -v error -show_entries format=duration \
  -of csv=p=0 "$OUT")s -> $OUT"