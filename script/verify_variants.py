"""Check KomoringHeights through USI, optionally against Haitaka's rule_oracle."""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import subprocess
from tempfile import NamedTemporaryFile


RULES = (
    "annan", "anhoku", "antouzai", "taimen", "haimen", "anki", "tenjiku",
    "neko", "nekoneko", "yokoneko", "yokonekoneko", "tenkyo",
)

ANNAN_START = "lnsgkgsnl/1r5b1/p1ppppp1p/1p5p1/9/1P5P1/P1PPPPP1P/1B5R1/LNSGKGSNL b - 1"
STANDARD_START = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1"
MATE = "8k/9/9/6B2/9/9/9/9/9 b G 1"
NO_MATE = "8k/9/9/9/9/9/9/9/K8 b - 1"


class Engine:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [str(binary.resolve())],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.process.poll() is None:
            self.send("quit")
        self.process.communicate(timeout=10)

    def send(self, *commands):
        self.process.stdin.write("\n".join(commands) + "\n")
        self.process.stdin.flush()

    def read_until(self, expected):
        lines = []
        for line in self.process.stdout:
            line = line.strip()
            if expected(line):
                return line, lines
            lines.append(line)
        raise RuntimeError(f"Engine exited with {self.process.wait()} while reading {lines!r}")

    def query(self, *commands):
        self.send(*commands, "side")
        _, lines = self.read_until(lambda line: line in ("black", "white"))
        return lines

    def set_rule(self, rule, threads=1):
        self.send(
            f"setoption name Threads value {threads}",
            "setoption name USI_Hash value 16",
            "setoption name PvInterval value 0",
            f"setoption name RuleSet value {rule}",
            "isready",
        )
        self.read_until(lambda line: line == "readyok")

    def moves(self, sfen):
        lines = self.query(f"position sfen {sfen}", "moves")
        assert len(lines) == 1, (sfen, lines)
        moves = lines[0].split()
        assert len(set(moves)) == len(moves), (sfen, moves)
        return set(moves)

    def sfen(self, position):
        lines = self.query(position, "d")
        return next(line[5:] for line in lines if line.startswith("sfen "))

    def mate(self, sfen):
        self.send(f"position sfen {sfen}", "go mate 5000")
        result, _ = self.read_until(lambda line: line.startswith("checkmate"))
        return result.split()[1:]


def protocol_tests(engine):
    engine.send("usi")
    _, options = engine.read_until(lambda line: line == "usiok")
    rule_option = next(line for line in options if line.startswith("option name RuleSet"))
    assert rule_option.split(" var ")[1:] == ["standard", *RULES], rule_option

    for rule in (*RULES, "standard", "annan"):
        engine.set_rule(rule)
        start = ANNAN_START if rule == "annan" else STANDARD_START
        assert engine.sfen("position startpos") == start, rule
        assert engine.mate(MATE) == ["G*1b"], rule
        assert engine.mate(NO_MATE) == ["nomate"], rule
        engine.send("go mate 5000")
        result, _ = engine.read_until(lambda line: line.startswith("checkmate"))
        assert result == "checkmate nomate", (rule, result)

    position = "8k/9/9/9/9/9/9/9/K8 b PLN 1"
    assert {"P*5a", "L*5a", "N*5a"} <= engine.moves(position)
    for rule in ("anhoku", "taimen"):
        engine.set_rule(rule)
        moves = engine.moves(position)
        assert not {"P*5a", "L*5a", "N*5a"} & moves
        assert "N*5b" in moves

    engine.set_rule("taimen")
    assert "R*5f" not in engine.moves("4K4/9/9/9/4p4/9/9/9/8k b R 1")
    assert "G*5f" in engine.moves("4K4/9/9/9/4r4/9/9/9/8k b G 1")

    engine.set_rule("annan")
    side_pawn = engine.moves("8k/9/4P4/4G4/5P3/9/9/9/K8 b - 1")
    assert "5c4c+" in side_pawn and "5c4c" not in side_pawn
    king_moves = engine.moves("8k/9/9/9/5K3/5R3/9/9/9 b - 1")
    assert "4e4b" in king_moves and "4e4b+" not in king_moves

    # Dropping the pawn gives another piece check. A mate by the pawn itself is illegal.
    pawn_donor = "8k/8B/5B1S1/9/9/9/9/9/9 b P 1"
    assert "P*1c" in engine.moves(pawn_donor)
    assert "P*1b" not in engine.moves("8k/9/5B1S1/9/9/9/9/9/9 b P 1")
    assert engine.query(f"position sfen {pawn_donor} moves P*1c", "mated") == ["1"]
    assert engine.mate(pawn_donor) not in (["nomate"], ["timeout"], [])
    # Five-ply problem from Haitaka's Annan dfpn_corpus, with both hands explicit.
    assert len(engine.mate("7nk/6+r2/7Bp/9/9/9/9/9/9 b GNrb3g4s2n4l17p 1")) == 5
    cycle = "8k/9/8R/9/9/9/9/9/9 w -"
    assert engine.sfen(f"position sfen {cycle} 1 moves 1a2a 1c2c 2a1a 2c1c") == f"{cycle} 5"
    assert engine.mate(f"{cycle} 1") == ["nomate"]

    engine.set_rule("antouzai")
    assert {"5e5a", "5e1a"} <= engine.moves("9/k8/9/9/3RGB3/9/9/9/8K b - 1")
    assert "7b8d" in engine.moves("2k6/2gn5/9/1RG6/9/9/9/9/K8 w - 1")

    for rule in ("neko", "nekoneko"):
        engine.set_rule(rule, threads=2)
        # Extending the run makes the bishop on 1c borrow the rook's movement.
        sfen = "8k/9/5B1SB/8G/9/9/9/9/9 b R 1"
        assert engine.query(f"position sfen {sfen} moves R*1e", "mated") == ["1"], rule
        assert engine.mate(sfen) not in (["nomate"], ["timeout"], []), rule

    engine.send("setoption name RuleSet value tenkyo", "go mate 1000")
    result, _ = engine.read_until(lambda line: line.startswith("checkmate"))
    assert result == "checkmate timeout", result
    engine.set_rule("tenkyo", threads=2)
    engine.send("position sfen invalid", "go mate 1000")
    result, diagnostics = engine.read_until(lambda line: line.startswith("checkmate"))
    assert result == "checkmate timeout" and "info string Invalid SFEN for RuleSet" in diagnostics
    hard = "9/9/3+p1+p3/2+p1+p1L2/4P3k/3+B5/1G7/B5S2/5RR2 b 2S3gs4n3l13p 1"
    engine.send(f"position sfen {hard}", "go mate infinite", "stop")
    result, _ = engine.read_until(lambda line: line.startswith("checkmate"))
    assert result == "checkmate timeout", result
    assert engine.mate(MATE) == ["G*1b"]
    engine.send(f"position sfen {MATE}", "go")
    result, _ = engine.read_until(lambda line: line.startswith("bestmove"))
    assert result == "bestmove G*1b", result
    print("USI cases passed", flush=True)


