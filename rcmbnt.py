"""Small simulation script.

Behavior:
- Maintain `n_stacks` (default 20) counters.
- Every `sample_every` timesteps (default 20) sample `sample_k` unique stacks and increment them.
- Every `remove_every` timesteps (default 30) remove all units from a stack with the current maximum value.
- Record a vector of (timestep, total_amount) for each remove moment.
- Run for `time_steps` timesteps (default 10000) and save results to CSV.

Usage: python rcmbnt.py
Optional args: --time_steps, --sample_every, --sample_k, --remove_every, --n_stacks, --out, --seed, --plot
"""
from __future__ import annotations

import argparse
import csv
import random
from typing import List, Tuple


def run_simulation(time_steps: int = 10000,
                   n_stacks: int = 20,
                   sample_every: int = 20,
                   sample_k: int = 10,
                   remove_every: int = 30,
                   remove_threshold: int = 1,
                   seed: int | None = None) -> List[Tuple[int, int]]:
    """Run the timestep simulation and return list of (timestep, total_amount)."""
    if seed is not None:
        random.seed(seed)

    stacks = [0] * n_stacks
    results: List[Tuple[int, int]] = []

    for t in range(0, time_steps):
        if t % sample_every == 0:
            # sample k unique stacks and increment them
            for _ in range(sample_k):
                i = random.randint(0, n_stacks - 1)
                stacks[i] += 1
        
        if t % remove_every == 0:
            maxv = max(stacks)
            if maxv >= remove_threshold:
                candidates = [i for i, v in enumerate(stacks) if v == maxv]
                idx = random.choice(candidates)
                stacks[idx] = 0  # reset to zero instead of decrementing by 1
            results.append((t, maxv))  # record the value before removing

    return results


def save_results(path: str, results: List[Tuple[int, int]]) -> None:
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestep", "amount"])
        writer.writerows(results)


def _percentile(sorted_vals: List[float], p: float) -> float:
    """Compute percentile p in [0,1] on sorted_vals using linear interpolation."""
    n = len(sorted_vals)
    if n == 0:
        return 0.0
    if p <= 0:
        return sorted_vals[0]
    if p >= 1:
        return sorted_vals[-1]
    idx = p * (n - 1)
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    frac = idx - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def aggregate_runs(all_results: List[List[Tuple[int, int]]], ci: float = 0.90):
    """Aggregate multiple runs (each a list of (timestep, amount)).

    Returns: list of (timestep, mean, lower, upper) where lower/upper are the
    two-sided confidence bounds at level `ci` (e.g., ci=0.9 -> 5th and 95th percentiles).
    """
    # collect unique timesteps (preserve order)
    timesteps = []
    seen = set()
    for run in all_results:
        for t, _ in run:
            if t not in seen:
                seen.add(t)
                timesteps.append(t)

    # map timestep -> list of amounts across runs (fill missing with 0)
    n_runs = len(all_results)
    amounts_by_t = {t: [] for t in timesteps}
    for run in all_results:
        d = {t: a for t, a in run}
        for t in timesteps:
            amounts_by_t[t].append(d.get(t, 0))

    lowp = (1 - ci) / 2.0
    highp = 1.0 - lowp
    aggregated = []
    for t in timesteps:
        vals = amounts_by_t[t]
        sorted_vals = sorted(vals)
        mean = sum(vals) / float(n_runs) if n_runs > 0 else 0.0
        lower = _percentile(sorted_vals, lowp)
        upper = _percentile(sorted_vals, highp)
        aggregated.append((t, mean, lower, upper))

    return aggregated


def save_agg_results(path: str, agg: List[Tuple[int, float, float, float]]) -> None:
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestep", "mean", "lower", "upper"])
        for row in agg:
            writer.writerow(row)


def main() -> None:
    p = argparse.ArgumentParser(description="Run stack sampling simulation")
    p.add_argument("--time_steps", type=int, default=10000)
    p.add_argument("--n_stacks", type=int, default=20)
    p.add_argument("--sample_every", type=int, default=20)
    p.add_argument("--sample_k", type=int, default=10)
    p.add_argument("--remove_every", type=int, default=30)
    p.add_argument("--out", type=str, default="rcmbnt_results.csv")
    p.add_argument("--runs", type=int, default=1, help="Number of independent runs to average")
    p.add_argument("--ci", type=float, default=0.90, help="Confidence interval width (e.g. 0.9 for 90%%)")
    p.add_argument("--out_agg", type=str, default="rcmbnt_agg.csv", help="Output CSV for aggregated results")
    p.add_argument("--plotfile", type=str, default="rcmbnt_plot.png", help="PNG file to save the plot")
    p.add_argument("--remove_threshold", type=int, default=5, help="Only remove if max value exceeds this threshold")
    p.add_argument("--seed", type=int, default=None)
    p.add_argument("--plot", action="store_true", help="Attempt to plot results if matplotlib is available")
    args = p.parse_args()

    print(f"Running simulation: time_steps={args.time_steps}, n_stacks={args.n_stacks}, runs={args.runs}")

    if args.runs <= 1:
        results = run_simulation(time_steps=args.time_steps,
                                 n_stacks=args.n_stacks,
                                 sample_every=args.sample_every,
                                 sample_k=args.sample_k,
                                 remove_every=args.remove_every,
                                 remove_threshold=args.remove_threshold,
                                 seed=args.seed)
        save_results(args.out, results)
        print(f"Saved results to {args.out}")
        all_results = [results]
    else:
        all_results = []
        for i in range(args.runs):
            s = None if args.seed is None else (args.seed + i)
            r = run_simulation(time_steps=args.time_steps,
                               n_stacks=args.n_stacks,
                               sample_every=args.sample_every,
                               sample_k=args.sample_k,
                               remove_every=args.remove_every,
                               seed=s)
            all_results.append(r)
        agg = aggregate_runs(all_results, ci=args.ci)
        save_agg_results(args.out_agg, agg)
        print(f"Saved aggregated results to {args.out_agg}")

    if args.plot:
        try:
            import matplotlib.pyplot as plt

            # choose source: aggregated if multiple runs, else single-run results
            if args.runs <= 1:
                xs = [t / 60.0 for t, _ in all_results[0]]
                ys = [amt for _, amt in all_results[0]]
                plt.plot(xs, ys, label="run")
            else:
                agg = aggregate_runs(all_results, ci=args.ci)
                xs = [t / 60.0 for t, _, _, _ in agg]
                means = [m for _, m, _, _ in agg]
                lowers = [l for _, _, l, _ in agg]
                uppers = [u for _, _, _, u in agg]
                plt.plot(xs, means, color="C0", label="mean")
                plt.fill_between(xs, lowers, uppers, color="C0", alpha=0.25, label=f"{int(args.ci*100)}% CI")

            plt.xlabel("time (hours)")
            plt.ylabel("amount")
            plt.title("Simulation: mean and confidence band over time")
            plt.grid(True)
            plt.legend()
            try:
                plt.savefig(args.plotfile, dpi=150)
                print(f"Saved plot to {args.plotfile}")
            except Exception:
                pass
            plt.show()
        except Exception as e:
            print("Could not plot results (matplotlib missing or error):", e)


if __name__ == "__main__":
    main()

