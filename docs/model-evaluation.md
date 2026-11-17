# TinyML model evaluation

The checked-in artifact is `himrakshak-synthetic-demo-tree-v2`, a compact
Decision Tree exported for the virtual ESP32 path.

| Item | Result |
|---|---|
| Dataset | 2,400 synthetic scenario rows |
| Split | 1,800 train / 600 holdout |
| Selected tree | Depth 4, 19 nodes |
| Holdout accuracy | 98.83% |
| CRITICAL recall | 99.50% |
| Confusion matrix | `[[199, 1, 0], [1, 195, 4], [0, 1, 199]]` |

Rows and columns in the matrix are `NORMAL`, `WARNING`, `CRITICAL`.

These are synthetic-label agreement measurements. They do not estimate
real-world landslide prediction, sensor accuracy, scientific validity, or
safety performance. In particular, the vibration-burst subtype demonstrates
an inspection/anomaly warning over simulated baseline values; it does not show
that an isolated vibration event predicts slope failure.

The research susceptibility and dynamic early-warning models remain
`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`; no real source dataset or event
inventory is bundled as field truth. See [training](tinyml-training.md) and
the [data-source rules](data-sources-and-quality.md).
