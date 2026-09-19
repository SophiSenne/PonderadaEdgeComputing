import argparse
import json
import math
import os
import re
import sys
import time
import wave
from pathlib import Path

import numpy as np

try:
    import onnxruntime as ort
except Exception:  # pragma: no cover
    ort = None

try:
    import soundfile as sf
except Exception:  # pragma: no cover
    sf = None

try:
    import librosa
except Exception:  # pragma: no cover
    librosa = None

SAMPLE_RATE = 16000
WINDOW_SIZE = 512
STEP_SIZE = 160
MFCC_COUNT = 13
MEL_FILTERS = 26
EPSILON = 1.0e-12
DEFAULT_THRESHOLD = 0.5


def hz_para_mel(frequencia):
    return 1127.0 * np.log(1.0 + frequencia / 700.0)


def mel_para_hz(mel):
    return 700.0 * (np.exp(mel / 1127.0) - 1.0)


def extrair_features_janela(amostras, taxa_amostragem=SAMPLE_RATE):
    amostras = amostras.astype(np.float64)
    media = amostras.mean()
    centralizado = amostras - media
    energia = np.sum(centralizado ** 2)

    rms = np.sqrt(energia / WINDOW_SIZE) / 32768.0
    rms_db = 20.0 * np.log10(rms + EPSILON)

    pre_enfase = np.empty(WINDOW_SIZE)
    pre_enfase[0] = centralizado[0]
    pre_enfase[1:] = centralizado[1:] - 0.97 * centralizado[:-1]

    janela_hamming = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(WINDOW_SIZE) / (WINDOW_SIZE - 1))
    sinal_janelado = pre_enfase * janela_hamming

    espectro = np.fft.fft(sinal_janelado, n=WINDOW_SIZE)
    metade = WINDOW_SIZE // 2
    magnitude = np.abs(espectro[:metade])
    magnitude[0] = 0.0
    potencia = magnitude ** 2

    frequencias = np.arange(metade) * taxa_amostragem / WINDOW_SIZE
    soma_magnitude = magnitude.sum()
    soma_frequencia = np.sum(frequencias * magnitude)
    centroide_espectral = soma_frequencia / soma_magnitude if soma_magnitude > EPSILON else 0.0

    mel_minimo = hz_para_mel(300.0)
    mel_maximo = hz_para_mel(taxa_amostragem / 2.0)
    pontos_mel = mel_minimo + (mel_maximo - mel_minimo) * np.arange(MEL_FILTERS + 2) / (MEL_FILTERS + 1)
    pontos_hz = mel_para_hz(pontos_mel)
    pontos_bin = np.floor((WINDOW_SIZE + 1) * pontos_hz / taxa_amostragem).astype(int)
    pontos_bin = np.clip(pontos_bin, 0, metade - 1)

    energia_mel = np.zeros(MEL_FILTERS)
    for filtro in range(MEL_FILTERS):
        inicio, meio, fim = pontos_bin[filtro], pontos_bin[filtro + 1], pontos_bin[filtro + 2]
        for b in range(inicio, meio):
            if meio > inicio:
                energia_mel[filtro] += potencia[b] * (b - inicio) / (meio - inicio)
        for b in range(meio, min(fim + 1, metade)):
            if fim > meio:
                energia_mel[filtro] += potencia[b] * (fim - b) / (fim - meio)

    log_energia_mel = np.log(energia_mel + EPSILON)
    mfcc = np.zeros(MFCC_COUNT)
    for coeficiente in range(MFCC_COUNT):
        indices_filtro = np.arange(MEL_FILTERS)
        mfcc[coeficiente] = np.sum(
            log_energia_mel * np.cos(np.pi * coeficiente * (indices_filtro + 0.5) / MEL_FILTERS)
        )

    return {
        "rms": float(rms),
        "rms_db": float(rms_db),
        "centroide_espectral": float(centroide_espectral),
        "mfcc": mfcc.astype(np.float32),
    }


def vetor_features(features):
    return np.concatenate(
        [[features["rms"], features["rms_db"], features["centroide_espectral"]], features["mfcc"]]
    ).astype(np.float32)


