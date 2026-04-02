"""
Step 1 – Download the E-GMD dataset zip and extract audio + MIDI files.

E-GMD:  Expanded Groove MIDI Dataset  (Google Magenta)
        License : CC BY 4.0
        Zip size : ~90 GB compressed
        Extracted : ~132 GB  (audio + MIDI)
        Peak disk during extraction : ~222 GB  (zip + extracted simultaneously)
        Final disk after zip deleted : ~132 GB

Individual files are NOT separately downloadable – only the full ZIP is offered.
This script downloads with resume support so a dropped connection is not fatal.

FRACTION config:
  1.00 → extract all  45,537 recordings  (~132 GB)
  0.10 → extract  10 %,  4,554 recordings  (~ 14 GB)   ← quick experiment
"""

import sys
import zipfile
from pathlib import Path

import pandas as pd
import requests
from tqdm import tqdm

# ── Config ────────────────────────────────────────────────────────────────────
BASE_URL  = "https://storage.googleapis.com/magentadata/datasets/e-gmd/v1.0.0"
ZIP_URL   = f"{BASE_URL}/e-gmd-v1.0.0.zip"
META_URL  = f"{BASE_URL}/e-gmd-v1.0.0.csv"
DATA_DIR  = Path("data/egmd")

FRACTION  = 1.00   # 1.00 = full dataset  |  0.10 = quick 10 % subset
SEED      = 42
DELETE_ZIP_AFTER_EXTRACTION = True   # saves ~90 GB once extraction is done


# ── Helpers ───────────────────────────────────────────────────────────────────
def download_with_resume(url: str, dest: Path) -> None:
    """
    Download a large file with:
      • HTTP Range resume  (safe to Ctrl-C and restart)
      • tqdm progress bar showing speed + ETA
    """
    dest.parent.mkdir(parents=True, exist_ok=True)

    # Check how much we already have
    existing = dest.stat().st_size if dest.exists() else 0

    headers = {"Range": f"bytes={existing}-"} if existing > 0 else {}
    r = requests.get(url, headers=headers, stream=True, timeout=60)

    # 206 = server supports resume; 200 = full file (restart)
    if r.status_code == 200 and existing > 0:
        print("  Server does not support resume – restarting download.", flush=True)
        existing = 0

    if r.status_code not in (200, 206):
        r.raise_for_status()

    total = int(r.headers.get("content-length", 0)) + existing
    mode  = "ab" if existing > 0 else "wb"

    if existing > 0:
        print(f"  Resuming from {existing / 1e9:.2f} GB …", flush=True)

    with open(dest, mode) as f, tqdm(
        total=total, initial=existing,
        unit="B", unit_scale=True, unit_divisor=1024,
        desc=dest.name,
    ) as pbar:
        for chunk in r.iter_content(chunk_size=1 << 20):   # 1 MB chunks
            f.write(chunk)
            pbar.update(len(chunk))


def extract_subset(zip_path: Path, members: set[str], extract_to: Path) -> None:
    """
    Extract only the files listed in `members` from the zip.
    Shows a tqdm bar based on number of files extracted.
    """
    extract_to.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(zip_path, "r") as zf:
        all_names = set(zf.namelist())

        # Filter to only what we need, skip already-extracted files
        todo = []
        for m in members:
            dest = extract_to / m
            if not dest.exists():
                if m in all_names:
                    todo.append(m)
                else:
                    print(f"  ⚠  Not found in zip: {m}", flush=True)

        if not todo:
            print("  All files already extracted – nothing to do.", flush=True)
            return

        print(f"  Extracting {len(todo):,} files …", flush=True)
        for name in tqdm(todo, unit="file"):
            zf.extract(name, extract_to)


# ── Main ──────────────────────────────────────────────────────────────────────
def main() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Metadata CSV ──────────────────────────────────────────────────────────
    meta_path = DATA_DIR / "e-gmd-v1.0.0.csv"
    if not meta_path.exists():
        print("Downloading metadata CSV (small) …", flush=True)
        r = requests.get(META_URL, timeout=30)
        r.raise_for_status()
        meta_path.write_bytes(r.content)
        print(f"  Saved → {meta_path}", flush=True)

    df = pd.read_csv(meta_path)
    total_h = df["duration"].sum() / 3600
    print(f"E-GMD: {len(df):,} recordings  ({total_h:.0f} h  across"
          f" {df['drummer'].nunique()} drummers)", flush=True)

    # 2. Select subset ─────────────────────────────────────────────────────────
    if FRACTION >= 1.0:
        sampled = df.copy()
        subset_label = "full"
    else:
        sampled = (
            df.groupby("split", group_keys=False)
              .apply(lambda g: g.sample(frac=FRACTION, random_state=SEED))
              .reset_index(drop=True)
        )
        subset_label = f"{int(FRACTION * 100)}pct"

    subset_h = sampled["duration"].sum() / 3600
    print(f"Selected: {len(sampled):,} recordings  ({subset_h:.1f} h)  [{subset_label}]",
          flush=True)
    sampled.to_csv(DATA_DIR / f"metadata_{subset_label}.csv", index=False)

    # Build the set of zip member paths we need to extract
    members: set[str] = set()
    for _, row in sampled.iterrows():
        members.add(row["audio_filename"])
        members.add(row["midi_filename"])

    # 3. Download ZIP ──────────────────────────────────────────────────────────
    zip_path = DATA_DIR / "e-gmd-v1.0.0.zip"

    # Skip download if all files already extracted
    already_extracted = all((DATA_DIR / m).exists() for m in list(members)[:20])
    if already_extracted and not zip_path.exists():
        print("Files already extracted – skipping download.", flush=True)
    else:
        if zip_path.exists():
            size_gb = zip_path.stat().st_size / 1e9
            print(f"Zip already present ({size_gb:.1f} GB) – will resume if incomplete.",
                  flush=True)
        else:
            est_gb = 90 if FRACTION >= 1.0 else 90 * FRACTION
            print(f"\nDownloading E-GMD zip  (~{est_gb:.0f} GB) …", flush=True)
            print("  You can safely Ctrl-C and restart – download will resume.",
                  flush=True)

        download_with_resume(ZIP_URL, zip_path)
        print(f"  Zip complete: {zip_path.stat().st_size / 1e9:.1f} GB", flush=True)

    # 4. Extract ───────────────────────────────────────────────────────────────
    print(f"\nExtracting {len(members):,} files from zip …", flush=True)
    extract_subset(zip_path, members, DATA_DIR)

    # 5. Optionally delete zip to free ~90 GB ─────────────────────────────────
    if DELETE_ZIP_AFTER_EXTRACTION and zip_path.exists():
        print(f"\nDeleting zip to free {zip_path.stat().st_size / 1e9:.1f} GB …",
              flush=True)
        zip_path.unlink()
        print("  Zip deleted.", flush=True)

    print(f"\nDownload + extraction complete.", flush=True)
    print(f"Data directory: {DATA_DIR.resolve()}", flush=True)


if __name__ == "__main__":
    main()