def antouzai_false_pin(sfen, move):
    # Haitaka 87a106b rejects these pinned captures before applying the move.
    # Each capture takes the piece that supplies the slider movement.
    patterns = {
        "7b8d": {"7a": "k", "7b": "g", "6b": "n", "8d": "R", "7d": "G"},
        "3d3e": {"6a": "k", "3d": "g", "3e": "B", "2e": "P"},
    }
    if move not in patterns:
        return False
    pieces = {}
    for rank, row in zip("abcdefghi", sfen.split()[0].split("/")):
        file = 9
        for token in re.findall(r"\+?[A-Za-z]|[1-9]", row):
            if token.isdigit():
                file -= int(token)
            else:
                pieces[f"{file}{rank}"] = token
                file -= 1
    return all(pieces.get(square) == piece for square, piece in patterns[move].items())


def check_oracle(engine, rule, binary, count):
    engine.set_rule(rule)
    positions = defaultdict(dict)
    transitions = subprocess.check_output([str(binary), str(count)], text=True)
    for row in transitions.splitlines():
        sfen, move, child, _status = row.split("\t")
        moves = positions[sfen]
        if move != "=":
            moves[move] = child

    false_pins = []
    for sfen, expected in positions.items():
        legal = engine.moves(sfen)
        extra = legal - expected.keys()
        missing = expected.keys() - legal
        assert not missing and all(rule == "antouzai" and antouzai_false_pin(sfen, m) for m in extra), (
            rule, sfen, f"extra {sorted(legal - expected.keys())}",
            f"missing {sorted(expected.keys() - legal)}",
        )
        for move in extra:
            false_pins.append(engine.sfen(f"position sfen {sfen} moves {move}"))
        for move, child in expected.items():
            actual = engine.sfen(f"position sfen {sfen} moves {move}")
            assert actual == child, (rule, sfen, move, actual, child)
    if false_pins:
        # Parsing the resulting positions also checks the previous mover's king in Haitaka.
        with NamedTemporaryFile(mode="w") as positions_file:
            positions_file.write("\n".join(false_pins) + "\n")
            positions_file.flush()
            replay = subprocess.check_output([str(binary), "0", positions_file.name], text=True)
        parsed = {row.split("\t", 1)[0] for row in replay.splitlines() if "\t=\t" in row}
        assert parsed == set(false_pins)
    extra = f", {len(false_pins)} false-pin captures verified separately" if false_pins else ""
    print(f"{rule}: {len(positions)} positions, {sum(map(len, positions.values()))} transitions{extra}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--oracle-dir", type=Path, help="directory of rule_oracle binaries named after each rule")
    parser.add_argument("--positions", type=int, default=256)
    args = parser.parse_args()
    with Engine(args.engine) as engine:
        protocol_tests(engine)
        if args.oracle_dir:
            for rule in RULES:
                check_oracle(engine, rule, (args.oracle_dir / rule).resolve(), args.positions)


if __name__ == "__main__":
    main()
