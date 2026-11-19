#!/usr/bin/env python3
"""
Throw damaged CAD files at cascadio and keep the ones that break it.

    python scripts/fuzz.py [--seconds 60] [--jobs 3] [--seed 0] [SEED_FILES...]

Each round takes a seed file (the test models by default), damages it in
a random way and converts it. Bad input is supposed to raise ValueError,
or RuntimeError when OpenCASCADE gives up. Three outcomes are findings,
and the input that caused them is saved to `fuzz-findings/`:

    crash    the worker died (a segmentation fault, an abort, a sanitizer
             report); cascadio survives these, but they are bugs
    timeout  the conversion ran past the limit
    memory   the conversion ran past the memory limit

Run it through `make bughunt`, which uses a worker built with sanitizers so
that memory errors are caught when they happen instead of when they
happen to crash.
"""

import argparse
import hashlib
import random
import re
import threading
import time
from collections import Counter
from pathlib import Path

import cascadio

ROOT = Path(__file__).resolve().parent.parent
FINDINGS = ROOT / "fuzz-findings"
NUMBER = re.compile(rb"-?\d+\.\d*(?:E[+-]?\d+)?")
REFERENCE = re.compile(rb"#\d+")
# values that tend to upset geometry code
NASTY = [b"0.", b"-0.", b"1.E308", b"-1.E308", b"1.E-320", b"NaN", b"1.E999", b""]


def mutate(data: bytes, rng: random.Random) -> bytes:
    """One random kind of damage."""
    kind = rng.randrange(7)
    if kind == 0 and data:  # flip a few bytes
        out = bytearray(data)
        for _ in range(rng.randint(1, 8)):
            out[rng.randrange(len(out))] = rng.randrange(256)
        return bytes(out)
    if kind == 1:  # cut the end off
        return data[: rng.randrange(len(data) + 1)]
    if kind == 2 and len(data) > 2:  # delete a chunk
        a, b = sorted(rng.sample(range(len(data)), 2))
        return data[:a] + data[b:]
    if kind == 3 and len(data) > 2:  # repeat a chunk
        a, b = sorted(rng.sample(range(len(data)), 2))
        return data[:b] + data[a:b] * rng.randint(1, 3) + data[b:]

    # the rest only make sense for the text formats
    def some(pattern, replacement):
        hits = list(pattern.finditer(data))
        if not hits:
            return data
        out, last = [], 0
        for hit in sorted(
            rng.sample(hits, min(len(hits), rng.randint(1, 5))), key=lambda h: h.start()
        ):
            out += [data[last : hit.start()], replacement(hit)]
            last = hit.end()
        return b"".join([*out, data[last:]])

    if kind == 4:  # replace numbers with nasty ones
        return some(NUMBER, lambda _: rng.choice(NASTY))
    if kind == 5:  # point entity references somewhere else
        return some(REFERENCE, lambda _: b"#%d" % rng.randrange(1, 5000))
    # scale numbers enormously
    return some(
        NUMBER, lambda hit: hit.group() + rng.choice([b"E30", b"E-30", b"E300"])
    )


def classify(data: bytes, file_type: str, timeout: float, memory_limit: int) -> str:
    try:
        cascadio.load(
            data,
            file_type=file_type,
            use_parallel=False,
            include_brep=True,
            timeout=timeout,
            memory_limit=memory_limit,
        )
        return "converted"
    except ValueError:
        return "rejected"
    except TimeoutError:
        return "timeout"
    except MemoryError:
        return "memory"
    except RuntimeError as error:
        return "crash" if "crashed" in str(error) else "failed"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("seeds", nargs="*", type=Path)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--memory-limit", type=int, default=4 * 2**30)
    args = parser.parse_args()

    paths = args.seeds or sorted((ROOT / "tests" / "models").iterdir())
    seeds = [
        (
            p.read_bytes(),
            "iges"
            if p.name.lower().split(".zst")[0].endswith((".igs", ".iges"))
            else "step",
        )
        for p in paths
    ]
    outcomes = Counter()
    lock = threading.Lock()
    deadline = time.monotonic() + args.seconds

    def work(worker: int) -> None:
        rng = random.Random(args.seed * 1000 + worker)
        while time.monotonic() < deadline:
            data, file_type = rng.choice(seeds)
            for _ in range(rng.randint(1, 3)):
                data = mutate(data, rng)
            outcome = classify(data, file_type, args.timeout, args.memory_limit)
            with lock:
                outcomes[outcome] += 1
                if outcome in ("crash", "timeout", "memory"):
                    FINDINGS.mkdir(exist_ok=True)
                    name = (
                        f"{outcome}-{hashlib.sha1(data).hexdigest()[:12]}.{file_type}"
                    )
                    (FINDINGS / name).write_bytes(data)

    threads = [threading.Thread(target=work, args=(i,)) for i in range(args.jobs)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    total = sum(outcomes.values())
    print(f"{total} damaged files in {args.seconds:.0f} s")
    for outcome in ("converted", "rejected", "failed", "crash", "timeout", "memory"):
        print(f"  {outcome:<10} {outcomes[outcome]}")
    found = outcomes["crash"] + outcomes["timeout"] + outcomes["memory"]
    if found:
        print(f"{found} findings saved to {FINDINGS.relative_to(ROOT)}/")


if __name__ == "__main__":
    main()
