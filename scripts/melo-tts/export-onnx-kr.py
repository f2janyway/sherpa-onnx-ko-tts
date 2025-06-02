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
        # self.lang_id = language_id_map[model.language] # forward에서 동적으로 생성
        self.bert_dim = 1024 # MeloTTS 한국어 모델이 사용하는 BERT 차원 (확인 필요)
        self.ja_bert_dim = 768 # JaBERT 사용 여부 및 차원 (확인 필요, 보통 한국어는 BERT만 사용)

    def forward(
        self,
        x,
        x_lengths,
        tones,
        sid,
        noise_scale,
        length_scale,
        noise_scale_w,
        max_len=None,
    ):
        """
        Args:
          x: A 1-D array of dtype np.int64. Its shape is (token_numbers,)
          tones: A 1-D array of dtype np.int64. Its shape is (token_numbers,)
          sid: an integer
        """
        # MeloTTS의 파이썬 추론 시 BERT/JaBERT가 동적으로 계산되지만,
        # ONNX export 시에는 더미 텐서가 필요합니다.
        # 실제 추론 시에는 sherpa-onnx의 프론트엔드가 이 텐서를 생성해야 합니다.
        bert = torch.zeros(x.shape[0], self.bert_dim, x.shape[1], dtype=torch.float32)
        ja_bert = torch.zeros(x.shape[0], self.ja_bert_dim, x.shape[1], dtype=torch.float32)

        # lang_id도 마찬가지로 더미로 넣지만, 실제 추론 시 프론트엔드에서 생성됩니다.
        # MeloTTS의 `get_text_for_tts_infer` 함수를 보면 텍스트에 따라 동적으로 생성됩니다.
        # 일반적으로 blank 토큰은 0, 실제 음소는 해당 언어의 ID를 가집니다.
        # 여기서는 모델의 언어 ID를 사용하도록 둡니다.
        lang_id = torch.zeros_like(x)
        # lang_id[:, 1::2] = self.lang_id # 원래 영어 코드. 한국어는 blank/phoneme 구분해서 ID 부여
        # MeloTTS 한국어 모델의 정확한 lang_id 부여 로직 확인 필요.
        # 보통 blank=0, Korean=4 (혹은 모델의 config.json에 정의된 ID)
        # 여기서는 ONNX export를 위한 더미이므로 간단히 처리.
        # 실제 C++ 프론트엔드에서 정확한 lang_id를 구성해야 합니다.
        language_id_for_korean = 4 # MeloTTS 한국어의 언어 ID (확인 필요)
        lang_id[:, 1::2] = language_id_for_korean # 예시: 홀수 인덱스에 언어 ID 부여 (blank 처리 시)


        return self.model.model.infer(
            x=x,
            x_lengths=x_lengths,
            sid=sid,
            tone=tones,
            language=lang_id,
            bert=bert,
            ja_bert=ja_bert,
            noise_scale=noise_scale,
            noise_scale_w=noise_scale_w,
            length_scale=length_scale,
        )[0]


def main():
    # generate_lexicon() # 한국어는 lexicon.txt를 사용하지 않을 가능성이 높음

    language = "KR"  # 언어를 "KO"로 변경
    # 모델 로드 (MeloTTS 라이브러리가 KO 모델을 찾아 로드할 것입니다)
    # 한국어 모델의 체크포인트가 `ko_KO-kss_low.pth`이라면 `language="KO"`로 가능
    # (주의: 실제로는 `ko_KO-kss_low.pth` 경로를 지정해야 할 수도 있습니다.)
    # 예: model = TTS(language="KO", checkpoint_path="path/to/ko_KO-kss_low.pth", device="cpu")
    model = TTS(language=language, device="cpu")

    # symbols 리스트를 기반으로 tokens.txt 생성
    generate_tokens(model.hps["symbols"])

    torch_model = ModelWrapper(model)

    opset_version = 13
    # ONNX 모델의 입력 셰이프를 정의하기 위한 더미 텐서.
    # 실제 입력은 sherpa-onnx의 프론트엔드에서 생성됩니다.
    x = torch.randint(low=0, high=10, size=(60,), dtype=torch.int64) # 음소 ID
    x_lengths = torch.tensor([x.size(0)], dtype=torch.int64) # 음소 시퀀스 길이
    sid = torch.tensor([1], dtype=torch.int64) # 스피커 ID (MeloTTS 한국어는 단일 스피커일 경우 0)
    tones = torch.zeros_like(x) # 톤 ID

    noise_scale = torch.tensor([1.0], dtype=torch.float32)
    length_scale = torch.tensor([1.0], dtype=torch.float32)
    noise_scale_w = torch.tensor([1.0], dtype=torch.float32)

    x = x.unsqueeze(0)
    tones = tones.unsqueeze(0)

    filename = "model_korean.onnx" # 출력 ONNX 파일 이름 변경

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
        ),
        filename,
        opset_version=opset_version,
        input_names=[
            "x", # 음소 ID
            "x_lengths", # 음소 시퀀스 길이
            "tones", # 톤 ID
            "sid", # 스피커 ID
            "noise_scale",
            "length_scale",
            "noise_scale_w",
        ],
        output_names=["y"],
        dynamic_axes={
            "x": {0: "N", 1: "L"}, # N: 배치 크기, L: 음소 시퀀스 길이
            "x_lengths": {0: "N"},
            "tones": {0: "N", 1: "L"},
            "y": {0: "N", 1: "S", 2: "T"}, # N: 배치 크기, S: 스피커 수(보통 1), T: 오디오 샘플 길이
        },
    )

    # **ONNX 모델에 포함할 메타데이터 (매우 중요)**
    # sherpa-onnx의 MeloTtsLexicon이 이 정보를 활용합니다.
    meta_data = {
        "model_type": "melo-vits",
        "comment": "melo_korean",
        "version": 2,
        "language": "Korean", # 언어를 "Korean"으로 변경!
        "add_blank": int(model.hps.data.add_blank), # MeloTTS config에서 가져옴
        "n_speakers": len(model.hps.data.spk2id), # MeloTTS 한국어 모델 스피커 수 (보통 1)
        "jieba": 1, # **핵심:** 이 값을 1로 설정하여 MeloTtsLexicon이 활성화되도록 시도
                     # MeloTtsLexicon의 조건이 `meta_data.jieba && !config_.model.vits.dict_dir.empty() && meta_data.is_melo_tts` 이므로
                     # `jieba`를 1로 설정하면 한국어 모델에서도 이 분기로 진입을 시도할 것입니다.
        "sample_rate": model.hps.data.sampling_rate,
        "bert_dim": 1024, # MeloTTS 한국어 모델이 사용하는 BERT 차원 (확인 필요)
        "ja_bert_dim": 768, # JaBERT 사용 여부 및 차원 (사용하지 않으면 0)
        "speaker_id": 0, # 기본 스피커 ID
        "lang_id": language_id_map[language], # "KO"에 해당하는 언어 ID
        "tone_start": language_tone_start_map[language], # "KO"에 해당하는 톤 시작 ID
        "url": "https://github.com/myshell-ai/MeloTTS",
        "license": "MIT license",
        "description": "MeloTTS Korean is a high-quality multi-lingual text-to-speech library by MyShell.ai",
        "is_melo_tts": 1, # 이 플래그도 필수
    }
    add_meta_data(filename, meta_data)


if __name__ == "__main__":
    main()