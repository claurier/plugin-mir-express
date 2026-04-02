"""
Step 3 – Train DrumDetector and export to ONNX.

Training strategy:
  • BCEWithLogitsLoss  with per-class pos_weight to handle onset sparsity.
  • WeightedRandomSampler so each mini-batch contains ~50 % onset frames.
  • AdamW + ExponentialLR decay.
  • Best checkpoint (highest mean F1 on validation) → saved as .pth.

ONNX export:
  • Adds Sigmoid to the model so the plugin receives probabilities [0, 1].
  • Static input shape [1, 1, 15, 40] — optimal for ONNX Runtime.
  • Companion normalization.json carries the per-band mean/std the plugin
    must apply before inference.

Output files (copy both to the plugin resources):
  models/drum_detector.onnx
  models/normalization.json
"""

import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from tqdm import tqdm

from model import DrumDetector, CONTEXT, MEL_BANDS, N_CLASSES

# ── Paths ─────────────────────────────────────────────────────────────────────
FEAT_DIR  = Path("data/features")
MODEL_DIR = Path("models")

# ── Hyperparameters ───────────────────────────────────────────────────────────
EPOCHS      = 30       # more data → a few extra epochs for full convergence
BATCH_SIZE  = 2048    # large batch — 4090 has plenty of VRAM
LR_INIT     = 1e-3
LR_GAMMA    = 0.90    # ExponentialLR: lr *= 0.90 each epoch
POS_WEIGHT  = 8.0     # positive frame up-weighting (onsets are ~10-15 % of frames)
THRESHOLD   = 0.40    # sigmoid threshold for binary F1 eval
NUM_WORKERS = 4       # DataLoader workers (Windows: keep ≤ 4)

DEVICE = "cuda" if torch.cuda.is_available() else "cpu"


# ── Dataset ───────────────────────────────────────────────────────────────────
class DrumDataset(Dataset):
    """
    Loads pre-computed mel features and onset labels into RAM.
    Each sample is a (mel_patch [1,15,40], label [3]) pair.
    """

    def __init__(self, feat_dir: Path, split: str):
        mean = np.load(str(feat_dir / "mel_mean.npy"))   # [MEL_BANDS]
        std  = np.load(str(feat_dir / "mel_std.npy"))

        npz_files = sorted((feat_dir / split).glob("*.npz"))
        if not npz_files:
            raise FileNotFoundError(
                f"No .npz files found in {feat_dir / split}.\n"
                "Run preprocess.py first."
            )
        print(f"  Loading {len(npz_files):,} files  [{split}] …", flush=True)

        mels, labs = [], []
        for p in tqdm(npz_files, unit="file", leave=False):
            d = np.load(str(p))
            mels.append(d["mel"])      # stored as float16
            labs.append(d["labels"])

        # Concatenate as float32 after stacking (float16 halves peak RAM during load)
        mel_np    = np.concatenate(mels, axis=0).astype(np.float32)   # [N, MEL_BANDS]
        labels_np = np.concatenate(labs,  axis=0)                     # [N, 3]

        # Normalise (mean/std are float32)
        mel_np = (mel_np - mean) / std

        self.mel    = torch.from_numpy(mel_np)
        self.labels = torch.from_numpy(labels_np)

        ram_gb = (self.mel.nbytes + self.labels.nbytes) / 1e9
        print(f"  {len(self.mel):,} frames loaded  ({ram_gb:.1f} GB in RAM).", flush=True)

    def __len__(self) -> int:
        # First CONTEXT frames can't form a full window
        return len(self.mel) - CONTEXT

    def __getitem__(self, idx: int):
        i = idx + CONTEXT
        patch = self.mel[i - CONTEXT : i]           # [CONTEXT, MEL_BANDS]
        x = patch.unsqueeze(0)                       # [1, CONTEXT, MEL_BANDS]
        y = self.labels[i].float()                   # [3]
        return x, y