def carregar_audio_arquivo(caminho_audio, target_sr=SAMPLE_RATE):
    caminho = Path(caminho_audio)
    if not caminho.exists():
        raise FileNotFoundError(f"Arquivo de áudio não encontrado: {caminho}")

    extensao = caminho.suffix.lower()

    if extensao == ".wav":
        with wave.open(str(caminho), "rb") as wav:
            n_channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            frames = wav.readframes(wav.getnframes())

        if sample_width == 1:
            dtype = np.uint8
            offset = 128
            audio = np.frombuffer(frames, dtype=dtype).astype(np.float32)
            audio = (audio - offset) / 128.0
        elif sample_width == 2:
            audio = np.frombuffer(frames, dtype='<i2').astype(np.float32)
            audio = audio / 32768.0
        elif sample_width == 4:
            audio = np.frombuffer(frames, dtype='<i4').astype(np.float32)
            audio = audio / 2147483648.0
        else:
            raise ValueError(f"Formato de amostragem não suportado: {sample_width} bytes")

        if n_channels > 1:
            audio = audio.reshape(-1, n_channels)
            audio = audio.mean(axis=1)
        if sample_rate != target_sr:
            if librosa is not None:
                audio = librosa.resample(audio, orig_sr=sample_rate, target_sr=target_sr)
            else:
                raise RuntimeError("Resample exigido, mas 'librosa' não está instalado.")
        return audio.astype(np.float32)

    if sf is not None:
        audio, sample_rate = sf.read(str(caminho), dtype="float32", always_2d=False)
        if audio.ndim > 1:
            audio = audio.mean(axis=1)
        if sample_rate != target_sr:
            if librosa is not None:
                audio = librosa.resample(audio, orig_sr=sample_rate, target_sr=target_sr)
            else:
                raise RuntimeError("Resample exigido, mas 'librosa' não está instalado.")
        return np.asarray(audio, dtype=np.float32)

    if librosa is not None:
        audio, sample_rate = librosa.load(str(caminho), sr=target_sr, mono=True)
        return np.asarray(audio, dtype=np.float32)

    raise RuntimeError("Nenhuma biblioteca de áudio disponível. Instale 'soundfile' ou 'librosa'.")


def extrair_janelas_audio(caminho_audio, taxa_amostragem=SAMPLE_RATE):
    audio = carregar_audio_arquivo(caminho_audio, target_sr=taxa_amostragem)
    amostras_int16 = np.clip(audio * 32768.0, -32768, 32767).astype(np.int16)

    vetores = []
    inicio = 0
    while inicio + WINDOW_SIZE <= len(amostras_int16):
        janela = amostras_int16[inicio:inicio + WINDOW_SIZE]
        features = extrair_features_janela(janela, taxa_amostragem)
        vetores.append(vetor_features(features))
        inicio += STEP_SIZE

    return np.asarray(vetores, dtype=np.float32)


def carregar_valores_desde_arquivo(caminho_arquivo):
    caminho = Path(caminho_arquivo)
    if not caminho.exists():
        return None

    if caminho.suffix.lower() == ".json":
        with open(caminho, "r", encoding="utf-8") as f:
            dados = json.load(f)
        if isinstance(dados, dict):
            if "mean" in dados and "std" in dados:
                return np.asarray(dados["mean"], dtype=np.float32), np.asarray(dados["std"], dtype=np.float32)
            if "media" in dados and "desvio" in dados:
                return np.asarray(dados["media"], dtype=np.float32), np.asarray(dados["desvio"], dtype=np.float32)
        return None

    if caminho.suffix.lower() in {".npy", ".npz"}:
        dados = np.load(caminho)
        if isinstance(dados, np.lib.npyio.NpzFile):
            if "mean" in dados and "std" in dados:
                return dados["mean"].astype(np.float32), dados["std"].astype(np.float32)
            if "media" in dados and "desvio" in dados:
                return dados["media"].astype(np.float32), dados["desvio"].astype(np.float32)
        else:
            if "mean" in dados and "std" in dados:
                return np.asarray(dados["mean"], dtype=np.float32), np.asarray(dados["std"], dtype=np.float32)
        return None

    if caminho.suffix.lower() in {".csv", ".txt"}:
        valores = []
        with open(caminho, "r", encoding="utf-8") as f:
            for linha in f:
                linha = linha.strip()
                if not linha:
                    continue
                valores.extend(float(v) for v in re.split(r"[\s,;]+", linha) if v)
        return np.asarray(valores, dtype=np.float32), np.ones_like(np.asarray(valores, dtype=np.float32))

    return None


