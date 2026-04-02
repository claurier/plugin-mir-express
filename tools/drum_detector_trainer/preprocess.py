"""
Step 2 – Compute mel spectrograms and onset labels from E-GMD audio/MIDI pairs.

For each recording:
  • Load WAV at 44.1 kHz, mix to mono.
  • Compute 40-band log-mel spectrogram, hop = 441 samples (10 ms).
  • Parse MIDI → extract kick / snare / hi-hat onset frames.
  • Mark ±ONSET_WINDOW frames around each onset as positive.
  • Save compressed .npz with keys  mel=[N,40]  labels=[N,3].

After processing, compute per-band mean and std on the training split
and save them as  data/features/mel_mean.npy  /  mel_std.npy  (needed by
train_export.py for feature normalisation AND by the plugin for inference).
"""

import sys
import warnings
import numpy as np
import pandas as pd
from pathlib import Path
from functools import partial
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

import torch
import torchaudio
import torchaudio.transforms as T
import mido

warnings.filterwarnings("ignore")

# ── Constants (MUST match model.py and the C++ plugin exactly) ─────────────────
SAMPLE_RATE   = 44100
HOP_SAMPLES   = 441      # 10 ms
FFT_SIZE      = 1024
MEL_BANDS     = 40
F_MIN         = 20.0
F_MAX         = 8000.0
ONSET_WINDOW  = 2        # ±2 frames (50 ms tolerance window around each onset)

# General MIDI drum note → class index
# 0 = kick  |  1 = snare  |  2 = hi-hat
DRUM_MAP: dict[int, int] = {
    35: 0, 36: 0,           # Bass Drum 2 / 1
    38: 1, 40: 1,           # Acoustic Snare / Electric Snare
    37: 1,                  # Side Stick (treated as snare)
    42: 2, 44: 2, 46: 2,    # Closed / Pedal / Open Hi-Hat
}

DATA_DIR = Path("data/egmd")
FEAT_DIR = Path("data/features")
N_WORKERS = min(8, __import__("os").cpu_count() or 4)


# ── Mel spectrogram builder (constructed once per thread) ─────────────────────
_mel_transform: T.MelSpectrogram | None = None
_db_transform:  T.AmplitudeToDB  | None = None

def _get_transforms():
    global _mel_transform, _db_transform
    if _mel_transform is None:
        _mel_transform = T.MelSpectrogram(
            sample_rate=SAMPLE_RATE, n_fft=FFT_SIZE, hop_length=HOP_SAMPLES,
            n_mels=MEL_BANDS, f_min=F_MIN, f_max=F_MAX,
            power=2.0, norm="slaney", mel_scale="slaney",
        )
        _db_transform = T.AmplitudeToDB(stype="power", top_db=80.0)
    return _mel_transform, _db_transform


def compute_mel(wav_path: Path) -> np.ndarray:
    """Returns log-mel spectrogram [N_frames, MEL_BANDS], float32."""
    waveform, sr = torchaudio.load(str(wav_path))
    if sr != SAMPLE_RATE:
        waveform = torchaudio.functional.resample(waveform, sr, SAMPLE_RATE)
    waveform = waveform.mean(0, keepdim=True)   # stereo → mono [1, T]

    mel_t, db_t = _get_transforms()
    with torch.no_grad():
        mel = mel_t(waveform)       # [1, MEL_BANDS, N_frames]
        log_mel = db_t(mel)         # dB scale, clipped to top_db
    return log_mel[0].T.numpy().astype(np.float32)   # [N_frames, MEL_BANDS]


def parse_midi(midi_path: Path, n_frames: int) -> np.ndarray:
    """
    Returns float32 label array [n_frames, 3].
    Frames within ±ONSET_WINDOW of a drum onset are set to 1.0.
    """
    labels = np.zeros((n_frames, 3), dtype=np.float32)
    try:
        mid    = mido.MidiFile(str(midi_path))
        tpb    = mid.ticks_per_beat
        tempo  = 500_000   # default: 120 BPM

        for track in mid.tracks:
            cur_tick = 0
            cur_sec  = 0.0
            tempo    = 500_000
            for msg in track:
                dt_sec   = mido.tick2second(msg.time, tpb, tempo)
                cur_tick += msg.time
                cur_sec  += dt_sec

                if msg.type == "set_tempo":
                    tempo = msg.tempo

                if msg.type == "note_on" and msg.velocity > 0:
                    cls = DRUM_MAP.get(msg.note)
                    if cls is None:
                        continue
                    frame = int(cur_sec * SAMPLE_RATE / HOP_SAMPLES)
                    lo = max(0, frame - ONSET_WINDOW)
                    hi = min(n_frames, frame + ONSET_WINDOW + 1)
                    labels[lo:hi, cls] = 1.0
    except Exception:
        pass   # return all-zero labels on parse error
    return labels


