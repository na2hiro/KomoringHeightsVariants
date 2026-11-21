"""Compare USI RuleSet builds with the legacy standard and fixed-rule builds."""

import argparse
import math
from pathlib import Path
import re
import statistics
import subprocess
import time

from verify_variants import Engine, RULES


# Indices in Haitaka 87a106b haitaka/examples/dfpn_corpus.rs, in source order.
# The first case in each pair is for the node limit, the second is for solving.
WORKLOADS = {
    "standard": (7, 2), "annan": (7, 2), "anhoku": (7, 0),
    "antouzai": (7, 4), "taimen": (7, 5), "haimen": (15, 17),
    "anki": (15, 14), "tenjiku": (7, 8), "neko": (7, 3),
    "nekoneko": (7, 2), "yokoneko": (7, 4), "yokonekoneko": (7, 10),
    "tenkyo": (7, 15),
}
NODE_LIMIT = 30_000
MEASURE_AT_NODE = 15_000
PAIRS = 30


def cases(haitaka):
    corpus = subprocess.check_output([
        "git", "-C", str(haitaka), "show", "87a106b:haitaka/examples/dfpn_corpus.rs",
    ], text=True)
    return re.findall(r'sfen: "([^"]+)"', corpus)


def sample(binary, rule, selectable, sfen, node_limit):
    with Engine(binary) as engine:
        if selectable:
            engine.send(f"setoption name RuleSet value {rule}")
        engine.send(
            "setoption name Threads value 1",
            "setoption name USI_Hash value 64",
            f"setoption name PvInterval value {1 if node_limit else 0}",
            f"setoption name NodesLimit value {node_limit}",
            "isready",
        )
        engine.read_until(lambda line: line == "readyok")
        diagnostics = engine.query(f"position sfen {sfen}")
        if diagnostics:
            raise RuntimeError((rule, sfen, diagnostics))

        start = time.perf_counter_ns()
        engine.send("go mate 10000")
        observations = [(0, 0)]
        for line in engine.process.stdout:
            elapsed = time.perf_counter_ns() - start
            if line.startswith("checkmate"):
                result = line.strip()
                break
            match = re.search(r"\bnodes (\d+)", line)
            if match and int(match[1]) > observations[-1][0]:
                observations.append((int(match[1]), elapsed))
        else:
            raise RuntimeError((rule, sfen, "engine exited without checkmate"))
        if not node_limit:
            if result == "checkmate timeout":
                raise RuntimeError((rule, sfen, "solution timed out"))
            return elapsed, result

        # Interpolate between the two progress reports around a fixed count.
        # NodesLimit itself is polled and can overshoot by different amounts.
        for (n1, t1), (n2, t2) in zip(observations, observations[1:]):
            if n1 <= MEASURE_AT_NODE <= n2:
                fraction = (MEASURE_AT_NODE - n1) / (n2 - n1)
                return t1 + fraction * (t2 - t1), "node sample"
        raise RuntimeError((rule, sfen, result, observations[-1][0], "node sample was not reached"))


def compare(binary, fixed, rule, sfen, node_limit, pairs):
    def run(path, selectable):
        return sample(path, rule, selectable, sfen, node_limit)

    run(binary, True)
    run(fixed, False)
    ratios = []
    for i in range(pairs):
        if i % 2 == 0:
            candidate, actual = run(binary, True)
            baseline, expected = run(fixed, False)
        else:
            baseline, expected = run(fixed, False)
            candidate, actual = run(binary, True)
        if actual != expected:
            raise RuntimeError((rule, sfen, actual, expected))
        ratios.append(math.log(candidate / baseline))
    mean = statistics.mean(ratios)
    # Two-sided 95% Student's t critical value for the minimum sample of 30 pairs.
    half_width = 2.045 * statistics.stdev(ratios) / math.sqrt(pairs)
    return math.exp(mean), math.exp(mean - half_width), math.exp(mean + half_width)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("all", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("fixed_dir", type=Path)
    parser.add_argument("haitaka", type=Path)
    parser.add_argument("--rule", choices=["standard", *RULES], action="append")
    parser.add_argument("--pairs", type=int, default=PAIRS, help="use 30 for the release comparison")
    args = parser.parse_args()
    if args.pairs < PAIRS:
        parser.error(f"--pairs must be at least {PAIRS}")
    positions = cases(args.haitaka)
    passed = True
    for rule in args.rule or ("standard", *RULES):
        fixed = args.baseline if rule == "standard" else args.fixed_dir / rule / f"KomoringHeights-by-gcc-{rule}"
        timed = []
        for index, limit in zip(WORKLOADS[rule], (NODE_LIMIT, 0)):
            timed.append(compare(args.all, fixed, rule, positions[index], limit, args.pairs))
        ok = all(upper <= 1.02 for _, _, upper in timed)
        passed &= ok
        result = " | ".join(f"{ratio:.4f} [{lo:.4f}, {hi:.4f}]" for ratio, lo, hi in timed)
        print(f"{rule} | {result} | {fixed.stat().st_size} | {'pass' if ok else 'fail'}", flush=True)
    print(f"Combined binary: {args.all.stat().st_size} bytes. Gate: {'pass' if passed else 'fail'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
