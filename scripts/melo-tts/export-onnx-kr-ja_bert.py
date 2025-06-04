#!/usr/bin/env python3
# This script exports the Korean TTS model from MeloTTS to ONNX format.

from typing import Any, Dict

import onnx
import torch
from melo.api import TTS
from melo.text import language_id_map, language_tone_start_map
# from melo.text.chinese import pinyin_to_symbol_map # 한국어에는 필요 없음
# from melo.text.english import eng_dict, refine_syllables # 한국어에는 필요 없음

# 한국어 텍스트 전처리 및 음소화를 위한 모듈 임포트 (MeloTTS 내부에서 사용)
# MeloTTS의 한국어 전처리는 내부적으로 mecab-ko 및 자체 규칙을 사용합니다.
# 직접적으로 import해서 사용하는 것은 아닐 수 있으므로, MeloTTS.api.TTS가 내부적으로 처리하도록 둡니다.


def generate_tokens(symbol_list):
    """
    Generates tokens.txt from the model's symbol list.
    """
    with open("tokens.txt", "w", encoding="utf-8") as f:
        for i, s in enumerate(symbol_list):
            f.write(f"{s} {i}\n")


# 한국어는 lexicon.txt를 사용하지 않을 가능성이 높습니다.
# MeloTTS 한국어 모델은 런타임에 MeCab-ko와 규칙 기반으로 음소화를 수행합니다.
# 하지만 sherpa-onnx의 MeloTtsLexicon이 lexicon.txt를 요구할 수 있으므로,
# 필요하다면 dummy 파일을 생성하거나 해당 필드를 비워두도록 합니다.
# 여기서는 일단 주석 처리하여 lexicon.txt 생성을 건너뜁니다.
# def generate_lexicon():
#     # 한국어에 특화된 lexicon 생성 로직 (필요하다면 구현)
#     pass


def add_meta_data(filename: str, meta_data: Dict[str, Any]):
    """Add meta data to an ONNX model. It is changed in-place.

    Args:
      filename:
        Filename of the ONNX model to be changed.
      meta_data:
        Key-value pairs.
    """
    model = onnx.load(filename)
    while len(model.metadata_props):
        model.metadata_props.pop()

    for key, value in meta_data.items():
        meta = model.metadata_props.add()
        meta.key = key
        meta.value = str(value)

    onnx.save(model, filename)


class ModelWrapper(torch.nn.Module):
    def __init__(self, model: "SynthesizerTrn"):
        super().__init__()
        self.model = model
        self.bert_dim = 1024
        self.ja_bert_dim = 768

    def forward(
        self,
        x,
        x_lengths,
        tones,
        sid,
        noise_scale,
        length_scale,
        noise_scale_w,
        ja_bert, # <--- 이 줄을 추가하여 ja_bert를 forward 메서드의 인자로 받습니다.
        max_len=None, # max_len은 ONNX 입력으로 필요하지 않으므로 그대로 두거나 제거해도 됩니다.
    ):
        """
        Args:
          x: A 1-D array of dtype np.int64. Its shape is (token_numbers,)
          tones: A 1-D array of dtype np.int64. Its shape is (token_numbers,)
          sid: an integer
          ja_bert: A tensor of dtype torch.float32. Its shape is (N, 768, L)
        """
        # BERT는 그대로 내부에서 생성할 수 있지만, ja_bert는 외부에서 받을 것이므로 제거합니다.
        bert = torch.zeros(x.shape[0], self.bert_dim, x.shape[1], dtype=torch.float32)

        lang_id = torch.zeros_like(x)
        language_id_for_korean = 4
        lang_id[:, 1::2] = language_id_for_korean

        return self.model.model.infer(
            x=x,
            x_lengths=x_lengths,
            sid=sid,
            tone=tones,
            language=lang_id,
            bert=bert,
            ja_bert=ja_bert, # 이제 외부에서 받은 ja_bert를 사용합니다.
            noise_scale=noise_scale,
            noise_scale_w=noise_scale_w,
            length_scale=length_scale,
        )[0]


def main():
    # generate_lexicon() # 한국어는 lexicon.txt를 사용하지 않을 가능성이 높음

    language = "KR"  # 언어를 "KO"로 변경
    # 모델 로드 (MeloTTS 라이브러리가 KO 모델을 찾아 로드할 것입니다)
    model = TTS(language=language, device="cpu")

    # symbols 리스트를 기반으로 tokens.txt 생성
    generate_tokens(model.hps["symbols"])

    torch_model = ModelWrapper(model)

    opset_version = 13
    # ONNX 모델의 입력 셰이프를 정의하기 위한 더미 텐서
    x = torch.randint(low=0, high=10, size=(60,), dtype=torch.int64)  # 음소 ID
    x_lengths = torch.tensor([x.size(0)], dtype=torch.int64)  # 음소 시퀀스 길이
    sid = torch.tensor([1], dtype=torch.int64)  # 스피커 ID
    tones = torch.zeros_like(x)  # 톤 ID
    noise_scale = torch.tensor([1.0], dtype=torch.float32)
    length_scale = torch.tensor([1.0], dtype=torch.float32)
    noise_scale_w = torch.tensor([1.0], dtype=torch.float32)
    # Dummy ja_bert tensor: shape (1, 768, L) where L=60 to match x
    ja_bert = torch.randn(1, 768, x.size(0), dtype=torch.float32)  # (N, 768, L)

    # Unsqueeze x and tones to add batch dimension
    x = x.unsqueeze(0)  # Shape: (1, 60) -> (N, L)
    tones = tones.unsqueeze(0)  # Shape: (1, 60) -> (N, L)

    filename = "model_korean_ja_bert.onnx"

    torch.onnx.export(
        torch_model,
        (
            x,
            x_lengths,
            tones,
            sid,
            noise_scale,
            length_scale,
            noise_scale_w,
            ja_bert,  # Add ja_bert to input tuple
        ),
        filename,
        opset_version=opset_version,
        input_names=[
            "x",  # 음소 ID
            "x_lengths",  # 음소 시퀀스 길이
            "tones",  # 톤 ID
            "sid",  # 스피커 ID
            "noise_scale",
            "length_scale",
            "noise_scale_w",
            "ja_bert",  # Included in input_names
        ],
        output_names=["y"],
        dynamic_axes={
            "x": {0: "N", 1: "L"},  # (N, L)
            "x_lengths": {0: "N"},  # (N,)
            "tones": {0: "N", 1: "L"},  # (N, L)
            "sid": {0: "N"},  # (N,)
            "y": {0: "N", 1: "S", 2: "T"},  # (N, S, T)
            "ja_bert": {0: "N", 2: "L"},  # (N, 768, L)
        },
    )

    # ONNX 모델에 포함할 메타데이터
    meta_data = {
        "model_type": "melo-vits",
        "comment": "melo_korean",
        "version": 2,
        "language": "Korean",
        "add_blank": int(model.hps.data.add_blank),
        "n_speakers": len(model.hps.data.spk2id),
        "jieba": 1,  # For MeloTtsLexicon
        "sample_rate": model.hps.data.sampling_rate,
        "bert_dim": 1024,
        "ja_bert_dim": 768,  # Matches ja_bert feature dimension
        "speaker_id": 0,
        "lang_id": language_id_map[language],
        "tone_start": language_tone_start_map[language],
        "url": "https://github.com/myshell-ai/MeloTTS",
        "license": "MIT license",
        "description": "MeloTTS Korean is a high-quality multi-lingual text-to-speech library by MyShell.ai",
        "is_melo_tts": 1,
    }
    add_meta_data(filename, meta_data)

if __name__ == "__main__":
    main()