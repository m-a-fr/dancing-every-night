# Raccourcis. Toutes les commandes sont utilisables par Claude Code.
# VITA_IP peut être défini dans l'environnement ou dans un fichier .env
-include .env
export VITA_IP

.PHONY: desktop run pc vita release deploy install-vpk clean map song editor editor-site check

desktop:
	cmake -S . -B build-desktop -DCMAKE_BUILD_TYPE=Debug
	cmake --build build-desktop -j

run: desktop
	rsync -a --delete assets/ build-desktop/assets/
	cd build-desktop && ./vitagame $(CHART)

vita:
	./scripts/build-vita.sh

# Version PC distribuable pour ce système (SDL2 intégrée, assets commités seulement) -> dist/
pc:
	./scripts/build-pc.sh

# .vpk de release : seulement les assets commités (pas les morceaux personnels) -> dist/<nom>-<version>.vpk
release:
	RELEASE=1 ./scripts/build-vita.sh

deploy: vita
	./scripts/deploy.sh

install-vpk: vita
	./scripts/install-vpk.sh

# Vérifie une map avant de la partager : make check MAP=ma_map.zip (ou un dossier de map)
check: desktop
	./build-desktop/vitagame --check "$(MAP)"

clean:
	rm -rf build-desktop build-vita build-release build-pc

# Génère une carte depuis une piste : make map SONG=/chemin/piste.ogg NAME=ma_chanson [DIFF=normal]
map:
	tools/.venv/bin/python tools/audio2chart.py "$(SONG)" $(if $(NAME),--name $(NAME)) $(if $(DIFF),--difficulty $(DIFF)) $(if $(PACK),--pack "$(PACK)")

# Nouveau morceau complet (piste, vidéo, fond, 4 difficultés) depuis un lien YouTube ou un fichier :
#   make song SRC="https://youtu.be/..." [TITLE="..."] [ARTIST="..."] [PACK="..."] [NAME=x] [IMAGE=img.jpg|URL] [NOVIDEO=1] [AUDIO=piste.ogg] [METER=3] [FORCE=1]
#   NOVIDEO=1 : image fixe au lieu d'une vidéo (IMAGE= sinon une image du clip) ; avec IMAGE= sur un lien YouTube, seul l'audio est téléchargé
song:
	tools/.venv/bin/python tools/song_import.py "$(SRC)" $(if $(TITLE),--title "$(TITLE)") $(if $(ARTIST),--artist "$(ARTIST)") \
		$(if $(PACK),--pack "$(PACK)") $(if $(NAME),--name $(NAME)) $(if $(IMAGE),--image "$(IMAGE)") $(if $(AUDIO),--audio "$(AUDIO)") $(if $(METER),--meter $(METER)) $(if $(NOVIDEO),--no-video) $(if $(FORCE),--force)

# Éditeur graphique de cartes (navigateur) : http://localhost:8765
editor:
	python3 tools/chart_editor.py

# Version en ligne de l'éditeur (sans serveur) dans build-site/, servie sur http://localhost:8766 pour l'essayer
editor-site:
	./scripts/build-editor-site.sh
	python3 -m http.server 8766 --directory build-site
