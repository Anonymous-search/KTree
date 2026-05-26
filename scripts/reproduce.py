#!/usr/bin/env python3
import argparse
import configparser
import csv
import json
import math
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
SUMMARY_FIELDS = [
    "experiment",
    "figure",
    "title",
    "metric",
    "baseline",
    "dataset",
    "query_set",
    "k",
    "build_seconds",
    "wall_clock_seconds",
    "wall_clock_ms_per_query",
    "mean_query_ms",
    "median_query_ms",
    "mean_distance_computations",
    "mean_visit_count",
    "parsed_queries",
    "index_dir",
    "build_command",
    "query_command",
    "reported_csv",
    "runtime_minutes",
]


def resolve_path(base: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else (base / path).resolve()


def load_config(path: Path) -> Dict:
    with path.open("r", encoding="utf-8") as handle:
        config = json.load(handle)
    config["_config_path"] = str(path.resolve())
    config["_base_dir"] = str(path.resolve().parent)
    return config


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def require_existing_file(path: Path, label: str) -> Path:
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")
    return path


def shell_join(parts: Iterable[str]) -> str:
    return shlex.join(str(part) for part in parts)


def run_command(
    cmd: List[str],
    cwd: Path,
    log_path: Path,
    dry_run: bool = False,
    env: Optional[Dict[str, str]] = None,
) -> Tuple[int, float, str]:
    rendered = shell_join(cmd)
    if dry_run:
        ensure_dir(log_path.parent)
        log_path.write_text(f"[dry-run]\n{rendered}\n", encoding="utf-8")
        return 0, 0.0, rendered

    ensure_dir(log_path.parent)
    started = time.perf_counter()
    process = subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    elapsed = time.perf_counter() - started
    log_path.write_text(process.stdout, encoding="utf-8")
    return process.returncode, elapsed, rendered


def mean(values: List[float]) -> Optional[float]:
    return sum(values) / len(values) if values else None


def median(values: List[float]) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2 == 1:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def write_json(path: Path, payload: Dict) -> None:
    ensure_dir(path.parent)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def append_csv(path: Path, row: Dict) -> None:
    ensure_dir(path.parent)
    exists = path.exists()
    with path.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=SUMMARY_FIELDS, extrasaction="ignore"
        )
        if not exists:
            writer.writeheader()
        writer.writerow({field: row.get(field) for field in SUMMARY_FIELDS})


def parse_ktree_query_log(log_text: str) -> Dict[str, Optional[float]]:
    query_times_ms = []
    distance_computations = []
    visit_counts = []
    line_pattern = re.compile(
        r"^\s*(\d+)\s*,\s*([0-9eE.+-]+)\s+s\s*,\s*(\d+)\s*,\s*(\d+)\s*$"
    )
    for line in log_text.splitlines():
        match = line_pattern.match(line.strip())
        if not match:
            continue
        query_times_ms.append(float(match.group(2)) * 1000.0)
        distance_computations.append(float(match.group(3)))
        visit_counts.append(float(match.group(4)))
    return {
        "mean_query_ms": mean(query_times_ms),
        "median_query_ms": median(query_times_ms),
        "mean_distance_computations": mean(distance_computations),
        "mean_visit_count": mean(visit_counts),
        "parsed_queries": len(query_times_ms),
    }


def parse_spartan_tlb_log(log_text: str) -> Dict[str, Optional[float]]:
    csv_match = re.search(r"([^\s]+_tlb_results\.csv)", log_text)
    runtime_match = re.search(r"runtime for .*?:\s*([0-9.]+)min", log_text)
    return {
        "reported_csv": csv_match.group(1) if csv_match else None,
        "runtime_minutes": float(runtime_match.group(1)) if runtime_match else None,
    }


@dataclass
class RunContext:
    config: Dict
    config_path: Path
    base_dir: Path
    workspace_dir: Path
    output_root: Path
    dry_run: bool

    def dataset(self, dataset_id: str) -> Dict:
        return self.config["datasets"][dataset_id]

    def baseline(self, baseline_id: str) -> Dict:
        return self.config["baselines"][baseline_id]


