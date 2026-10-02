#!/usr/bin/env bash
# gram all-features demo — ~5 minutes, intertitled, rendered into ./out/
#
# Every segment is produced by a gram command (or its genmontage submodule),
# normalized to 932x576@25 + 48 k stereo AAC, then concatenated into
# out/demo.mp4. Outputs use the local libraries (overriding the central
# config via $GRAM_* env) so the demo needs no other machine.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/out"
mkdir -p "$OUT"

export GRAM_MUS="${GRAM_MUS:-$HOME/Music}"
export GRAM_FLD="${GRAM_FLD:-$HOME/recordings}"
export GRAM_VID="${GRAM_VID:-$HOME/Videos}"
export GRAM_IMG="${GRAM_IMG:-$HOME/DCIM/Camera}"
V="$GRAM_VID"

GRAM="$ROOT/gram"
W=932; H=576; FPS=25

step() { printf '\n=== %s ===\n' "$*"; }

# bed: add a 48k stereo AAC bed to a video-only mp4 (stream-copies video)
bed() {
    local src="$1" dst="$2"
    ffmpeg -y -v error -i "$src" -f lavfi -i anullsrc=r=48000:cl=stereo \
        -shortest -c:v copy -c:a aac -b:a 128k "$dst"
}

step "intro"
"$GRAM" title "$OUT/it_intro.mp4" -d 8 \
    -t 'GRAM' -t 'STRUCTURALIST AVANTGARDE TOOLKIT' >/dev/null

step "compose — omicron engine (30s)"
"$GRAM" title "$OUT/it_compose.mp4" -d 4 \
    -t '1 — COMPOSE' -t 'OMICRON ENGINE' -t 'STORM' >/dev/null
"$GRAM" compose storm 4242 --engine omicron --parts 1 --len 30 \
    --out "$OUT/c_omicron" --av --max 150 >/dev/null

step "compose — rng engine (30s)"
"$GRAM" title "$OUT/it_plan.mp4" -d 4 \
    -t '2 — PLAN' -t 'RNG ENGINE' -t 'RENDER + MASTER' >/dev/null
"$GRAM" compose drift 777 --parts 1 --len 30 \
    --out "$OUT/c_rng" --av --max 150 >/dev/null

step "render — EDL to 48k stereo bed (30s)"
"$GRAM" title "$OUT/it_render.mp4" -d 4 \
    -t '3 — RENDER' -t 'EDL INTO A 48K STEREO BED' >/dev/null
