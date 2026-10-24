# Dancing Every Night

Charte graphique du jeu de rythme homebrew pour PS Vita *Dancing Every Night*. Elle documente le style « Cascade pop » validé en maquettes le 14 et le 15 octobre 2026 (sélection, jeu, résultats, accueil, packs, chargement, pause, compte à rebours, LiveArea), accordé au visuel d'accueil le 15 octobre 2026 (sept couleurs ajustées, tableau en fin de page).

## Principes

- **Inspiration assumée d'un jeu de danse rythmique existant, identité propre.** On reprend la grammaire (cercle, couleurs de jugement franches, récompenses généreuses, écran qui réagit à la musique), jamais ses éléments.
- **La typographie fait le spectacle.** Un titre n'est pas une étiquette : il est énorme, incliné, cerné d'encre, avec une ombre décalée.
- **Imprimé plutôt qu'écran.** Aplats francs, ombres décalées, contours d'encre ; pas de dégradés techniques ni de halos, sauf sur les notes en jeu.
- **Rien de statique.** Tout décor bat au tempo du morceau (temps fort tous les 4 temps) ; un décor qui ne bouge pas est retiré.
- **C'est l'interface qui s'accorde au logo**, pas l'inverse.
- **Lisible à 30 cm sur un écran de 5 pouces (960 × 544).**

## Couleurs

Thème unique « Nuit » : le jeu est toujours sombre.

| Rôle | Token | Usage |
| --- | --- | --- |
| Encre | `ink` | contours, cartes, bandeaux |
| Accent | `pink` | éclaboussure, ombres, onglet actif |
| Vedette | `lemon` | titre sélectionné, score, PERFECT, étoiles |
| Côté gauche | `mint` (cyan) | arc de la croix directionnelle, pastille active |
| Bichromie | `violet` → `duo-pink` | toutes les images de morceau (et la vidéo en jeu), en `multiply` |
| Atténué | `lavender` | MISS |

Règles :
- Le citron et le blanc se posent **toujours** sur de l'encre ou avec un contour d'encre de 5 à 9 px.
- Les images de morceau ne s'affichent jamais en couleurs brutes : bichromie violet → rose, puis voile d'encre côté texte.
- Les tokens `accueil-*` ne sont pas utilisés dans l'interface : ce sont les couleurs **mesurées sur le logo**, la référence qui a servi aux ajustements ci-dessous.

## Typographie

Trois familles libres (Google Fonts, licence OFL), rastérisées dans l'atlas de polices du jeu :

- **Anton** (`display`) : tout ce qui se lit de loin. Capitales, contour d'encre (`paint-order: stroke fill`), ombre décalée.
- **Bricolage Grotesque** (`grotesk`) : la fiche du morceau (artiste, stats, pastilles).
- **M PLUS Rounded 1c** (`rounded`) : petites étiquettes et aides de boutons, en graisse 800.

Les zéros de tête du score s'affichent en contour discret. Orbitron (ancienne police) est abandonnée : ses zéros barrés se lisaient comme des cases.

## Formes

- **La cascade** : la liste des morceaux (ou des packs, ou du menu pause) devient la composition. Inclinée de `tilt-cascade`, l'élément sélectionné énorme en citron, ses voisins en contour blanc, les éloignés en contour à 55 %.
- **L'éclaboussure** : aplat rose derrière la sélection, polygone calculé à partir d'un hachage du titre (toujours la même forme pour un morceau), quelques éclats anguleux autour. Pas de coulures. Réservée à la sélection et au chargement : PERFECT et les résultats ne la reprennent pas.
- **La carte d'encre** : rectangle d'encre incliné (`tilt-card`), ombre décalée `shadow-card`.
- **Les pastilles** : `radius-pill`, difficulté active en cyan (`mint`).
- **Le tampon** : plaque d'encre arrondie (`radius-stamp`), lettre géante à double ombre rose et cyan, inclinée de `tilt-stamp`.
- **Confettis** : losanges et étoiles à quatre branches, placés selon le titre, hors des zones de lecture.
- **Facettes de vitrail** : triangles de couleurs (violets, cyan, roses) cernés d'encre, fond de l'accueil et de la LiveArea.

## Mouvement

- Une seule courbe pour tous les battements : montée de 40 ms, retombée douce ; le temps fort (1 temps sur 4) est plus marqué.
- Changer de morceau anime tout l'écran : la cascade glisse, l'éclaboussure éclate avec une gerbe de confettis, l'image se fond, la fiche entre par la droite.
- Accueil : les deux plans de ville défilent à l'infini (28 et 64 px/s), la mosaïque reste fixe et scintille ; le logo bat d'un bloc (+1,8 %), le titre ajoute +1,4 %.

## Notes de jeu (exceptions)

Les notes gardent leurs sprites, leur halo et un contour de la couleur ravivée de leur touche (5,5 px, ajouté vers l'extérieur du disque de 54 px). Les flèches sont redessinées au trait, à la graisse de △ ○ ✕. Les arcs sont en aplat cyan (gauche) et rose (droite), cernés d'encre.

## Logo et hôtesse

- Logo : version mosaïque (ChatGPT), en trois couches pour l'accueil : boule disco, hôtesse, bandeau-titre (groupe d'assets **Logo**).
- L'hôtesse : personnage original (queue de cheval noire, nœud rouge, yeux rouges, uniforme noir à col blanc). Pour l'instant **uniquement** dans le logo et la LiveArea ; ses poses pour les autres écrans viendront en une seule passe.

## Contraintes Vita

- Écran 960 × 544, 60 fps. Rien de dessiné par note et par image au-delà du nécessaire (un gel GPU a déjà eu lieu).
- Textures ≤ 2048 px, en atlas ; jamais réduites de plus de 2× à l'affichage.
- La bichromie des images de morceau se précalcule à l'import (`make song`).
- LiveArea et icône : PNG 8 bits indexés (`tools/livearea.py`).

## Ajustements vers le visuel d'accueil (appliqués le 15 octobre 2026)

Le logo est plus bleu et plus magenta que l'ancienne interface : il n'a pas de menthe, son or est plus chaud que l'ancien citron, son rose plus profond. Les sept couleurs ont été recalées sur lui ; les noms des tokens ne changent pas (`mint` désigne désormais un cyan).

| Token | Avant | Après | D'après |
| --- | --- | --- | --- |
| `pink` | `#ff2e8a` | `#f70c93` | `accueil-pink` |
| `lemon` | `#ffe14a` | `#fbd63f` | `accueil-gold` |
| `mint` | `#3cf0c0` | `#4fc8f0` | `accueil-ice` / cyan des facettes |
| `violet` | `#3b1fa8` | `#2a2fb0` | vers `accueil-blue` |
| `duo-pink` | `#e0287f` | `#c81aa8` | vers `accueil-magenta` |
| `ink` | `#140c2e` | `#0b0720` | `accueil-ink`, un peu plus bleu |
| `lavender` | `#d7c9e8` | `#eb86d5` | rose clair des facettes |

Les maquettes publiées avant cette date gardent les anciennes valeurs ; le jeu prendra les nouvelles à l'intégration.
