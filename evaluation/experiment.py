#!/usr/bin/env python3
import subprocess
import os
import time
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from pathlib import Path
import logging
from typing import List, Dict, Any
import json

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class RastaExperimentRunner:
	def __init__(self, rasta_exe: str = "RastaConverter.exe", input_file: str = "test.jpg"):
		self.rasta_exe = Path(rasta_exe)
		self.input_file = Path(input_file)
		self.results_dir = Path("experiment_results")
		self.results_dir.mkdir(exist_ok=True)

		if not self.rasta_exe.exists():
			raise FileNotFoundError(f"RastaConverter executable not found: {self.rasta_exe}")
		if not self.input_file.exists():
			raise FileNotFoundError(f"Input file not found: {self.input_file}")

	def run_rasta_converter(self, params: Dict[str, Any], output_name: str) -> bool:
		# Always write outputs under results_dir and enforce .png suffix in base
		out_base = self.results_dir / f"{output_name}.png"

		cmd = [str(self.rasta_exe), str(self.input_file), "/max_evals=10000000", "/quiet"]

		# Provide a default stable seed unless overridden
		if "seed" not in params:
			params = {**params, "seed": 123456}

		# Add CLI params
		for key, value in params.items():
			if isinstance(value, bool):
				if value:
					cmd.append(f"/{key}")
			else:
				cmd.append(f"/{key}={value}")

		# Output base
		cmd.append(f"/o={str(out_base)}")
		cmd.append("/threads=8")

		logger.info(f"Running: {' '.join(cmd)}")
		try:
			start_time = time.time()
			result = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
			elapsed = time.time() - start_time
			if result.returncode == 0:
				logger.info(f"Completed {output_name} in {elapsed:.1f}s")
				return True
			logger.error(f"Run failed {output_name}: {result.stderr.strip()}")
			return False
		except subprocess.TimeoutExpired:
			logger.error(f"Timeout: {output_name}")
			return False
		except Exception as e:
			logger.error(f"Error running {output_name}: {e}")
			return False

	def get_csv_data(self, output_name: str, is_dual: bool = False) -> pd.DataFrame:
		# Match your filenames: output.png.csv or output.png-dual.csv
		csv_file = self.results_dir / (f"{output_name}.png-dual.csv" if is_dual else f"{output_name}.png.csv")
		if not csv_file.exists():
			logger.warning(f"CSV not found: {csv_file}")
			return pd.DataFrame()

		try:
			df = pd.read_csv(csv_file)
			if not {"Iterations", "Seconds", "Score"}.issubset(df.columns):
				logger.error(f"CSV columns missing in {csv_file}")
				return pd.DataFrame()
			df["Millions"] = df["Iterations"] / 1_000_000.0
			return df
		except Exception as e:
			logger.error(f"CSV read error {csv_file}: {e}")
			return pd.DataFrame()

	def run_experiment_set(self, experiment_name: str, parameter_sets: List[Dict[str, Any]]) -> Dict[str, pd.DataFrame]:
		logger.info(f"Starting experiment set: {experiment_name}")
		results: Dict[str, pd.DataFrame] = {}

		for i, params in enumerate(parameter_sets):
			output_name = f"{experiment_name}_run_{i+1}"

			# Detect dual mode
			is_dual = bool(params.get("dual", False))

			ok = self.run_rasta_converter(params, output_name)
			if not ok:
				continue

			csv_data = self.get_csv_data(output_name, is_dual=is_dual)
			if csv_data.empty:
				continue

			label = self.create_parameter_label(params)
			# Ensure uniqueness in case of collisions
			if label in results:
				label = f"{label} ({i+1})"
			results[label] = csv_data

		return results

	def create_parameter_label(self, params: Dict[str, Any]) -> str:
		parts = []
		if "optimizer" in params:
			parts.append(params["optimizer"].upper())
		if params.get("dual", False):
			parts.append("DUAL")
			if "dual_strategy" in params:
				parts.append(params["dual_strategy"])
		if "mutation_strategy" in params:
			parts.append(params["mutation_strategy"])
		if "init" in params:
			parts.append(params["init"])
		if "s" in params:
			parts.append(f"s={params['s']}")
		if "threads" in params:
			parts.append(f"threads={params['threads']}")
		return " ".join(parts)

	def _pow2_ticks_millions(self, min_m=0.001, max_m=10.0):
		vals = []
		v = min_m
		while v <= max_m * 1.000001:  # tolerance
			vals.append(v)
			v *= 2.0
		return vals

	def create_comparison_graph(
		self,
		experiment_name: str,
		results: Dict[str, pd.DataFrame],
		x_column: str,
		x_label: str,
		log_x: bool
	) -> None:
		plt.figure(figsize=(12, 8))

		# Exactly 8 visually distinct colors
		colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728',
		          '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']

		for i, (label, data) in enumerate(results.items()):
			if data.empty or x_column not in data.columns:
				continue
			color = colors[i % len(colors)]
			plt.plot(
				data[x_column],
				data["Score"],
				label=label,
				color=color,
				linewidth=2,
				marker='o',
				markersize=3,
				alpha=0.95
			)

		# Axes formatting
		if log_x:
			plt.xscale('log')
			plt.xlim(0.001, 10.0)  # 1K to 10M evaluations
			# Power-of-two ticks and guiding lines
			major_x = self._pow2_ticks_millions(0.001, 10.0)
			plt.xticks(major_x, [f"{x:.3f}".rstrip('0').rstrip('.') for x in major_x])
			for x in major_x:
				plt.axvline(x=x, color='gray', linestyle=':', alpha=0.35)
		else:
			# Time axis: linear, auto range; add vertical guiding lines at nice intervals
			all_max = 0.0
			for df in results.values():
				if not df.empty:
					all_max = max(all_max, float(df["Seconds"].max()))
			if all_max <= 0:
				all_max = 1.0
			step = max(1.0, round(all_max / 8))  # ~8 guide lines
			xs = np.arange(0, all_max + step, step)
			plt.xticks(xs)
			for x in xs:
				plt.axvline(x=x, color='gray', linestyle=':', alpha=0.25)

		# Dynamic horizontal guide lines from y ticks
		ax = plt.gca()
		ax.yaxis.set_major_locator(ticker.MaxNLocator(nbins=8, prune=None))
		plt.grid(True, which='major', alpha=0.35)
		plt.grid(True, which='minor', alpha=0.15)
		for y in ax.get_yticks():
			plt.axhline(y=y, color='gray', linestyle=':', alpha=0.25, linewidth=0.8, zorder=0)

		plt.xlabel(x_label, fontsize=12)
		plt.ylabel('Normalized Distance', fontsize=12)
		plt.title(f'{experiment_name} - Parameter Comparison', fontsize=14, fontweight='bold')

		plt.legend(bbox_to_anchor=(1.02, 1), loc='upper left', fontsize=9, frameon=False)
		plt.tight_layout()

		out_png = self.results_dir / f"{experiment_name}_comparison.png"
		plt.savefig(out_png, dpi=300, bbox_inches='tight')
		logger.info(f"Saved graph: {out_png}")
		plt.close()

