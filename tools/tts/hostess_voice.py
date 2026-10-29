#!/usr/bin/env python3
"""Voix de l'hôtesse avec Qwen3-TTS (Apache 2.0), a lancer SUR la VM : ~/tts/venv/bin/python ~/tts/hostess_voice.py <commande>

    design              3 voix candidates (description -> voix) lisant la même phrase : ~/tts/out/design/cand_*.wav
    lines CAND          rend toutes les répliques de LINES avec la voix CAND (cand_a, cand_b ou cand_c) : ~/tts/out/lines/<id>.wav

Voix inventée par description (aucune voix réelle clonée) ; la voix choisie sert ensuite de référence (Voice Design then Clone,
README officiel) pour garder le même timbre d'une réplique à l'autre. ComfyUI occupe la carte : on la vide d'abord.
"""
import json
import sys
import urllib.request
from pathlib import Path

import soundfile as sf
import torch
from qwen_tts import Qwen3TTSModel

ROOT = Path.home() / "tts"
OUT = ROOT / "out"
DESIGN = ROOT / "models" / "Qwen3-TTS-12Hz-1.7B-VoiceDesign"
BASE = ROOT / "models" / "Qwen3-TTS-12Hz-1.7B-Base"

REF_TEXT = "ようこそ、ダンシング・エブリ・ナイトへ。今夜も一緒に、最高のステージを作ろうね。"
CANDIDATES = {
    "cand_a": "Young adult Japanese woman, about 20, bright and confident with a playful teasing edge, clear medium-high pitch, "
              "warm and expressive, anime heroine voice, energetic but graceful.",
    "cand_b": "Japanese woman in her early twenties, elegant and self-assured hostess, slightly lower and smoother voice, "
              "calm warm smile in the tone, a little mischievous, polished anime narrator style.",
    "cand_c": "Cheerful Japanese young woman, high and light voice with lively intonation, cute and friendly, "
              "enthusiastic idol-like energy, clear and bright, anime style.",
    # variantes de B (retenue), plus graves
    "cand_b2": "Japanese woman in her mid twenties, elegant and self-assured hostess, noticeably lower and smoother voice, "
               "warm alto register, calm smile in the tone, a little mischievous, polished anime narrator style.",
    "cand_b3": "Japanese woman around thirty, mature and charming hostess, low husky alto voice, deep and velvety, "
               "relaxed and confident, slightly teasing, anime older-sister style.",
}
# id -> (réplique japonaise, traduction pour mémoire, écran)
LINES = {
    "rank_s": ("やったね！ 完璧だよ、すごい！", "Yes! Perfect, amazing!", "résultats S"),
    "rank_a": ("いい調子！ ふふっ、さすがだね。", "Nice! Hehe, as expected.", "résultats A"),
    "rank_b": ("うん、その調子！ もう一回、いってみよう！", "That's the spirit! Let's go once more!", "résultats B"),
    "rank_cd": ("あらら……もう一回、やってみる？", "Oh my... want to try again?", "résultats C/D"),
    "loading": ("さあ、今夜のステージは、これ！", "Now, tonight's stage is this!", "chargement"),
    "pause": ("ちょっと休憩？ 待ってるね。", "A little break? I'll wait.", "pause"),
    "title": ("ダンシング・エブリ・ナイト！", "Dancing Every Night!", "titre"),
    "oh": ("えーっ！？", "Eeh?!", "onomatopée"),
    "hehe": ("えへへ。", "Ehehe.", "onomatopée"),
    "fufu": ("ふふっ。", "Fufu.", "onomatopée"),
    "yatta": ("やった！", "Yay!", "onomatopée"),
    "nice": ("ナイス！", "Nice!", "onomatopée"),
}

# version expressive : VoiceDesign relancé à chaque réplique avec la description de B3 + une consigne d'émotion
# (le clonage de la Base rend un ton plat ; ici le timbre varie un peu d'une prise à l'autre, on choisit à l'oreille)
EXPR_BASE = CANDIDATES["cand_b3"]
# id -> (textes alternatifs, consigne d'émotion)
EXPR = {
    "rank_s": (["やったね！ 完璧だよ、すごい！"], "Overjoyed and proud, bursting with excitement, big smile, voice rising, very expressive and lively."),
    "rank_a": (["いい調子！ ふふっ、さすがだね。"], "Pleased and playful, a warm teasing laugh in the voice, upbeat."),
    "rank_b": (["うん、その調子！ もう一回、いってみよう！"], "Gentle and encouraging, warm and supportive, motivating tone."),
    "rank_cd": (["あらら……もう一回、やってみる？"], "Soft, kind and slightly amused, a sympathetic sigh, comforting."),
    "loading": (["さあ、今夜のステージは、これ！"], "Showman-like announcement, confident and theatrical, building anticipation."),
    "pause": (["ちょっと休憩？ 待ってるね。"], "Relaxed, gentle and patient, soft calm voice."),
    "title": (["ダンシング・エブリ・ナイト！"], "Energetic announcer, dramatic and stylish, powerful delivery."),
    "oh": (["えーっ！？", "えーーっ！？ うそっ！"], "Genuinely surprised and startled, sudden high-pitched gasp, very exaggerated and expressive."),
    "hehe": (["えへへ。", "えへへ……っ"], "Shy, sheepish giggle, bashful and cute, breathy."),
    "fufu": (["ふふっ。", "ふふふっ……", "うふふ。"], "A sly, teasing, sultry little laugh through the nose, breathy, amused, playful and seductive."),
    "yatta": (["やった！", "やったぁ！"], "Triumphant cheer, joyful burst of happiness, high energy."),
    "nice": (["ナイス！", "ナイスっ！"], "Quick, punchy and upbeat praise, cheerful and energetic."),
}