# ── Per-file processing ───────────────────────────────────────────────────────
def process_one(row: pd.Series) -> dict:
    audio_path = DATA_DIR / row["audio_filename"]
    midi_path  = DATA_DIR / row["midi_filename"]
    split      = row["split"]
    stem       = Path(row["audio_filename"]).stem

    out_path = FEAT_DIR / split / f"{stem}.npz"
    if out_path.exists():
        return {"ok": True, "frames": 0, "skipped": True}

    out_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        mel    = compute_mel(audio_path)
        labels = parse_midi(midi_path, len(mel))
        # Store mel as float16 to halve disk/RAM usage (~14 GB for the full dataset).
        # Precision loss is negligible: log-mel values span ~80 dB and float16
        # gives ~3 decimal places of precision, well within the noise floor.
        np.savez_compressed(str(out_path),
                            mel=mel.astype(np.float16),
                            labels=labels)
        return {"ok": True, "frames": len(mel), "skipped": False}
    except Exception as exc:
        return {"ok": False, "frames": 0, "error": str(exc), "file": str(audio_path)}


# ── Main ──────────────────────────────────────────────────────────────────────
def _find_metadata(data_dir: Path) -> Path:
    """Pick whichever metadata subset file the download step created."""
    for name in ("metadata_full.csv", "metadata_10pct.csv",
                 "e-gmd-v1.0.0.csv", "metadata.csv"):
        p = data_dir / name
        if p.exists():
            return p
    raise FileNotFoundError(
        f"No metadata CSV found in {data_dir}.\nRun download.py first."
    )


def main():
    FEAT_DIR.mkdir(parents=True, exist_ok=True)

    meta_path = _find_metadata(DATA_DIR)
    print(f"Using metadata: {meta_path.name}")
    df = pd.read_csv(meta_path)
    rows = [row for _, row in df.iterrows()]
    print(f"Preprocessing {len(rows):,} recordings  ({N_WORKERS} threads)...")

    ok = fail = skip = 0
    total_frames = 0
    errors = []

    with ThreadPoolExecutor(max_workers=N_WORKERS) as pool:
        futures = {pool.submit(process_one, row): row for row in rows}
        with tqdm(total=len(rows), unit="file") as pbar:
            for fut in as_completed(futures):
                res = fut.result()
                if res["ok"]:
                    if res.get("skipped"):
                        skip += 1
                    else:
                        ok += 1
                        total_frames += res["frames"]
                else:
                    fail += 1
                    errors.append(res.get("file", "?") + " – " + res.get("error", ""))
                pbar.update(1)
                pbar.set_postfix(ok=ok, fail=fail, skip=skip)

    print(f"\nProcessed:  {ok:,} OK  •  {skip:,} skipped  •  {fail:,} failed")
    print(f"Total frames: {total_frames:,}  ({total_frames / 100 / 3600:.1f} h at 100 fps)")

    if errors:
        print("\nFailed files:")
        for e in errors[:10]:
            print(f"  {e}")

    # ── Global normalisation stats (training split only) ──────────────────────
    print("\nComputing per-band mean and std (on training split) ...")
    train_files = sorted((FEAT_DIR / "train").glob("*.npz"))
    # Sample 5 % for speed (still thousands of files)
    sample = train_files[:: max(1, len(train_files) // max(1, len(train_files) // 20))]
    sample = sample[:500]   # cap at 500 files

    accum = []
    for p in tqdm(sample, unit="file", desc="Stats"):
        accum.append(np.load(str(p))["mel"])   # [N, 40]

    all_mel = np.concatenate(accum, axis=0)    # [N_total, 40]
    mean = all_mel.mean(axis=0).astype(np.float32)
    std  = (all_mel.std(axis=0) + 1e-8).astype(np.float32)

    np.save(str(FEAT_DIR / "mel_mean.npy"), mean)
    np.save(str(FEAT_DIR / "mel_std.npy"),  std)

    print(f"Saved mel_mean.npy  and  mel_std.npy  →  {FEAT_DIR}")
    print(f"Mean range : [{mean.min():.2f}, {mean.max():.2f}]")
    print(f"Std  range : [{std.min():.2f},  {std.max():.2f}]")


if __name__ == "__main__":
    main()