def main():
	runner = RastaExperimentRunner()

	experiments: Dict[str, List[Dict[str, Any]]] = {
		"Set1_Optimizer_Comparison": [
			{"optimizer": "dlas", "s": 3},
			{"optimizer": "dlas", "s": 5},
			{"optimizer": "dlas", "s": 10},
			{"optimizer": "dlas", "s": 100},
			{"optimizer": "lahc", "s": 3},
			{"optimizer": "lahc", "s": 10},
			{"optimizer": "lahc", "s": 100},
			{"optimizer": "lahc", "s": 1000},
		],
		"Set2_Mutation_Strategy": [
			{"optimizer": "lahc", "mutation_strategy": "global", "s": 3},
			{"optimizer": "lahc", "mutation_strategy": "global", "s": 10},
			{"optimizer": "lahc", "mutation_strategy": "global", "s": 100},
			{"optimizer": "lahc", "mutation_strategy": "global", "s": 1000},
			{"optimizer": "lahc", "mutation_strategy": "regional", "s": 3},
			{"optimizer": "lahc", "mutation_strategy": "regional", "s": 10},
			{"optimizer": "lahc", "mutation_strategy": "regional", "s": 100},
			{"optimizer": "lahc", "mutation_strategy": "regional", "s": 1000},
		],
		"Set3_Initialization_Comparison": [
			{"optimizer": "lahc", "init": "random", "s": 5},
			{"optimizer": "lahc", "init": "random", "s": 100},
			{"optimizer": "lahc", "init": "empty", "s": 5},
			{"optimizer": "lahc", "init": "empty", "s": 100},
			{"optimizer": "lahc", "init": "less", "s": 5},
			{"optimizer": "lahc", "init": "less", "s": 100},
			{"optimizer": "lahc", "init": "smart", "s": 5},
			{"optimizer": "lahc", "init": "smart", "s": 100},
		],
		"Set4_Dual_Strategy_Comparison": [
			{"optimizer": "lahc", "dual": True, "dual_strategy": "alternate", "s": 3},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "alternate", "s": 10},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "alternate", "s": 100},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "alternate", "s": 1000},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "staged", "s": 3},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "staged", "s": 10},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "staged", "s": 100},
			{"optimizer": "lahc", "dual": True, "dual_strategy": "staged", "s": 1000},
		],
		"Set5_Thread_Count_Comparison": [
			{"optimizer": "lahc", "s": 100, "threads": 1},
			{"optimizer": "lahc", "s": 100, "threads": 2},
			{"optimizer": "lahc", "s": 100, "threads": 3},
			{"optimizer": "lahc", "s": 100, "threads": 4},
			{"optimizer": "lahc", "s": 100, "threads": 5},
			{"optimizer": "lahc", "s": 100, "threads": 6},
			{"optimizer": "lahc", "s": 100, "threads": 7},
			{"optimizer": "lahc", "s": 100, "threads": 8},
		],
	}

	for experiment_name, parameter_sets in experiments.items():
		logger.info(f"\n{'='*60}\nStarting Experiment: {experiment_name}\n{'='*60}")
		results = runner.run_experiment_set(experiment_name, parameter_sets)
		if not results:
			logger.error(f"No results for {experiment_name}")
			continue

		is_time = (experiment_name == "Set5_Thread_Count_Comparison")
		x_col = "Seconds" if is_time else "Millions"
		x_label = "Time (seconds)" if is_time else "Millions of evaluations (0.001 = 1K, 10.0 = 10M)"
		log_x = not is_time

		runner.create_comparison_graph(
			experiment_name=experiment_name,
			results=results,
			x_column=x_col,
			x_label=x_label,
			log_x=log_x
		)

		# Save a summary JSON
		summary_file = runner.results_dir / f"{experiment_name}_summary.json"
		with open(summary_file, 'w', encoding='utf-8') as f:
			json.dump({
				"experiment_name": experiment_name,
				"parameter_sets": parameter_sets,
				"results_count": len(results),
				"timestamp": time.strftime("%Y-%m-%d %H:%M:%S")
			}, f, indent=2)

	logger.info("All experiments completed.")

if __name__ == "__main__":
	main()