def free_comfy():
    try:
        req = urllib.request.Request("http://127.0.0.1:8188/free", json.dumps({"unload_models": True, "free_memory": True}).encode(),
                                     {"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=10).read()
    except Exception as e:  # ComfyUI éteint : rien à vider
        print("ComfyUI non vidé :", e)


def load(path):
    return Qwen3TTSModel.from_pretrained(str(path), device_map="cuda:0", dtype=torch.bfloat16, attn_implementation="sdpa")


def design(names=None):
    free_comfy()
    out = OUT / "design"
    out.mkdir(parents=True, exist_ok=True)
    m = load(DESIGN)
    for name, instruct in CANDIDATES.items():
        if names and name not in names:
            continue
        wavs, sr = m.generate_voice_design(text=REF_TEXT, language="Japanese", instruct=instruct)
        sf.write(out / f"{name}.wav", wavs[0], sr)
        print("ok", name, f"{len(wavs[0]) / sr:.1f} s", flush=True)


def lines(cand):
    free_comfy()
    ref = OUT / "design" / f"{cand}.wav"
    out = OUT / "lines" / cand
    out.mkdir(parents=True, exist_ok=True)
    m = load(BASE)
    audio, sr = sf.read(ref)
    prompt = m.create_voice_clone_prompt(ref_audio=(audio, sr), ref_text=REF_TEXT)
    for k, (text, _, _) in LINES.items():
        wavs, sr = m.generate_voice_clone(text=text, language="Japanese", voice_clone_prompt=prompt)
        sf.write(out / f"{k}.wav", wavs[0], sr)
        print("ok", k, f"{len(wavs[0]) / sr:.1f} s", flush=True)


def expr(takes=3):
    free_comfy()
    out = OUT / "expr"
    out.mkdir(parents=True, exist_ok=True)
    m = load(DESIGN)
    for k, (texts, emo) in EXPR.items():
        n = 0
        for t in texts:
            for _ in range(takes if len(texts) == 1 else max(1, takes // len(texts) + (1 if takes % len(texts) else 0))):
                n += 1
                wavs, sr = m.generate_voice_design(text=t, language="Japanese", instruct=f"{EXPR_BASE} {emo}")
                sf.write(out / f"{k}_{n}.wav", wavs[0], sr)
                print("ok", k, n, t, f"{len(wavs[0]) / sr:.1f} s", flush=True)


def rank_a():
    """Rang A refait : sans rire ni souffle (les prises de expr() respiraient trop), textes alternatifs."""
    free_comfy()
    out = OUT / "expr"
    m = load(DESIGN)
    emo = ("Clear, confident and satisfied praise, spoken cleanly and firmly with a warm smile, "
           "no laughing, no breathiness, no sighing, steady voice, natural pacing.")
    n = 0
    for t in ["いい調子！ さすがだね！", "いい感じ！ やるじゃない！", "いいね、その調子！"]:
        for _ in range(2):
            n += 1
            wavs, sr = m.generate_voice_design(text=t, language="Japanese", instruct=f"{EXPR_BASE} {emo}")
            sf.write(out / f"rank_a2_{n}.wav", wavs[0], sr)
            print("ok rank_a2", n, t, f"{len(wavs[0]) / sr:.1f} s", flush=True)


def rank_s():
    """Rang S refait : la voix dérivait d'une prise à l'autre. 4 prises VoiceDesign + 3 clones de la prise A retenue (timbre de référence)."""
    free_comfy()
    out = OUT / "expr"
    emo = ("Overjoyed and proud, bursting with excitement, big smile, voice rising, very expressive and lively, "
           "but keeping the same low, smooth, velvety mature timbre.")
    text = "やったね！ 完璧だよ、すごい！"
    m = load(DESIGN)
    for n in range(1, 5):
        wavs, sr = m.generate_voice_design(text=text, language="Japanese", instruct=f"{EXPR_BASE} {emo}")
        sf.write(out / f"rank_s2_{n}.wav", wavs[0], sr)
        print("ok rank_s2", n, f"{len(wavs[0]) / sr:.1f} s", flush=True)
    del m
    torch.cuda.empty_cache()
    m = load(BASE)
    audio, sr = sf.read(out / "rank_a2_4.wav")
    prompt = m.create_voice_clone_prompt(ref_audio=(audio, sr), ref_text="いい感じ！ やるじゃない！")
    for n in range(5, 8):
        wavs, sr = m.generate_voice_clone(text=text, language="Japanese", voice_clone_prompt=prompt)
        sf.write(out / f"rank_s2_{n}.wav", wavs[0], sr)
        print("ok rank_s2", n, "clone", f"{len(wavs[0]) / sr:.1f} s", flush=True)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "design":
        design(sys.argv[2:])
    elif len(sys.argv) >= 2 and sys.argv[1] == "rank_s":
        rank_s()
    elif len(sys.argv) >= 2 and sys.argv[1] == "rank_a":
        rank_a()
    elif len(sys.argv) >= 2 and sys.argv[1] == "expr":
        expr(int(sys.argv[2]) if len(sys.argv) > 2 else 3)
    elif len(sys.argv) == 3 and sys.argv[1] == "lines":
        lines(sys.argv[2])
    else:
        sys.exit(__doc__)