class BaselineAdapter:
    name = "base"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        raise NotImplementedError

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        raise NotImplementedError

    def clean(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = baseline.get("build_dir")
        if build_dir:
            remove_path(resolve_path(ROOT, build_dir), ctx.dry_run)
        binary = baseline.get("binary")
        if binary:
            binary_path = resolve_path(ROOT, binary)
            if binary_path.exists() and binary_path.is_file():
                remove_path(binary_path, ctx.dry_run)


class KTreeAdapter(BaselineAdapter):
    name = "ktree"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = resolve_path(ROOT, baseline["build_dir"])
        configure_log = ctx.output_root / "build_logs" / f"{baseline_id}_cmake.log"
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        code, _, _ = run_command(
            [
                "cmake",
                "-S",
                "ktree",
                "-B",
                str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
            ],
            cwd=ROOT,
            log_path=configure_log,
            dry_run=ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to configure {baseline_id}")
        code, _, _ = run_command(
            ["cmake", "--build", str(build_dir), "-j"],
            cwd=ROOT,
            log_path=build_log,
            dry_run=ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / "index"
        if index_dir.exists():
            shutil.rmtree(index_dir, ignore_errors=True)

        build_log = run_root / "index_build.log"
        build_cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--index",
            str(index_dir),
            "--dataset_size",
            str(dataset["dataset_size"]),
            "--dimensions",
            str(dataset["dimensions"]),
            "--leaf_size",
            str(baseline.get("default_leaf_size", 20000)),
            "--top_k",
            str(k),
            "--mode",
            "index",
        ]
        code, build_seconds, rendered_build = run_command(
            build_cmd, ROOT, build_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"KTree index build failed for {dataset_id}/{query_id}/k={k}"
            )

        query_log = run_root / "query.log"
        query_cmd = [
            str(binary),
            "--queries",
            str(query_path),
            "--index",
            str(index_dir),
            "--queries_size",
            str(query["queries_size"]),
            "--top_k",
            str(k),
            "--mode",
            "query",
        ]
        code, query_seconds, rendered_query = run_command(
            query_cmd, ROOT, query_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"KTree query run failed for {dataset_id}/{query_id}/k={k}"
            )

        parsed = (
            parse_ktree_query_log(query_log.read_text(encoding="utf-8"))
            if not ctx.dry_run
            else {}
        )
        return {
            "build_seconds": build_seconds,
            "wall_clock_seconds": query_seconds,
            "wall_clock_ms_per_query": (
                (query_seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "mean_query_ms": parsed.get("mean_query_ms"),
            "median_query_ms": parsed.get("median_query_ms"),
            "mean_distance_computations": parsed.get("mean_distance_computations"),
            "mean_visit_count": parsed.get("mean_visit_count"),
            "parsed_queries": parsed.get("parsed_queries"),
            "index_dir": str(index_dir),
            "build_command": rendered_build,
            "query_command": rendered_query,
        }


class DumpyAdapter(BaselineAdapter):
    name = "dumpy"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = resolve_path(ROOT, baseline["build_dir"])
        configure_log = ctx.output_root / "build_logs" / f"{baseline_id}_cmake.log"
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        code, _, _ = run_command(
            [
                "cmake",
                "-S",
                "Baselines/dumpy",
                "-B",
                str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
            ],
            cwd=ROOT,
            log_path=configure_log,
            dry_run=ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to configure {baseline_id}")
        code, _, _ = run_command(
            ["cmake", "--build", str(build_dir), "-j"],
            cwd=ROOT,
            log_path=build_log,
            dry_run=ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def _write_config(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        run_root: Path,
    ) -> Path:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        dataset_path = require_existing_file(
            resolve_path(ctx.workspace_dir, dataset["path"]), f"{dataset_id} dataset"
        )
        query_path = require_existing_file(
            resolve_path(ctx.workspace_dir, query["path"]),
            f"{dataset_id}/{query_id} query set",
        )
        template_path = resolve_path(ROOT, baseline["config_template"])

        parser = configparser.ConfigParser(strict=False, interpolation=None)
        parser.optionxform = str
        parser.read(template_path, encoding="utf-8")

        local_name = dataset_id
        if not parser.has_section(local_name):
            parser.add_section(local_name)

        local_dir = ensure_dir(run_root / "artifacts")
        parser["expr"]["dataset"] = local_name
        parser["expr"]["index"] = "1"
        parser["expr"]["ops"] = "0"
        parser["expr"]["query_num"] = str(query["queries_size"])
        parser["expr"]["k"] = str(k)

        parser["parameter"]["th"] = str(baseline.get("threshold", 10000))
        parser["parameter"]["segmentNum"] = str(baseline.get("segment_num", 16))
        parser["parameter"]["bitsCardinality"] = str(
            baseline.get("bits_cardinality", 8)
        )
        parser["parameter"]["fbl_size"] = str(baseline.get("fbl_size_mb", 12480))

        parser["other"]["breakpointsfn"] = str(
            resolve_path(ROOT, "Baselines/dumpy/breakpoints.txt")
        )
        parser["other"]["graphfn"] = str(
            resolve_path(
                ROOT,
                f"Baselines/dumpy/RawGraph_{baseline.get('segment_num', 16)}_{baseline.get('bits_reserve', 3)}.bin",
            )
        )
        parser["other"]["bitsReserve"] = str(baseline.get("bits_reserve", 3))

        parser[local_name]["tsLength"] = str(dataset["dimensions"])
        parser[local_name]["maxK"] = str(max(k, 100))
        parser[local_name]["paafn"] = str(
            (local_dir / f"{dataset_id}.paa.bin").resolve()
        )
        parser[local_name]["saxfn"] = str(
            (local_dir / f"{dataset_id}.sax.bin").resolve()
        )
        parser[local_name]["idxfn"] = str((local_dir / "index").resolve()) + "/"
        parser[local_name]["fuzzyidxfn"] = (
            str((local_dir / "fuzzy-index").resolve()) + "/"
        )
        parser[local_name]["memoryidxfn"] = str((local_dir / "memory").resolve()) + "/"
        parser[local_name]["datafn"] = str(dataset_path)
        parser[local_name]["queryfn"] = str(query_path)

        config_path = run_root / "dumpy.generated.ini"
        with config_path.open("w", encoding="utf-8") as handle:
            parser.write(handle)
        return config_path

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        config_path = self._write_config(
            ctx, baseline_id, dataset_id, query_id, k, run_root
        )

        env = dict(os.environ)
        dumpy_workdir = resolve_path(ROOT, "Baselines/dumpy/src")
        env["PWD"] = str(dumpy_workdir)
        original_config = resolve_path(ROOT, "Baselines/dumpy/config.ini")
        backup_path = run_root / "config.backup.ini"
        if original_config.exists() and not ctx.dry_run:
            shutil.copyfile(original_config, backup_path)
            shutil.copyfile(config_path, original_config)

        try:
            skeleton_log = run_root / "graph.log"
            graph_cmd = [str(binary)]
            if not ctx.dry_run:
                parser = configparser.ConfigParser(strict=False, interpolation=None)
                parser.optionxform = str
                parser.read(config_path, encoding="utf-8")
                parser["expr"]["index"] = "0"
                parser["expr"]["ops"] = "0"
                with original_config.open("w", encoding="utf-8") as handle:
                    parser.write(handle)
            code, graph_seconds, rendered_graph = run_command(
                graph_cmd, dumpy_workdir, skeleton_log, ctx.dry_run, env
            )
            if code != 0:
                raise RuntimeError(
                    f"Dumpy skeleton graph construction failed for {dataset_id}"
                )

            if not ctx.dry_run:
                parser = configparser.ConfigParser(strict=False, interpolation=None)
                parser.optionxform = str
                parser.read(config_path, encoding="utf-8")
                parser["expr"]["index"] = "1"
                parser["expr"]["ops"] = "0"
                with original_config.open("w", encoding="utf-8") as handle:
                    parser.write(handle)
            index_log = run_root / "index_build.log"
            code, build_seconds, rendered_build = run_command(
                [str(binary)], dumpy_workdir, index_log, ctx.dry_run, env
            )
            if code != 0:
                raise RuntimeError(f"Dumpy index build failed for {dataset_id}")

            if not ctx.dry_run:
                parser = configparser.ConfigParser(strict=False, interpolation=None)
                parser.optionxform = str
                parser.read(config_path, encoding="utf-8")
                parser["expr"]["index"] = "1"
                parser["expr"]["ops"] = "2"
                with original_config.open("w", encoding="utf-8") as handle:
                    parser.write(handle)
            query_log = run_root / "query.log"
            code, query_seconds, rendered_query = run_command(
                [str(binary)], dumpy_workdir, query_log, ctx.dry_run, env
            )
            if code != 0:
                raise RuntimeError(
                    f"Dumpy query run failed for {dataset_id}/{query_id}/k={k}"
                )
        finally:
            if not ctx.dry_run and backup_path.exists():
                shutil.copyfile(backup_path, original_config)

        return {
            "build_seconds": build_seconds,
            "graph_seconds": graph_seconds,
            "wall_clock_seconds": query_seconds,
            "wall_clock_ms_per_query": (
                (query_seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "generated_config": str(config_path),
            "graph_command": rendered_graph,
            "build_command": rendered_build,
            "query_command": rendered_query,
        }


class SofaAdapter(BaselineAdapter):
    name = "sofa"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = resolve_path(ROOT, baseline["build_dir"])
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        cmd = [
            "bash",
            "-lc",
            (
                f"mkdir -p {build_dir / 'obj'} {build_dir / 'lib'} {build_dir / 'bin'} && "
                "for src in "
                "Baselines/SOFA/src/ads/isax_file_loaders.c "
                "Baselines/SOFA/src/ads/isax_first_buffer_layer.c "
                "Baselines/SOFA/src/ads/isax_index.c "
                "Baselines/SOFA/src/ads/isax_node.c "
                "Baselines/SOFA/src/ads/isax_node_buffer.c "
                "Baselines/SOFA/src/ads/isax_node_record.c "
                "Baselines/SOFA/src/ads/isax_node_split.c "
                "Baselines/SOFA/src/ads/isax_query_engine.c "
                "Baselines/SOFA/src/ads/isax_visualize_index.c "
                "Baselines/SOFA/src/ads/pqueue.c "
                "Baselines/SOFA/src/ads/sax/sax.c "
                "Baselines/SOFA/src/ads/sax/ts.c "
                "Baselines/SOFA/src/ads/inmemory_query_engine.c "
                "Baselines/SOFA/src/ads/parallel_inmemory_query_engine.c "
                "Baselines/SOFA/src/ads/inmemory_index_engine.c "
                "Baselines/SOFA/src/ads/parallel_index_engine.c "
                "Baselines/SOFA/src/ads/parallel_query_engine.c "
                "Baselines/SOFA/src/ads/inmemory_topk_engine.c "
                "Baselines/SOFA/src/ads/sfa/dft.c "
                "Baselines/SOFA/src/ads/sfa/sfa.c "
                "Baselines/SOFA/src/ads/calc_utils.c; do "
                f"obj='{build_dir}/obj/'\"$(basename \"${{src%.c}}\")\"'.o'; "
                "gcc -O2 -g -fcommon -I Baselines/SOFA/include -I Baselines/SOFA "
                "-march=native -mavx -mavx2 -msse3 -fopenmp "
                '-c "$src" -o "$obj" || exit 1; '
                "done && "
                f"ar rcs {build_dir}/lib/libads.a {build_dir}/obj/*.o && "
                "gcc -O2 -g -fcommon -I Baselines/SOFA/include -I Baselines/SOFA "
                "-march=native -mavx -mavx2 -msse3 -fopenmp "
                f"Baselines/SOFA/src/utils/MESSI.c -L {build_dir}/lib "
                "-lads -lreadline -lfftw3f -lm -lpthread "
                f"-o {build_dir}/bin/MESSI"
            ),
        ]
        code, _, _ = run_command(cmd, ROOT, build_log, ctx.dry_run)
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def _common_args(self, baseline: Dict, dataset: Dict) -> List[str]:
        cmd = [
            "--timeseries-size",
            str(dataset["dimensions"]),
            "--dataset-size",
            str(dataset["dataset_size"]),
            "--flush-limit",
            str(baseline.get("flush_limit", 300000)),
            "--read-block",
            str(baseline.get("read_block", baseline.get("leaf_size", 20000))),
            "--sax-cardinality",
            str(baseline.get("sax_cardinality", 8)),
            "--queue-number",
            str(baseline.get("queue_number", 36)),
            "--cpu-type",
            str(baseline.get("cpu_type", 36)),
            "--leaf-size",
            str(baseline.get("leaf_size", 20000)),
            "--min-leaf-size",
            str(baseline.get("leaf_size", 20000)),
            "--initial-lbl-size",
            str(baseline.get("leaf_size", 20000)),
        ]
        if baseline.get("simd", False):
            cmd.append("--SIMD")
        if baseline.get("filetype_int", False):
            cmd.append("--filetype-int")
        if baseline.get("apply_z_norm", False):
            cmd.append("--apply-z-norm")
        return cmd

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        configured_function_type = baseline.get("function_type", 0)
        disk_function_type = baseline.get("disk_function_type", 0)
        if configured_function_type == 4:
            raise RuntimeError(
                "SOFA SFA is not supported in the new two-phase disk-index workflow"
            )
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / "index"
        shutil.rmtree(index_dir, ignore_errors=True)
        ensure_dir(index_dir)
        env = os.environ.copy()
        env["HOME"] = str(ensure_dir(Path(tempfile.gettempdir()) / "sofa-home"))

        build_log = run_root / "index_build.log"
        build_cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--index-path",
            str(index_dir) + "/",
            "--function-type",
            str(disk_function_type),
        ]
        build_cmd.extend(self._common_args(baseline, dataset))
        code, build_seconds, rendered_build = run_command(
            build_cmd, ROOT, build_log, ctx.dry_run, env=env
        )
        if code != 0:
            raise RuntimeError(
                f"SOFA index build failed for {baseline_id} on {dataset_id}/{query_id}/k={k}"
            )

        query_log = run_root / "query.log"
        query_cmd = [
            str(binary),
            "--use-index",
            "--index-path",
            str(index_dir) + "/",
            "--queries",
            str(query_path),
            "--queries-size",
            str(query["queries_size"]),
            "--function-type",
            str(disk_function_type),
            "--topk",
            "--k-size",
            str(k),
        ]
        query_cmd.extend(self._common_args(baseline, dataset))
        code, seconds, rendered = run_command(
            query_cmd, ROOT, query_log, ctx.dry_run, env=env
        )
        if code != 0:
            raise RuntimeError(
                f"SOFA run failed for {baseline_id} on {dataset_id}/{query_id}/k={k}"
            )

        return {
            "build_seconds": build_seconds,
            "wall_clock_seconds": seconds,
            "wall_clock_ms_per_query": (
                (seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "index_dir": str(index_dir),
            "build_command": rendered_build,
            "query_command": rendered,
        }


class SpartanAdapter(BaselineAdapter):
    name = "spartan"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        workdir = resolve_path(ROOT, baseline["workdir"])
        venv_dir = ROOT / ".venv-spartan"
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_requirements.log"
        run_command(
            [
                "python3",
                "-m",
                "venv",
                str(venv_dir),
            ],
            ROOT,
            ctx.output_root / "build_logs" / f"{baseline_id}_venv.log",
            ctx.dry_run,
        )
        run_command(
            [str(venv_dir / "bin" / "pip"), "install", "-r", "requirements.txt"],
            workdir,
            build_log,
            ctx.dry_run,
        )

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        dataset = ctx.dataset(dataset_id)
        workdir = resolve_path(ROOT, ctx.baseline(baseline_id)["workdir"])
        venv_python = ROOT / ".venv-spartan" / "bin" / "python"
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        run_log = run_root / "tlb.log"
        env = os.environ.copy()
        env["MPLCONFIGDIR"] = str(
            ensure_dir(Path(tempfile.gettempdir()) / "mpl-spartan")
        )
        cmd = [
            str(venv_python),
            "-m",
            "benchmark.eval_tlb",
            "--data",
            str(
                resolve_path(
                    ctx.workspace_dir,
                    dataset.get(
                        "spartan_data_root",
                        str(resolve_path(ctx.workspace_dir, dataset["path"]).parent),
                    ),
                )
            ),
            "--problem",
            dataset.get("spartan_problem", dataset_id),
            "--alpha_max",
            str(ctx.baseline(baseline_id).get("alpha_max", 16)),
            "--alpha_min",
            str(ctx.baseline(baseline_id).get("alpha_min", 2)),
            "--wordlen_max",
            str(
                min(
                    ctx.baseline(baseline_id).get("wordlen_max", 16),
                    dataset["dimensions"],
                )
            ),
            "--wordlen_min",
            str(ctx.baseline(baseline_id).get("wordlen_min", 2)),
        ]
        code, seconds, rendered = run_command(
            cmd, workdir, run_log, ctx.dry_run, env=env
        )
        if code != 0:
            raise RuntimeError(f"SPARTAN TLB run failed for {dataset_id}")
        parsed = (
            parse_spartan_tlb_log(run_log.read_text(encoding="utf-8"))
            if not ctx.dry_run
            else {}
        )
        return {
            "wall_clock_seconds": seconds,
            "query_command": rendered,
            "reported_csv": parsed.get("reported_csv"),
            "runtime_minutes": parsed.get("runtime_minutes"),
        }

    def clean(self, ctx: RunContext, baseline_id: str) -> None:
        super().clean(ctx, baseline_id)
        remove_path(ROOT / ".venv-spartan", ctx.dry_run)
        remove_path(resolve_path(ROOT, "Baselines/SPARTAN/output"), ctx.dry_run)


class DSTreeAdapter(BaselineAdapter):
    name = "dstree"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        workdir = resolve_path(ROOT, baseline["workdir"])
        configure_log = ctx.output_root / "build_logs" / f"{baseline_id}_configure.log"
        lib_log = ctx.output_root / "build_logs" / f"{baseline_id}_lib.log"
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        code, _, _ = run_command(
            ["bash", "./configure"], workdir, configure_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(f"Failed to configure {baseline_id}")
        code, _, _ = run_command(
            ["make", "CFLAGS=-g -O2 -fcommon", "lib/libdstree.a", "-j1"],
            workdir,
            lib_log,
            ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id} library")
        code, _, _ = run_command(
            ["make", "CFLAGS=-g -O2 -fcommon", "bin/dstree", "-j1"],
            workdir,
            build_log,
            ctx.dry_run,
        )
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / f"index_{int(time.time() * 1000)}"
        shutil.rmtree(index_dir, ignore_errors=True)

        build_log = run_root / "index_build.log"
        build_cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--dataset-size",
            str(dataset["dataset_size"]),
            "--buffer-size",
            str(baseline.get("buffer_size", 64)),
            "--leaf-size",
            str(baseline.get("leaf_size", 100)),
            "--index-path",
            str(index_dir) + "/",
            "--ascii-input",
            "0",
            "--mode",
            "0",
            "--timeseries-size",
            str(dataset["dimensions"]),
        ]
        code, build_seconds, rendered_build = run_command(
            build_cmd, ROOT, build_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"DSTree index build failed for {dataset_id}/{query_id}/k={k}"
            )

        query_log = run_root / "query.log"
        query_cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--queries",
            str(query_path),
            "--queries-size",
            str(query["queries_size"]),
            "--buffer-size",
            str(baseline.get("buffer_size", 64)),
            "--leaf-size",
            str(baseline.get("leaf_size", 100)),
            "--index-path",
            str(index_dir) + "/",
            "--ascii-input",
            "0",
            "--mode",
            "1",
            "--timeseries-size",
            str(dataset["dimensions"]),
            "--k",
            str(k),
            "--epsilon",
            "0",
            "--delta",
            "1",
        ]
        code, query_seconds, rendered_query = run_command(
            query_cmd, ROOT, query_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"DSTree query run failed for {dataset_id}/{query_id}/k={k}"
            )

        return {
            "build_seconds": build_seconds,
            "wall_clock_seconds": query_seconds,
            "wall_clock_ms_per_query": (
                (query_seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "index_dir": str(index_dir),
            "build_command": rendered_build,
            "query_command": rendered_query,
        }

    def clean(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        workdir = resolve_path(ROOT, baseline["workdir"])
        run_clean_command(["make", "clean"], workdir, ctx.dry_run)
        run_clean_command(["make", "distclean"], workdir, ctx.dry_run)
        binary = resolve_path(ROOT, baseline["binary"])
        if binary.exists():
            remove_path(binary, ctx.dry_run)


class MTreeAdapter(BaselineAdapter):
    name = "mtree"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        workdir = resolve_path(ROOT, baseline["workdir"])
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        code, _, _ = run_command(["make", "-j2"], workdir, build_log, ctx.dry_run)
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / "index"
        shutil.rmtree(index_dir, ignore_errors=True)

        run_log = run_root / "query.log"
        cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--queries",
            str(query_path),
            "--queries-size",
            str(query["queries_size"]),
            "--timeseries-size",
            str(dataset["dimensions"]),
            "--dataset-size",
            str(dataset["dataset_size"]),
            "--index-path",
            str(index_dir),
            "--use-index",
        ]
        code, seconds, rendered = run_command(cmd, ROOT, run_log, ctx.dry_run)
        if code != 0:
            raise RuntimeError(f"MTree run failed for {dataset_id}/{query_id}/k={k}")

        return {
            "wall_clock_seconds": seconds,
            "wall_clock_ms_per_query": (
                (seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "index_dir": str(index_dir),
            "query_command": rendered,
        }

    def clean(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        workdir = resolve_path(ROOT, baseline["workdir"])
        run_clean_command(["make", "clean"], workdir, ctx.dry_run)
        binary = resolve_path(ROOT, baseline["binary"])
        if binary.exists():
            remove_path(binary, ctx.dry_run)
        for pattern in ("*.o", "*.a"):
            for path in workdir.glob(pattern):
                remove_path(path, ctx.dry_run)


class RTreeAdapter(BaselineAdapter):
    name = "rtree"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = resolve_path(ROOT, baseline["build_dir"])
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        cmd = [
            "bash",
            "-lc",
            (
                f"mkdir -p {build_dir} && "
                "g++ -O2 -std=c++98 -Wall -Wno-long-long -pedantic "
                "-I Baselines/rtree/include "
                "-I Baselines/rtree/include/spatialindex "
                "-I Baselines/rtree/src "
                "-I Baselines/rtree/test/rtree "
                "Baselines/rtree/src/spatialindex/*.cc "
                "Baselines/rtree/src/storagemanager/*.cc "
                "Baselines/rtree/src/rtree/*.cc "
                "Baselines/rtree/src/tools/*.cc "
                f"Baselines/rtree/test/rtree/RTreeBulkLoad.cc -o {build_dir}/RTreeBulkLoad -lpthread"
            ),
        ]
        code, _, _ = run_command(cmd, ROOT, build_log, ctx.dry_run)
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / "index"
        shutil.rmtree(index_dir, ignore_errors=True)
        ensure_dir(index_dir)

        build_log = run_root / "index_build.log"
        build_cmd = [
            str(binary),
            str(dataset_path),
            str(index_dir),
            str(dataset["dataset_size"]),
            str(baseline.get("capacity", 1000)),
            "0",
        ]
        code, build_seconds, rendered_build = run_command(
            build_cmd, ROOT, build_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"RTree index build failed for {dataset_id}/{query_id}/k={k}"
            )

        query_log = run_root / "query.log"
        query_cmd = [
            str(binary),
            str(query_path),
            str(index_dir),
            str(query["queries_size"]),
            str(dataset["dataset_size"]),
            "1",
        ]
        code, query_seconds, rendered_query = run_command(
            query_cmd, ROOT, query_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"RTree query run failed for {dataset_id}/{query_id}/k={k}"
            )

        return {
            "build_seconds": build_seconds,
            "wall_clock_seconds": query_seconds,
            "wall_clock_ms_per_query": (
                (query_seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "index_dir": str(index_dir),
            "build_command": rendered_build,
            "query_command": rendered_query,
        }


class VAPlusAdapter(BaselineAdapter):
    name = "vaplus"

    def build(self, ctx: RunContext, baseline_id: str) -> None:
        baseline = ctx.baseline(baseline_id)
        build_dir = resolve_path(ROOT, baseline["build_dir"])
        workdir = resolve_path(ROOT, "Baselines/vaplus")
        configure_log = ctx.output_root / "build_logs" / f"{baseline_id}_configure.log"
        build_log = ctx.output_root / "build_logs" / f"{baseline_id}_build.log"
        code, _, _ = run_command(
            ["bash", "./configure"], workdir, configure_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(f"Failed to configure {baseline_id}")
        cmd = [
            "bash",
            "-lc",
            (
                f"mkdir -p {build_dir} && "
                "gcc -O2 -g -fcommon "
                "-I Baselines/vaplus -I Baselines/vaplus/include "
                "Baselines/vaplus/src/vaplus.c "
                "Baselines/vaplus/src/pqueue.c "
                "Baselines/vaplus/src/vaplus_file_loaders.c "
                "Baselines/vaplus/src/vaplus_index.c "
                "Baselines/vaplus/src/vaplus_node.c "
                "Baselines/vaplus/src/ts.c "
                "Baselines/vaplus/src/calc_utils.c "
                "Baselines/vaplus/src/vaplus_query_engine.c "
                "Baselines/vaplus/src/vaplus_file_buffer.c "
                "Baselines/vaplus/src/vaplus_file_buffer_manager.c "
                f"Baselines/vaplus/src/dft.c -o {build_dir}/vaplus "
                "-lreadline -lfftw3 -lfftw3f -ljemalloc -lm"
            ),
        ]
        code, _, _ = run_command(cmd, ROOT, build_log, ctx.dry_run)
        if code != 0:
            raise RuntimeError(f"Failed to build {baseline_id}")

    def run(
        self,
        ctx: RunContext,
        baseline_id: str,
        dataset_id: str,
        query_id: str,
        k: int,
        experiment_id: str,
    ) -> Dict:
        baseline = ctx.baseline(baseline_id)
        dataset = ctx.dataset(dataset_id)
        query = dataset["queries"][query_id]
        binary = resolve_path(ROOT, baseline["binary"])
        dataset_path = resolve_path(ctx.workspace_dir, dataset["path"])
        query_path = resolve_path(ctx.workspace_dir, query["path"])
        run_root = ensure_dir(
            ctx.output_root
            / "runs"
            / experiment_id
            / baseline_id
            / dataset_id
            / query_id
            / f"k{k}"
        )
        index_dir = run_root / "index"
        shutil.rmtree(index_dir, ignore_errors=True)

        build_log = run_root / "index_build.log"
        build_cmd = [
            str(binary),
            "--dataset",
            str(dataset_path),
            "--dataset-size",
            str(dataset["dataset_size"]),
            "--index-path",
            str(index_dir) + "/",
            "--mode",
            "0",
            "--ascii-input",
            "0",
            "--timeseries-size",
            str(dataset["dimensions"]),
            "--buffer-size",
            str(baseline.get("buffer_size", 5400)),
            "--leaf-size",
            str(baseline.get("leaf_size", 1000)),
        ]
        code, build_seconds, rendered_build = run_command(
            build_cmd, ROOT, build_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"VAPlus index build failed for {dataset_id}/{query_id}/k={k}"
            )

        query_log = run_root / "query.log"
        query_cmd = [
            str(binary),
            "--queries",
            str(query_path),
            "--queries-size",
            str(query["queries_size"]),
            "--index-path",
            str(index_dir) + "/",
            "--mode",
            "1",
        ]
        code, query_seconds, rendered_query = run_command(
            query_cmd, ROOT, query_log, ctx.dry_run
        )
        if code != 0:
            raise RuntimeError(
                f"VAPlus query run failed for {dataset_id}/{query_id}/k={k}"
            )

        return {
            "build_seconds": build_seconds,
            "wall_clock_seconds": query_seconds,
            "wall_clock_ms_per_query": (
                (query_seconds * 1000.0 / query["queries_size"])
                if query["queries_size"]
                else None
            ),
            "index_dir": str(index_dir),
            "build_command": rendered_build,
            "query_command": rendered_query,
        }


ADAPTERS = {
    "ktree": KTreeAdapter(),
    "dumpy": DumpyAdapter(),
    "dstree": DSTreeAdapter(),
    "mtree": MTreeAdapter(),
    "rtree": RTreeAdapter(),
    "sofa": SofaAdapter(),
    "spartan": SpartanAdapter(),
    "vaplus": VAPlusAdapter(),
}


def remove_path(path: Path, dry_run: bool) -> None:
    if not path.exists() and not path.is_symlink():
        return
    print(f"[clean] remove {path}")
    if dry_run:
        return
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path, ignore_errors=True)
    else:
        path.unlink(missing_ok=True)


def run_clean_command(cmd: List[str], cwd: Path, dry_run: bool) -> None:
    print(f"[clean] {cwd}$ {shell_join(cmd)}")
    if dry_run:
        return
    subprocess.run(
        cmd,
        cwd=str(cwd),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def get_enabled_baselines(config: Dict) -> List[str]:
    return [
        name
        for name, config in config["baselines"].items()
        if config.get("enabled", True)
    ]


def iter_selected_experiments(
    config: Dict, selection: Optional[List[str]]
) -> Iterable[Tuple[str, Dict]]:
    experiments = config["experiments"]
    if not selection:
        yield from experiments.items()
        return
    for experiment_id in selection:
        if experiment_id not in experiments:
            raise KeyError(f"Unknown experiment: {experiment_id}")
        yield experiment_id, experiments[experiment_id]


def run_build(args: argparse.Namespace, ctx: RunContext) -> None:
    baseline_ids = (
        get_enabled_baselines(ctx.config)
        if args.baseline == "all"
        else [args.baseline]
    )
    for baseline_id in baseline_ids:
        baseline = ctx.baseline(baseline_id)
        adapter = ADAPTERS[baseline["adapter"]]
        print(f"[build] {baseline_id}")
        adapter.build(ctx, baseline_id)


def run_clean(args: argparse.Namespace, ctx: RunContext) -> None:
    remove_path(ctx.output_root, ctx.dry_run)
    baseline_ids = list(ctx.config["baselines"].keys())
    for baseline_id in baseline_ids:
        baseline = ctx.baseline(baseline_id)
        adapter = ADAPTERS[baseline["adapter"]]
        print(f"[clean] {baseline_id}")
        adapter.clean(ctx, baseline_id)


def run_experiments(args: argparse.Namespace, ctx: RunContext) -> None:
    summary_csv = ctx.output_root / "results.csv"
    for experiment_id, experiment in iter_selected_experiments(
        ctx.config, args.experiment
    ):
        dataset_id = experiment["dataset"]
        baselines = experiment["baselines"]
        query_sets = experiment["query_sets"]
        k_values = experiment["k_values"]
        print(f"[experiment] {experiment_id}")
        for baseline_id in baselines:
            baseline = ctx.baseline(baseline_id)
            if not baseline.get("enabled", True):
                print(f"  - skip {baseline_id} (disabled in config)")
                continue
            adapter = ADAPTERS[baseline["adapter"]]
            for query_id in query_sets:
                for k in k_values:
                    print(f"  - {baseline_id} {dataset_id} {query_id} k={k}")
                    metrics = adapter.run(
                        ctx, baseline_id, dataset_id, query_id, k, experiment_id
                    )
                    record = {
                        "experiment": experiment_id,
                        "figure": experiment.get("figure"),
                        "title": experiment.get("title"),
                        "metric": experiment.get("metric"),
                        "baseline": baseline_id,
                        "dataset": dataset_id,
                        "query_set": query_id,
                        "k": k,
                    }
                    record.update(metrics)
                    run_dir = (
                        ctx.output_root
                        / "runs"
                        / experiment_id
                        / baseline_id
                        / dataset_id
                        / query_id
                        / f"k{k}"
                    )
                    write_json(run_dir / "result.json", record)
                    append_csv(summary_csv, record)


def run_list(args: argparse.Namespace, ctx: RunContext) -> None:
    print("Baselines:")
    for baseline_id in get_enabled_baselines(ctx.config):
        baseline = ctx.baseline(baseline_id)
        print(f"  - {baseline_id} ({baseline['adapter']})")
    print("\nExperiments:")
    for experiment_id, experiment in ctx.config["experiments"].items():
        print(
            f"  - {experiment_id}: dataset={experiment['dataset']} query_sets={','.join(experiment['query_sets'])} k={experiment['k_values']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Reproducibility runner for baselines."
    )
    parser.add_argument(
        "--config",
        required=True,
        help="Path to the config JSON file.",
    )
    parser.add_argument(
        "--output-root",
        default=None,
        help="Override output directory from the config.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print and log commands without executing them.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser(
        "list", help="List enabled baselines and configured experiments."
    )
    list_parser.set_defaults(func=run_list)

    build_parser_cmd = subparsers.add_parser(
        "build", help="Build one or all baselines."
    )
    build_parser_cmd.add_argument("baseline", help="Baseline id or 'all'.")
    build_parser_cmd.set_defaults(func=run_build)

    clean_parser = subparsers.add_parser(
        "clean", help="Clean outputs and all baseline build artifacts."
    )
    clean_parser.set_defaults(func=run_clean)

    run_parser = subparsers.add_parser(
        "run", help="Run one or all configured experiments."
    )
    run_parser.add_argument(
        "--experiment", action="append", help="Experiment id to run. Can be repeated."
    )
    run_parser.set_defaults(func=run_experiments)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    config_path = Path(args.config).resolve()
    config = load_config(config_path)
    base_dir = Path(config["_base_dir"])
    workspace_dir = resolve_path(
        base_dir, config.get("paths", {}).get("workspace", ".")
    )
    output_root_value = args.output_root or config.get("paths", {}).get(
        "output_root", "repro/out"
    )
    output_root = resolve_path(base_dir, output_root_value)
    ctx = RunContext(
        config=config,
        config_path=config_path,
        base_dir=base_dir,
        workspace_dir=workspace_dir,
        output_root=output_root,
        dry_run=args.dry_run,
    )
    args.func(args, ctx)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