def carregar_media_desvio(base_dir: Path):
    candidatos = [
        base_dir / "bark_classifier_mean_std.npz",
        base_dir / "bark_classifier_mean_std.json",
        base_dir / "mean_std.json",
        base_dir / "media_desvio.json",
        base_dir.parent / "bark_classifier_mean_std.npz",
        base_dir.parent / "bark_classifier_mean_std.json",
        Path("/content") / "bark_classifier_mean_std.npz",
        Path("/content") / "bark_classifier_mean_std.json",
    ]

    for caminho in candidatos:
        dados = carregar_valores_desde_arquivo(caminho)
        if dados is not None:
            return dados

    header_path = base_dir / "../sketch/classifier_head.hpp"
    header_final = header_path.resolve()
    if header_final.exists():
        conteudo = header_final.read_text(encoding="utf-8")
        padrao_media = re.search(r"K_CLASSIFIER_FEATURE_MEAN\[CLASSIFIER_INPUT_DIM\]\s*=\s*\{\s*(.*?)\s*\};", conteudo, re.S)
        padrao_desvio = re.search(r"K_CLASSIFIER_FEATURE_STD\[CLASSIFIER_INPUT_DIM\]\s*=\s*\{\s*(.*?)\s*\};", conteudo, re.S)
        if padrao_media and padrao_desvio:
            media = np.array([float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", padrao_media.group(1))], dtype=np.float32)
            desvio = np.array([float(x) for x in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", padrao_desvio.group(1))], dtype=np.float32)
            if media.size == desvio.size:
                return media, desvio

    return None


def normalizar_features(vetores, media_desvio=None):
    if media_desvio is None:
        return vetores
    media, desvio = media_desvio
    media = np.asarray(media, dtype=np.float32)
    desvio = np.asarray(desvio, dtype=np.float32)
    desvio[desvio < 1e-6] = 1e-6
    return (vetores - media) / desvio


def softmax(logits):
    logits = np.asarray(logits, dtype=np.float32)
    logits = logits - np.max(logits, axis=-1, keepdims=True)
    exp = np.exp(logits)
    return exp / np.sum(exp, axis=-1, keepdims=True)


def encontrar_modelo(modelo_path):
    caminhos = []
    if modelo_path:
        caminhos.append(Path(modelo_path))
    caminhos.extend([
        Path("bark_classifier.onnx"),
        Path("src/modelo/bark_classifier.onnx"),
        Path("/content/bark_classifier.onnx"),
        Path("src/modelo/modelo.onnx"),
    ])

    for caminho in caminhos:
        if caminho and caminho.exists():
            return str(caminho)

    return None


def classificar_arquivo_audio(caminho_audio, model_path=None, threshold=DEFAULT_THRESHOLD, media_desvio=None):
    if ort is None:
        raise RuntimeError("onnxruntime não está instalado. Execute: pip install onnxruntime")

    inicio_total = time.perf_counter()

    modelo = encontrar_modelo(model_path)
    if modelo is None:
        raise FileNotFoundError(
            "Modelo ONNX não encontrado. Passe --model ou coloque 'bark_classifier.onnx' em algum dos caminhos esperados."
        )

    sessao = ort.InferenceSession(modelo, providers=["CPUExecutionProvider"])

    entrada = sessao.get_inputs()[0]
    nome_entrada = entrada.name
    dim_entrada = tuple(entrada.shape)
    if dim_entrada[0] is None:
        pass

    tempo_extracao_inicio = time.perf_counter()
    feature_vectors = extrair_janelas_audio(caminho_audio)
    tempo_extracao_fim = time.perf_counter()
    if feature_vectors.size == 0:
        raise ValueError("Nenhuma janela foi extraída do áudio informado.")

    if media_desvio is None:
        media_desvio = carregar_media_desvio(Path(modelo).parent)
    tempo_normalizacao_inicio = time.perf_counter()
    if media_desvio is not None:
        feature_vectors = normalizar_features(feature_vectors, media_desvio)
    tempo_normalizacao_fim = time.perf_counter()

    entrada_modelo = np.asarray(feature_vectors, dtype=np.float32)
    if len(entrada_modelo.shape) == 1:
        entrada_modelo = entrada_modelo.reshape(1, -1)

    tempo_infer_inicio = time.perf_counter()
    saida = sessao.run(None, {nome_entrada: entrada_modelo})[0]
    tempo_infer_fim = time.perf_counter()
    if saida.ndim == 1:
        saida = saida.reshape(1, -1)

    if saida.shape[-1] == 2:
        probabilidades = softmax(saida)
        scores = probabilidades[:, 1]
    else:
        scores = np.asarray(saida).reshape(-1)

    media_score = float(np.mean(scores))
    pred = 1 if media_score >= threshold else 0

    tempo_total = time.perf_counter() - inicio_total
    tempo_extracao = tempo_extracao_fim - tempo_extracao_inicio
    tempo_normalizacao = tempo_normalizacao_fim - tempo_normalizacao_inicio
    tempo_inferencia = tempo_infer_fim - tempo_infer_inicio
    throughput = len(scores) / tempo_total if tempo_total > 0 else 0.0
    latencia_media_ms = (tempo_total / len(scores)) * 1000.0 if len(scores) else 0.0

    return {
        "modelo": modelo,
        "janelas": int(len(scores)),
        "probabilidade_latido": media_score,
        "classificacao": "LATIDO" if pred else "SEM_LATIDO",
        "scores_por_janela": scores.tolist(),
        "metrics": {
            "tempo_total_ms": round(tempo_total * 1000.0, 3),
            "tempo_extracao_features_ms": round(tempo_extracao * 1000.0, 3),
            "tempo_normalizacao_ms": round(tempo_normalizacao * 1000.0, 3),
            "tempo_inferencia_onnx_ms": round(tempo_inferencia * 1000.0, 3),
            "latencia_media_ms_por_janela": round(latencia_media_ms, 3),
            "throughput_janelas_por_segundo": round(throughput, 3),
        },
    }


def main():
    parser = argparse.ArgumentParser(description="Classifica um áudio usando o modelo ONNX real de detecção de latido.")
    parser.add_argument("audio", help="Caminho do arquivo de áudio .wav, .mp3, .ogg, etc.")
    parser.add_argument("--model", default="bark_classifier.onnx", help="Caminho do arquivo ONNX. Padrão: bark_classifier.onnx")
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD, help="Limite para decidir latido (padrão 0.5)")
    parser.add_argument("--mean-std", type=str, default=None, help="Arquivo opcional com média e desvio padrão para normalização (JSON/NPZ/CSV)")
    args = parser.parse_args()

    base_dir = Path(__file__).resolve().parent
    media_desvio = None
    if args.mean_std:
        media_desvio = carregar_valores_desde_arquivo(args.mean_std)
    if media_desvio is None:
        media_desvio = carregar_media_desvio(base_dir)

    resultado = classificar_arquivo_audio(
        args.audio,
        model_path=args.model,
        threshold=args.threshold,
        media_desvio=media_desvio,
    )

    print("=== Detecção com ONNX ===")
    print(f"Arquivo: {args.audio}")
    print(f"Modelo: {resultado['modelo']}")
    print(f"Janelas processadas: {resultado['janelas']}")
    print(f"Probabilidade média de latido: {resultado['probabilidade_latido']:.4f}")
    print(f"Classificação: {resultado['classificacao']}")
    print("\n=== Métricas de performance ===")
    for chave, valor in resultado["metrics"].items():
        print(f"{chave}: {valor}")


if __name__ == "__main__":
    main()
