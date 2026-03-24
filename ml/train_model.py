#!/usr/bin/env python3
"""
BybitTrader ML Training Pipeline
─────────────────────────────────
Trains a GRU/LSTM model on recorded feature data and exports to ONNX format.

Usage:
    python train_model.py --data_dir ./logs/session_XXX --model_type gru --export model.onnx

Expected input: features.csv from ResearchRecorder with 25 features per row.
Expected output: ONNX model with:
    Input:  "features"  [1, seq_len, 25]  float32
    Output: "probs"     [1, 12]           float32  (4 horizons × 3 classes)
"""

import argparse
import os
import sys
import numpy as np
import pandas as pd
from pathlib import Path

import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

# ─── Constants ────────────────────────────────────────────────────────────────

FEATURE_COUNT = 25
NUM_HORIZONS = 4
NUM_CLASSES = 3  # up, down, flat
SEQ_LEN = 100
MOVE_THRESHOLD_BPS = 0.5  # 0.5 bps = significant move

# Horizon steps (in feature ticks, assuming 10ms per tick)
HORIZON_TICKS = [10, 50, 100, 300]  # 100ms, 500ms, 1s, 3s

FEATURE_COLUMNS = [
    'imb1', 'imb5', 'imb20', 'ob_slope', 'depth_conc', 'cancel_spike', 'liq_wall',
    'aggr_ratio', 'avg_trade_sz', 'trade_vel', 'trade_accel', 'vol_accel',
    'microprice', 'spread_bps', 'spread_chg', 'mid_mom', 'volatility',
    'mp_dev', 'st_pressure', 'bid_depth', 'ask_depth',
    'd_imb_dt', 'd2_imb_dt2', 'd_vol_dt', 'd_mom_dt'
]


# ─── Dataset ──────────────────────────────────────────────────────────────────

class FeatureSequenceDataset(Dataset):
    """Creates sequences of features with multi-horizon labels."""

    def __init__(self, features: np.ndarray, mid_prices: np.ndarray,
                 seq_len: int = SEQ_LEN):
        self.seq_len = seq_len
        self.features = features  # [N, FEATURE_COUNT]
        self.mid_prices = mid_prices  # [N]

        # Normalize features (z-score)
        self.mean = np.mean(features, axis=0)
        self.std = np.std(features, axis=0) + 1e-8
        self.features_norm = (features - self.mean) / self.std

        # Clip to [-5, 5]
        self.features_norm = np.clip(self.features_norm, -5.0, 5.0)

        # Compute labels for all horizons
        self.labels = self._compute_labels()

        # Valid indices (need seq_len history + max horizon forward)
        max_horizon = max(HORIZON_TICKS)
        self.valid_indices = list(range(seq_len, len(features) - max_horizon))

    def _compute_labels(self) -> np.ndarray:
        """Compute 3-class labels for each horizon at each timestep."""
        N = len(self.mid_prices)
        labels = np.full((N, NUM_HORIZONS), 2, dtype=np.int64)  # default: flat

        for h_idx, h_ticks in enumerate(HORIZON_TICKS):
            for i in range(N - h_ticks):
                if self.mid_prices[i] < 1e-8:
                    continue
                move_bps = ((self.mid_prices[i + h_ticks] - self.mid_prices[i])
                            / self.mid_prices[i]) * 10000.0
                if move_bps > MOVE_THRESHOLD_BPS:
                    labels[i, h_idx] = 0  # up
                elif move_bps < -MOVE_THRESHOLD_BPS:
                    labels[i, h_idx] = 1  # down
                # else: flat (2)

        return labels

    def __len__(self):
        return len(self.valid_indices)

    def __getitem__(self, idx):
        i = self.valid_indices[idx]
        seq = self.features_norm[i - self.seq_len:i]  # [seq_len, FEATURE_COUNT]
        label = self.labels[i]  # [NUM_HORIZONS]
        return (torch.tensor(seq, dtype=torch.float32),
                torch.tensor(label, dtype=torch.long))


# ─── Model ────────────────────────────────────────────────────────────────────

class PricePredictionModel(nn.Module):
    """GRU/LSTM model for multi-horizon 3-class price prediction."""

    def __init__(self, input_size=FEATURE_COUNT, hidden_size=64,
                 num_layers=2, dropout=0.1, model_type='gru'):
        super().__init__()

        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.model_type = model_type

        RNNClass = nn.GRU if model_type == 'gru' else nn.LSTM
        self.rnn = RNNClass(
            input_size=input_size,
            hidden_size=hidden_size,
            num_layers=num_layers,
            batch_first=True,
            dropout=dropout if num_layers > 1 else 0.0
        )

        self.layer_norm = nn.LayerNorm(hidden_size)
        self.dropout = nn.Dropout(dropout)

        # Output head: one linear layer per horizon, each outputs 3 classes
        self.heads = nn.ModuleList([
            nn.Linear(hidden_size, NUM_CLASSES) for _ in range(NUM_HORIZONS)
        ])

    def forward(self, x):
        # x: [batch, seq_len, input_size]
        rnn_out, _ = self.rnn(x)  # [batch, seq_len, hidden_size]

        # Use last hidden state
        last_hidden = rnn_out[:, -1, :]  # [batch, hidden_size]
        last_hidden = self.layer_norm(last_hidden)
        last_hidden = self.dropout(last_hidden)

        # Per-horizon predictions with softmax
        horizon_probs = []
        for head in self.heads:
            logits = head(last_hidden)  # [batch, 3]
            probs = torch.softmax(logits, dim=-1)
            horizon_probs.append(probs)

        # Concatenate: [batch, NUM_HORIZONS * 3]
        output = torch.cat(horizon_probs, dim=-1)
        return output


