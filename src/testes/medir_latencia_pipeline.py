#!/usr/bin/env python3
import argparse
import json
import re
import sys
import time
from pathlib import Path

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover
    serial = None

STAGES = [
    "captura_us",
    "buffer_us",
    "extracao_us",
    "fila_us",
    "inferencia_us",
    "led_us",
    "total_us",
]


def parse_pipeline_line(line):
    text = (line or "").strip()
    if not text or "latencia|" not in text:
        return None

    match = re.search(r"latencia\|(?P<body>.*)", text)
    if not match:
        return None

    body = match.group("body")
    values = {}
    for key in STAGES:
        m = re.search(rf"{re.escape(key)}=(?P<valor>\d+)", body)
        if m:
            values[key] = int(m.group("valor"))

    status_match = re.search(r"status=(?P<status>[A-Z_]+)", body)
    if status_match:
        values["status"] = status_match.group("status")

    prob_match = re.search(r"prob_latido=(?P<prob>\d+(?:\.\d+)?)", body)
    if prob_match:
        values["prob_latido"] = float(prob_match.group("prob"))

    if not values:
        return None

    return values


def summarize_metrics(samples):
    if not samples:
        return {
            "amostras": 0,
            "status_counts": {},
        }

    summary = {"amostras": len(samples)}
    for key in STAGES:
        valores = [sample[key] for sample in samples if key in sample]
        if not valores:
            summary[key] = {"media_us": 0.0, "min_us": 0.0, "max_us": 0.0, "amostras": 0}
            continue
        summary[key] = {
            "media_us": round(sum(valores) / len(valores), 3),
            "min_us": min(valores),
            "max_us": max(valores),
            "amostras": len(valores),
        }

    status_counts = {}
    for sample in samples:
        status = sample.get("status", "UNKNOWN")
        status_counts[status] = status_counts.get(status, 0) + 1
    summary["status_counts"] = status_counts
    return summary


def read_log_file(path):
    samples = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parsed = parse_pipeline_line(line)
            if parsed is not None:
                samples.append(parsed)
    return samples


def read_serial_port(port, baudrate, duration_seconds):
    if serial is None:
        raise RuntimeError("Biblioteca pyserial não está instalada. Use --log ou instale 'pyserial'.")

    samples = []
    deadline = time.monotonic() + duration_seconds
    with serial.Serial(port, baudrate, timeout=1) as ser:
        while time.monotonic() < deadline:
            try:
                line = ser.readline()
            except serial.SerialException:
                break
            if not line:
                continue
            parsed = parse_pipeline_line(line.decode("utf-8", errors="replace"))
            if parsed is not None:
                samples.append(parsed)
    return samples


def print_summary(summary):
    print("Resumo da latência por etapa (µs):")
    for key in STAGES:
        entry = summary.get(key, {"media_us": 0.0, "min_us": 0.0, "max_us": 0.0, "amostras": 0})
        print(f"- {key}: média={entry['media_us']:.3f} | min={entry['min_us']} | max={entry['max_us']} | amostras={entry['amostras']}")

    if summary.get("status_counts"):
        print("Status:")
        for status, count in sorted(summary["status_counts"].items()):
            print(f"- {status}: {count}")


def main():
    parser = argparse.ArgumentParser(description="Mede e resume a latência por etapa do pipeline embarcado.")
    parser.add_argument("--log", type=str, help="Caminho para um arquivo de log serial contendo linhas 'latencia|...'.")
    parser.add_argument("--serial-port", type=str, help="Porta serial do ESP32, por exemplo /dev/ttyUSB0")
    parser.add_argument("--baudrate", type=int, default=115200, help="Taxa de baud do monitor serial.")
    parser.add_argument("--duration-seconds", type=float, default=30.0, help="Tempo de leitura em segundos para serial em tempo real.")
    parser.add_argument("--output-json", type=str, help="Se informado, salva o resumo em um arquivo JSON.")
    args = parser.parse_args()

    if args.log:
        samples = read_log_file(args.log)
    elif args.serial_port:
        try:
            samples = read_serial_port(args.serial_port, args.baudrate, args.duration_seconds)
        except RuntimeError as exc:
            print(f"Erro: {exc}", file=sys.stderr)
            return 1
    else:
        parser.error("Informe --log ou --serial-port.")

    if not samples:
        print("Nenhuma linha de latencia|... foi encontrada no log/serial informado.", file=sys.stderr)
        return 2

    summary = summarize_metrics(samples)
    print_summary(summary)

    if args.output_json:
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"\nResumo salvo em: {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