def make_weighted_sampler(dataset: DrumDataset) -> WeightedRandomSampler:
    """Up-sample frames that contain at least one drum onset."""
    labels = dataset.labels[CONTEXT:]               # [N, 3]
    has_onset = (labels.sum(dim=1) > 0).numpy()
    n_pos = has_onset.sum()
    n_neg = len(has_onset) - n_pos
    w_pos = float(n_neg) / max(n_pos, 1)
    weights = np.where(has_onset, w_pos, 1.0)
    return WeightedRandomSampler(
        torch.from_numpy(weights).float(),
        num_samples=len(weights),
        replacement=True,
    )


# ── Training helpers ──────────────────────────────────────────────────────────
def train_epoch(model, loader, criterion, optimizer, device) -> float:
    model.train()
    total_loss = total_n = 0
    for x, y in loader:
        x = x.to(device, non_blocking=True)
        y = y.to(device, non_blocking=True)
        optimizer.zero_grad(set_to_none=True)
        loss = criterion(model(x), y)
        loss.backward()
        optimizer.step()
        total_loss += loss.item() * len(x)
        total_n    += len(x)
    return total_loss / total_n


@torch.no_grad()
def evaluate(model, loader, criterion, device):
    model.eval()
    total_loss = total_n = 0
    tp = np.zeros(N_CLASSES)
    fp = np.zeros(N_CLASSES)
    fn = np.zeros(N_CLASSES)

    for x, y in loader:
        x, y = x.to(device), y.to(device)
        logits = model(x)
        total_loss += criterion(logits, y).item() * len(x)
        total_n    += len(x)

        pred = (torch.sigmoid(logits) > THRESHOLD).cpu().numpy()
        gt   = (y > 0.5).cpu().numpy()
        tp  += (pred & gt).sum(0)
        fp  += (pred & ~gt).sum(0)
        fn  += (~pred & gt).sum(0)

    f1 = (2 * tp) / np.maximum(2 * tp + fp + fn, 1)
    return total_loss / total_n, f1