class OnnxWrapper(nn.Module):
    """Wrapper that ensures softmax is baked into the ONNX export."""

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, features):
        return self.model(features)


# ─── Training ────────────────────────────────────────────────────────────────

def train_model(dataset: FeatureSequenceDataset, args) -> PricePredictionModel:
    """Train the model and return it."""

    # Split train/val
    n = len(dataset)
    train_size = int(0.8 * n)
    val_size = n - train_size
    train_ds, val_ds = torch.utils.data.random_split(dataset, [train_size, val_size])

    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True,
                              num_workers=0, pin_memory=True)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size, shuffle=False,
                            num_workers=0, pin_memory=True)

    device = torch.device('cuda' if torch.cuda.is_available() else
                          'mps' if torch.backends.mps.is_available() else 'cpu')
    print(f"Training on: {device}")

    model = PricePredictionModel(
        hidden_size=args.hidden_size,
        num_layers=args.num_layers,
        dropout=args.dropout,
        model_type=args.model_type
    ).to(device)

    optimizer = optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)
    criterion = nn.CrossEntropyLoss()

    best_val_acc = 0.0
    best_state = None

    for epoch in range(args.epochs):
        # Train
        model.train()
        train_loss = 0.0
        train_correct = 0
        train_total = 0

        for seqs, labels in train_loader:
            seqs, labels = seqs.to(device), labels.to(device)

            optimizer.zero_grad()
            output = model(seqs)  # [batch, NUM_HORIZONS * 3]

            # Compute loss for each horizon
            loss = 0.0
            for h in range(NUM_HORIZONS):
                h_probs = output[:, h * 3:(h + 1) * 3]
                h_labels = labels[:, h]
                loss += criterion(h_probs, h_labels)
            loss /= NUM_HORIZONS

            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss += loss.item() * seqs.size(0)

            # Accuracy for 500ms horizon (index 1)
            with torch.no_grad():
                h1_probs = output[:, 3:6]
                preds = h1_probs.argmax(dim=-1)
                train_correct += (preds == labels[:, 1]).sum().item()
                train_total += seqs.size(0)

        scheduler.step()

        # Validate
        model.eval()
        val_loss = 0.0
        val_correct = 0
        val_total = 0
        class_tp = [0, 0, 0]
        class_fp = [0, 0, 0]
        class_fn = [0, 0, 0]

        with torch.no_grad():
            for seqs, labels in val_loader:
                seqs, labels = seqs.to(device), labels.to(device)
                output = model(seqs)

                loss = 0.0
                for h in range(NUM_HORIZONS):
                    h_probs = output[:, h * 3:(h + 1) * 3]
                    h_labels = labels[:, h]
                    loss += criterion(h_probs, h_labels)
                loss /= NUM_HORIZONS
                val_loss += loss.item() * seqs.size(0)

                # 500ms horizon accuracy
                h1_probs = output[:, 3:6]
                preds = h1_probs.argmax(dim=-1)
                targets = labels[:, 1]
                val_correct += (preds == targets).sum().item()
                val_total += seqs.size(0)

                # Per-class metrics
                for c in range(3):
                    class_tp[c] += ((preds == c) & (targets == c)).sum().item()
                    class_fp[c] += ((preds == c) & (targets != c)).sum().item()
                    class_fn[c] += ((preds != c) & (targets == c)).sum().item()

        train_acc = train_correct / max(train_total, 1)
        val_acc = val_correct / max(val_total, 1)

        # Per-class precision/recall
        prec = [class_tp[c] / max(class_tp[c] + class_fp[c], 1) for c in range(3)]
        recall = [class_tp[c] / max(class_tp[c] + class_fn[c], 1) for c in range(3)]

        print(f"Epoch {epoch+1:3d}/{args.epochs} | "
              f"Train Loss: {train_loss/max(train_total,1):.4f} Acc: {train_acc:.4f} | "
              f"Val Loss: {val_loss/max(val_total,1):.4f} Acc: {val_acc:.4f} | "
              f"P(up/dn/fl): {prec[0]:.3f}/{prec[1]:.3f}/{prec[2]:.3f} | "
              f"R(up/dn/fl): {recall[0]:.3f}/{recall[1]:.3f}/{recall[2]:.3f}")

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}

    if best_state:
        model.load_state_dict(best_state)
        print(f"\nBest val accuracy: {best_val_acc:.4f}")

    return model.cpu()


