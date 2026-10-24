# ffmpeg.wasm (version en ligne de l'éditeur)

- `ffmpeg.js`, `814.ffmpeg.js` : @ffmpeg/ffmpeg 0.12.10 (dist/umd), licence MIT — https://github.com/ffmpegwasm/ffmpeg.wasm
- `util.js` : @ffmpeg/util 0.12.1 (dist/umd), licence MIT

Le moteur (@ffmpeg/core 0.12.6, `ffmpeg-core.js` + `ffmpeg-core.wasm`, ~31 Mo) n'est pas copié ici : il dépasse la
limite de 25 Mo par fichier de Cloudflare Pages. L'éditeur le télécharge depuis unpkg.com au premier usage
(CORS autorisé), puis le navigateur le garde en cache. FFmpeg lui-même est sous LGPL/GPL, compatible avec la GPLv3 du jeu.