# ── ONNX export ───────────────────────────────────────────────────────────────
def export_onnx(state_dict: dict, onnx_path: Path):
    """Export model with Sigmoid output for plugin compatibility."""
    model = DrumDetector(sigmoid_output=True).cpu().eval()
    model.load_state_dict(state_dict)

    dummy = torch.zeros(1, 1, CONTEXT, MEL_BANDS)

    torch.onnx.export(
        model,
        dummy,
        str(onnx_path),
        export_params=True,
        opset_version=13,
        do_constant_folding=True,
        input_names=["mel_patch"],
        output_names=["onset_probs"],
        dynamic_axes=None,    # static shape → fastest ONNX Runtime path
    )

    # Sanity-check
    try:
        import onnx
        m = onnx.load(str(onnx_path))
        onnx.checker.check_model(m)
        print(f"  ONNX graph verified OK.", flush=True)
    except ImportError:
        print("  (install 'onnx' to enable graph verification)", flush=True)

    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(str(onnx_path),
                                    providers=["CPUExecutionProvider"])
        out = sess.run(None, {"mel_patch": dummy.numpy()})
        assert out[0].shape == (1, 3), "Unexpected output shape"
        print(f"  ONNX Runtime inference check: output {out[0].shape}  ✓", flush=True)
    except ImportError:
        pass

    print(f"  Saved → {onnx_path}  ({onnx_path.stat().st_size / 1024:.0f} KB)")


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    MODEL_DIR.mkdir(parents=True, exist_ok=True)

    print(f"\nDevice : {DEVICE}")
    if DEVICE == "cuda":
        print(f"GPU    : {torch.cuda.get_device_name(0)}")
        # Enable TF32 for slightly faster matmuls on Ampere/Ada
        torch.backends.cuda.matmul.allow_tf32 = True
        torch.backends.cudnn.allow_tf32 = True
        torch.backends.cudnn.benchmark  = True

    # ── Datasets ──────────────────────────────────────────────────────────────
    print("\nLoading training data …")
    train_ds = DrumDataset(FEAT_DIR, "train")
    print("\nLoading validation data …")
    val_ds   = DrumDataset(FEAT_DIR, "valid")

    sampler = make_weighted_sampler(train_ds)
    train_loader = DataLoader(
        train_ds, batch_size=BATCH_SIZE, sampler=sampler,
        num_workers=NUM_WORKERS, pin_memory=(DEVICE == "cuda"),
        persistent_workers=(NUM_WORKERS > 0),
    )
    val_loader = DataLoader(
        val_ds, batch_size=BATCH_SIZE * 2, shuffle=False,
        num_workers=NUM_WORKERS, pin_memory=(DEVICE == "cuda"),
        persistent_workers=(NUM_WORKERS > 0),
    )

    # ── Model ─────────────────────────────────────────────────────────────────
    model = DrumDetector(sigmoid_output=False).to(DEVICE)
    n_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"\nModel  : {n_params:,} trainable parameters")

    pos_w     = torch.full((N_CLASSES,), POS_WEIGHT, device=DEVICE)
    criterion = nn.BCEWithLogitsLoss(pos_weight=pos_w)
    optimizer = optim.AdamW(model.parameters(), lr=LR_INIT, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.ExponentialLR(optimizer, gamma=LR_GAMMA)

    # ── Training loop ──────────────────────────────────────────────────────────
    print(f"\nTraining for {EPOCHS} epochs  (batch={BATCH_SIZE}) …\n")
    header = (f"{'Ep':>3} | {'TrainLoss':>9} | {'ValLoss':>8} | "
              f"{'F1-K':>5} {'F1-S':>5} {'F1-H':>5} {'F1-mean':>7} | "
              f"{'LR':>8} | {'Time':>5}")
    print(header)
    print("─" * len(header))

    best_f1   = -1.0
    best_sd   = None

    for epoch in range(1, EPOCHS + 1):
        t0 = time.time()
        tr_loss       = train_epoch(model, train_loader, criterion, optimizer, DEVICE)
        val_loss, f1  = evaluate(model, val_loader, criterion, DEVICE)
        scheduler.step()

        elapsed  = time.time() - t0
        mean_f1  = f1.mean()
        lr_now   = scheduler.get_last_lr()[0]

        print(f"{epoch:3d} | {tr_loss:9.4f} | {val_loss:8.4f} | "
              f"{f1[0]:5.3f} {f1[1]:5.3f} {f1[2]:5.3f} {mean_f1:7.3f} | "
              f"{lr_now:8.2e} | {elapsed:4.0f}s",
              flush=True)

        if mean_f1 > best_f1:
            best_f1 = mean_f1
            best_sd = {k: v.cpu().clone() for k, v in model.state_dict().items()}
            torch.save(best_sd, MODEL_DIR / "drum_detector_best.pth")

    print(f"\nBest validation F1 : {best_f1:.4f}")

    # ── ONNX export ───────────────────────────────────────────────────────────
    print("\nExporting best checkpoint to ONNX …")
    onnx_path = MODEL_DIR / "drum_detector.onnx"
    export_onnx(best_sd, onnx_path)

    # ── Normalization JSON (needed by the plugin at inference time) ───────────
    mean = np.load(str(FEAT_DIR / "mel_mean.npy")).tolist()
    std  = np.load(str(FEAT_DIR / "mel_std.npy")).tolist()
    stats = {
        "mel_mean":    mean,
        "mel_std":     std,
        "mel_bands":   MEL_BANDS,
        "context":     CONTEXT,
        "hop_samples": 441,
        "fft_size":    1024,
        "sample_rate": 44100,
        "f_min":       20.0,
        "f_max":       8000.0,
        "classes":     ["kick", "snare", "hihat"],
        "threshold":   THRESHOLD,
    }
    norm_path = MODEL_DIR / "normalization.json"
    norm_path.write_text(json.dumps(stats, indent=2))
    print(f"Normalization stats → {norm_path}")

    print("\n" + "=" * 60)
    print("  Training complete!")
    print()
    print("  Copy these two files to the plugin's resources folder:")
    print(f"    {onnx_path.resolve()}")
    print(f"    {norm_path.resolve()}")
    print("=" * 60 + "\n")


if __name__ == "__main__":
    # Required on Windows with spawn-based multiprocessing
    main()