# ─── ONNX Export ──────────────────────────────────────────────────────────────

def export_onnx(model: PricePredictionModel, output_path: str,
                seq_len: int = SEQ_LEN):
    """Export model to ONNX format."""
    model.eval()
    wrapper = OnnxWrapper(model)

    dummy_input = torch.randn(1, seq_len, FEATURE_COUNT)

    torch.onnx.export(
        wrapper,
        dummy_input,
        output_path,
        input_names=['features'],
        output_names=['probs'],
        dynamic_axes={
            'features': {0: 'batch', 1: 'seq_len'},
            'probs': {0: 'batch'}
        },
        opset_version=17,
        do_constant_folding=True
    )
    print(f"ONNX model exported to: {output_path}")

    # Verify with onnxruntime
    try:
        import onnxruntime as ort
        sess = ort.InferenceSession(output_path)
        result = sess.run(None, {'features': dummy_input.numpy()})
        probs = result[0]
        print(f"ONNX verification: output shape={probs.shape}, "
              f"sum per horizon={[probs[0, h*3:(h+1)*3].sum():.4f for h in range(NUM_HORIZONS)]}")
    except ImportError:
        print("Warning: onnxruntime not installed, skipping verification")


def save_normalization(mean: np.ndarray, std: np.ndarray, output_path: str):
    """Save normalization stats as binary (compatible with C++ load)."""
    with open(output_path, 'wb') as f:
        mean.astype(np.float64).tofile(f)
        std.astype(np.float64).tofile(f)
    print(f"Normalization stats saved to: {output_path}")


# ─── Data Loading ─────────────────────────────────────────────────────────────

def load_data(data_dir: str) -> tuple:
    """Load features.csv from ResearchRecorder output."""
    features_path = os.path.join(data_dir, 'features.csv')

    if not os.path.exists(features_path):
        print(f"Error: {features_path} not found")
        sys.exit(1)

    df = pd.read_csv(features_path)
    print(f"Loaded {len(df)} rows from {features_path}")

    # Extract feature columns (skip timestamp_ns which is first column)
    feature_cols = df.columns[1:FEATURE_COUNT + 1]
    features = df[feature_cols].values.astype(np.float64)

    # Extract microprice for label computation (column index 12 in features = 13 in CSV)
    microprice_idx = FEATURE_COLUMNS.index('microprice')
    mid_prices = features[:, microprice_idx].copy()

    # Handle NaN/Inf
    features = np.nan_to_num(features, nan=0.0, posinf=5.0, neginf=-5.0)
    mid_prices = np.nan_to_num(mid_prices, nan=0.0)

    print(f"Features shape: {features.shape}")
    print(f"Price range: {mid_prices.min():.2f} - {mid_prices.max():.2f}")

    return features, mid_prices


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='BybitTrader ML Training Pipeline')
    parser.add_argument('--data_dir', type=str, required=True,
                        help='Path to session directory with features.csv')
    parser.add_argument('--export', type=str, default='model.onnx',
                        help='Output ONNX model path')
    parser.add_argument('--model_type', type=str, default='gru',
                        choices=['gru', 'lstm'], help='RNN type')
    parser.add_argument('--hidden_size', type=int, default=64)
    parser.add_argument('--num_layers', type=int, default=2)
    parser.add_argument('--seq_len', type=int, default=SEQ_LEN)
    parser.add_argument('--epochs', type=int, default=50)
    parser.add_argument('--batch_size', type=int, default=256)
    parser.add_argument('--lr', type=float, default=1e-3)
    parser.add_argument('--dropout', type=float, default=0.1)
    args = parser.parse_args()

    # Load data
    features, mid_prices = load_data(args.data_dir)

    # Create dataset
    dataset = FeatureSequenceDataset(features, mid_prices, seq_len=args.seq_len)
    print(f"Dataset: {len(dataset)} sequences (seq_len={args.seq_len})")

    # Label distribution for 500ms horizon
    labels_500 = dataset.labels[dataset.valid_indices, 1]
    up_pct = (labels_500 == 0).mean() * 100
    dn_pct = (labels_500 == 1).mean() * 100
    fl_pct = (labels_500 == 2).mean() * 100
    print(f"Label distribution (500ms): UP={up_pct:.1f}% DOWN={dn_pct:.1f}% FLAT={fl_pct:.1f}%")

    # Train
    model = train_model(dataset, args)

    # Export ONNX
    export_dir = os.path.dirname(args.export) or '.'
    os.makedirs(export_dir, exist_ok=True)
    export_onnx(model, args.export, seq_len=args.seq_len)

    # Save normalization stats
    norm_path = args.export.replace('.onnx', '_norm.bin')
    save_normalization(dataset.mean, dataset.std, norm_path)

    print("\nDone! Files created:")
    print(f"  Model:         {args.export}")
    print(f"  Normalization: {norm_path}")
    print(f"\nTo use in BybitTrader, set:")
    print(f"  onnx_model_path = \"{os.path.abspath(args.export)}\"")


if __name__ == '__main__':
    main()
