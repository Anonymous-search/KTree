#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path
from typing import Dict, List


def read_rows(path: Path) -> List[Dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def normalize_number(value: str):
    if value in ("", "None", None):
        return None
    try:
        return float(value)
    except ValueError:
        return None


def collect_query_timings(rows: List[Dict[str, str]], experiment: str) -> List[Dict[str, str]]:
    grouped: List[Dict[str, str]] = []
    for row in rows:
        if row["experiment"] != experiment:
            continue
        query_seconds = normalize_number(row.get("wall_clock_seconds")) or 0.0
        if query_seconds == 0.0:
            continue
        grouped.append(
            {
                "baseline": row["baseline"],
                "query_set": row["query_set"],
                "k": row["k"],
                "query_seconds": query_seconds,
            }
        )
    grouped.sort(key=lambda item: (int(item["k"]), item["query_set"], item["baseline"]))
    return grouped


def category_label(entry: Dict[str, str]) -> str:
    return f"{entry['query_set']} (k={entry['k']})"


def plot_matplotlib(rows: List[Dict[str, str]], output_dir: Path) -> None:
    import matplotlib.pyplot as plt

    experiments = sorted({row["experiment"] for row in rows})
    for experiment in experiments:
        experiment_rows = [row for row in rows if row["experiment"] == experiment]
        if not experiment_rows:
            continue
        title = experiment_rows[0]["title"] or experiment
        grouped = collect_query_timings(rows, experiment)
        if not grouped:
            continue

        baselines = []
        seen_baselines = set()
        for entry in grouped:
            baseline = entry["baseline"]
            if baseline not in seen_baselines:
                seen_baselines.add(baseline)
                baselines.append(baseline)

        color_map = plt.get_cmap("tab10", max(len(baselines), 1))
        baseline_colors = {
            baseline: color_map(index) for index, baseline in enumerate(baselines)
        }

        categories = []
        seen_categories = set()
        for entry in grouped:
            category = (entry["query_set"], entry["k"])
            if category not in seen_categories:
                seen_categories.add(category)
                categories.append(category)

        values = {
            (entry["query_set"], entry["k"], entry["baseline"]): entry["query_seconds"]
            for entry in grouped
        }

        group_positions = list(range(len(categories)))
        bar_width = 0.8 / max(len(baselines), 1)

        fig, ax = plt.subplots(figsize=(max(10, len(categories) * 2.2), 6))
        for index, baseline in enumerate(baselines):
            offset = (index - (len(baselines) - 1) / 2.0) * bar_width
            heights = [
                values.get((query_set, k, baseline), 0.0)
                for query_set, k in categories
            ]
            positions = [group_x + offset for group_x in group_positions]
            ax.bar(
                positions,
                heights,
                width=bar_width,
                color=baseline_colors[baseline],
                label=baseline,
            )

        tick_labels = [f"{query_set} (k={k})" for query_set, k in categories]
        ax.set_xticks(group_positions)
        ax.set_xticklabels(tick_labels, rotation=30, ha="right")
        ax.set_ylabel("seconds")
        ax.set_title(f"{title} - query time")
        ax.legend(title="Baseline")
        ax.grid(axis="y", linestyle="--", alpha=0.3)
        fig.tight_layout()
        fig.savefig(output_dir / f"{experiment}.png", dpi=200)
        plt.close(fig)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Plot experiment results"
    )
    parser.add_argument(
        "--results",
        required=True,
        help="CSV produced by reproduce.py",
    )
    parser.add_argument(
        "--output-dir", 
        required=True,
        help="Output directory for figures"
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    results_path = Path(args.results).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = read_rows(results_path)
    plot_matplotlib(rows, output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