MUS=""
for f in "$GRAM_MUS"/*; do
    [ -f "$f" ] || continue
    d=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$f" 2>/dev/null || echo 0)
    if awk -v d="$d" 'BEGIN { exit !(d > 32) }'; then MUS="$f"; break; fi
done
if [ -z "$MUS" ]; then echo "demo: no >=32s track in $GRAM_MUS"; exit 1; fi
printf 'in0 out32 at0 %s' "$MUS" > "$OUT/r.edl"
printf '1 + wave\n' > "$OUT/r.vedl"
"$GRAM" render "$(cat "$OUT/r.edl")" "$OUT/r_bed.wav" --master pop >/dev/null
cp "$OUT/r_bed.wav" "$OUT/r_visual_audio.wav"
"$GRAM" av "$(cat "$OUT/r.edl")" "$OUT/r_visual.mp4" --vedl "$OUT/r.vedl" >/dev/null

step "edit — text-scored video (~36s)"
"$GRAM" title "$OUT/it_edit.mp4" -d 4 \
    -t '4 — EDIT' -t 'TEXT IS THE SCORE' >/dev/null
EDIT_TEXT="gram is a structuralist toolkit every letter picks a clip the position sets the inpoint the cut is the sentence structure repeats"
printf '%s' "$EDIT_TEXT" | "$GRAM" edit "$OUT/e_edit.mp4" --mute \
    --max 40 --edl "$OUT/e_edit.edl" >/dev/null
bed "$OUT/e_edit.mp4" "$OUT/e_edit_bed.mp4"

step "slides — grayscale camera (40s)"
"$GRAM" title "$OUT/it_slides.mp4" -d 4 \
    -t '5 — SLIDES' -t 'GRAYSCALE CAMERA' >/dev/null
"$GRAM" slides "$OUT/s_slides.mp4" --img "$GRAM_IMG" --dur 0.4 \
    --max 100 --seed 7 --mute >/dev/null
bed "$OUT/s_slides.mp4" "$OUT/s_slides_bed.mp4"

step "dada — the stone-oracle film (60s)"
"$GRAM" title "$OUT/it_dada.mp4" -d 4 \
    -t '6 — DADA' -t 'THE STONE ORACLE FILM' >/dev/null
"$GRAM" dada "$OUT/d_dada.mp4" >/dev/null

step "partition — transport DSL crosscut (~26s)"
"$GRAM" title "$OUT/it_partition.mp4" -d 4 \
    -t '7 — PARTITION' -t 'TRANSPORT DSL CROSSCUT' >/dev/null
"$GRAM" partition "$OUT/p_partition.mp4" \
    -p 'pf 20 pf 7 x 4 H 3 3' \
    -i "$V/seventh_edit.mp4" -i "$V/kf_film.mp4" >/dev/null
bed "$OUT/p_partition.mp4" "$OUT/p_partition_bed.mp4"

step "genmontage — pool montage engine (~18s)"
"$GRAM" title "$OUT/it_genmontage.mp4" -d 4 \
    -t '8 — GENMONTAGE' -t 'POOL MONTAGE ENGINE' >/dev/null
GW="$OUT/gen_work"
rm -rf "$GW"; mkdir -p "$GW/pool"
GM_POOL=(
    "$V/seventh_edit.mp4" "$V/drkota26.mp4" "$V/kf_film.mp4"
    "$V/Vlna_07z100_2026.mp4" "$V/20260813_195520_diffrec.mp4"
    "$V/20260813_223918_diffrec.mp4" "$V/The_Seventh_Seal-1957.mp4"
)
i=0
for f in "${GM_POOL[@]}"; do
    [ -f "$f" ] || { echo "demo: missing pool clip $f"; exit 1; }
    cp "$f" "$GW/pool/cut_$(printf '%03d' "$i").mp4"
    i=$((i + 1))
done
( cd "$GW" && "$ROOT/genmontage/genmontage" -c 30 -f 18 > gen.log 2>&1 )
cat > "$GW/montage.awk" <<'AWK'
# mpv EDL v0 -> MLT timeline (entries laid down consecutively)
BEGIN { nprod = 0; nerr = 0; }
/^pool\// {
    n = split($0, a, ",");
    p = a[1];
    if (!(p in id)) { id[p] = nprod; pname[nprod] = p; nprod++; }
    eseq[nerr] = p;
    st[nerr] = a[2] + 0;
    en[nerr] = a[2] + a[3];
    nerr++;
}
END {
    for (k = 0; k < nprod; k++)
        printf "  <producer id=\"p%d\" in=\"0\" out=\"1000000\"><property name=\"resource\">%s</property></producer>\n", k, pname[k];
    printf "  <playlist id=\"main\">\n";
    dur = 0;
    for (i = 0; i < nerr; i++) {
        bi = int(st[i] * FPS + 0.5);
        bj = int(en[i] * FPS + 0.5);
        if (bj <= bi) continue;
        printf "    <entry producer=\"p%d\" in=\"%d\" out=\"%d\"/>\n", id[eseq[i]], bi, bj;
        dur += bj - bi;
    }
    printf "  </playlist>\n";
    printf "  <tractor id=\"t0\" in=\"0\" out=\"%d\"><track producer=\"main\"/></tractor>\n", (dur > 1 ? dur - 1 : 0);
}
AWK
awk -v FPS="$FPS" -f "$GW/montage.awk" "$GW/montage.edl" > "$GW/montage_body.xml"
{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n'
    printf '<mlt LC_NUMERIC="C" version="7.40.0" root="%s">\n' "$GW"
    printf '  <profile description="structural" width="%d" height="%d" progressive="1" sample_aspect_num="1" sample_aspect_den="1" display_aspect_num="932" display_aspect_den="576" frame_rate_num="25" frame_rate_den="1" colorspace="709"/>\n' "$W" "$H"
    cat "$GW/montage_body.xml"
    printf '</mlt>\n'
} > "$GW/montage.mlt"
melt "$GW/montage.mlt" -consumer avformat:"$OUT/g_montage.mp4" 2>/dev/null \
    >/dev/null
bed "$OUT/g_montage.mp4" "$OUT/g_montage_bed.mp4"

step "closing"
"$GRAM" title "$OUT/it_end.mp4" -d 8 -t 'END' >/dev/null

step "concatenate to out/demo.mp4"
FRAGS=(
    "$OUT/it_intro.mp4" "$OUT/it_compose.mp4" "$OUT/c_omicron_part01.mp4"
    "$OUT/it_plan.mp4" "$OUT/c_rng_part01.mp4"
    "$OUT/it_render.mp4" "$OUT/r_visual.mp4"
    "$OUT/it_edit.mp4" "$OUT/e_edit_bed.mp4"
    "$OUT/it_slides.mp4" "$OUT/s_slides_bed.mp4"
    "$OUT/it_dada.mp4" "$OUT/d_dada.mp4"
    "$OUT/it_partition.mp4" "$OUT/p_partition_bed.mp4"
    "$OUT/it_genmontage.mp4" "$OUT/g_montage_bed.mp4"
    "$OUT/it_end.mp4"
)
ARGS=()
FILTER=""
FCIN=""
N=${#FRAGS[@]}
for ((i = 0; i < N; i++)); do
    ARGS+=(-i "${FRAGS[$i]}")
    FILTER+="[$i:v]scale=$W:$H:force_original_aspect_ratio=increase,"
    FILTER+="crop=$W:$H,fps=$FPS,format=yuv420p,setpts=PTS-STARTPTS[v$i];"
    FILTER+="[$i:a]aresample=48000,aformat=channel_layouts=stereo,"
    FILTER+="asetpts=PTS-STARTPTS[a$i];"
    FCIN+="[v$i][a$i]"
done
FILTER+="${FCIN}concat=n=$N:v=1:a=1[v][a]"
ffmpeg -y -v error "${ARGS[@]}" -filter_complex "$FILTER" \
    -map "[v]" -map "[a]" -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
    -c:a aac -b:a 192k -ar 48000 "$OUT/demo.mp4"

echo
echo "demo done: $OUT/demo.mp4"
ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$OUT/demo.mp4" | \
    awk '{ printf "   duration %.0f s (%d:%02d)\n", $1, $1/60, $1%60 }'