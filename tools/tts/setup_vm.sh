#!/bin/bash
# Installe Qwen3-TTS (licence Apache 2.0) dans un environnement isole de la VM, a cote de ComfyUI.
# A lancer DANS le conteneur : distrobox enter comfy -- bash ~/tts/setup_vm.sh   (journal : ~/tts/setup.log)
# - torch cu128 (meme version que ComfyUI), qwen-tts, soundfile
# - modeles : VoiceDesign (cree une voix a partir d'une description) et Base (reutilise cette voix : memes timbres d'une replique a l'autre)
set -e
mkdir -p ~/tts && cd ~/tts
[ -d venv ] || uv venv --python 3.12 venv
uv pip install --python venv/bin/python torch torchaudio --index-url https://download.pytorch.org/whl/cu128
uv pip install --python venv/bin/python qwen-tts soundfile "huggingface_hub[cli]"
for m in Qwen3-TTS-12Hz-1.7B-VoiceDesign Qwen3-TTS-12Hz-1.7B-Base; do
    echo "== telechargement $m"
    venv/bin/hf download "Qwen/$m" --local-dir "models/$m" || venv/bin/huggingface-cli download "Qwen/$m" --local-dir "models/$m"
done
venv/bin/python -c "import torch, qwen_tts; print('OK torch', torch.__version__, 'cuda', torch.cuda.is_available())"
echo "=== INSTALLATION TERMINEE ==="
