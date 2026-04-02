"""
DrumDetector – Causal CNN for real-time kick / snare / hi-hat onset detection.

Input shape  : [B, 1, CONTEXT=15, MEL_BANDS=40]
               One mel-spectrogram patch of 15 past frames × 40 mel bands.
               Hop = 441 samples = 10 ms at 44.1 kHz.

Output shape : [B, 3]
               Raw logits (kick, snare, hi-hat).
               During ONNX export a Sigmoid wrapper is applied so the plugin
               receives probabilities in [0, 1].

Causal property: padding on the time axis is left-only (past context only),
so no future frames are ever used. Latency = exactly one hop = 10 ms.

~350 K trainable parameters.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F


# ── Hyperparameters (must stay in sync with preprocess.py and the plugin) ──────
CONTEXT   = 15    # past mel frames used as input context
MEL_BANDS = 40    # mel filterbank resolution
N_CLASSES = 3     # kick (0), snare (1), hi-hat (2)


class CausalConv2d(nn.Module):
    """
    2-D convolution with causal (left-only) padding on the time axis and
    symmetric 'same' padding on the frequency axis.

    Time axis   dim=-2 : padded (kernel-1) on the left, 0 on the right.
    Freq axis   dim=-1 : padded (kernel//2) on both sides.
    """

    def __init__(self, in_channels: int, out_channels: int, kernel_size: int):
        super().__init__()
        self.time_pad = kernel_size - 1
        self.freq_pad = kernel_size // 2
        self.conv = nn.Conv2d(in_channels, out_channels, kernel_size, padding=0)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # F.pad order: (last_dim_lo, last_dim_hi, second_last_lo, second_last_hi)
        x = F.pad(x, (self.freq_pad, self.freq_pad,   # freq: symmetric
                       self.time_pad, 0))               # time: causal (left only)
        return self.conv(x)


class DrumDetector(nn.Module):
    """
    Causal CNN drum onset detector.

    sigmoid_output=False  (default, training)  – returns raw logits.
    sigmoid_output=True   (ONNX export)        – returns probabilities [0,1].
    """

    def __init__(self, sigmoid_output: bool = False):
        super().__init__()
        self.sigmoid_output = sigmoid_output

        # Block 1 : [B,  1, T, 40] → [B, 16, T, 40]
        self.conv1 = CausalConv2d(1,  16, 3)
        self.bn1   = nn.BatchNorm2d(16)

        # Block 2 : [B, 16, T, 40] → [B, 32, T, 20]
        self.conv2 = CausalConv2d(16, 32, 3)
        self.bn2   = nn.BatchNorm2d(32)
        self.pool2 = nn.MaxPool2d(kernel_size=(1, 2))   # freq: 40 → 20

        # Block 3 : [B, 32, T, 20] → [B, 64, T, 10]
        self.conv3 = CausalConv2d(32, 64, 3)
        self.bn3   = nn.BatchNorm2d(64)
        self.pool3 = nn.MaxPool2d(kernel_size=(1, 2))   # freq: 20 → 10

        # Classifier on the last time frame: [B, 64*10] → [B, 3]
        self.fc1     = nn.Linear(64 * (MEL_BANDS // 4), 128)
        self.dropout = nn.Dropout(0.3)
        self.fc2     = nn.Linear(128, N_CLASSES)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x : [B, 1, T=15, F=40]
        x = F.relu(self.bn1(self.conv1(x)))               # [B, 16, T, 40]
        x = self.pool2(F.relu(self.bn2(self.conv2(x))))   # [B, 32, T, 20]
        x = self.pool3(F.relu(self.bn3(self.conv3(x))))   # [B, 64, T, 10]

        x = x[:, :, -1, :]                                # take last time frame → [B, 64, 10]
        x = x.reshape(x.size(0), -1)                      # [B, 640]
        x = F.relu(self.fc1(x))                            # [B, 128]
        x = self.dropout(x)
        logits = self.fc2(x)                               # [B, 3]

        return torch.sigmoid(logits) if self.sigmoid_output else logits


if __name__ == "__main__":
    m = DrumDetector()
    n = sum(p.numel() for p in m.parameters() if p.requires_grad)
    print(f"DrumDetector: {n:,} trainable parameters")

    dummy = torch.zeros(1, 1, CONTEXT, MEL_BANDS)
    out = m(dummy)
    print(f"Input  shape : {list(dummy.shape)}")
    print(f"Output shape : {list(out.shape)}")
