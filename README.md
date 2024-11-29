# Stargate-beyond-the-universe
![MainMenu](https://github.com/PaulSenecal/Stargate-beyond-the-universe/assets/102356707/efb6a4cb-27b4-4f70-9ca8-9f74ca68eac1)
<h3>Description du Projet :</h3>
Suite a la perte de nombreux fichier du jeux source j’ai décidé de recréer le jeu avec un nouveau moteur, nouvelle feature, contenu ... 
Mais également plus optimiser, en effet le jeu originellement manquait cruellement de contenu et d’optimisation.
**PS :** Je travaille sur ce projet sur mon temps libre, en parallèle de nombreux autres projets professionnels. L’avancement risque donc d’être relativement lent.



<h3>Description du Jeu :</h3>
Le jeu se déroule à bord du Destiny, un cadre idéal pour son gameplay survivaliste en raison du contexte de survie qu’il propose. 
Le jeu se pilote via des lignes de commande que l’utilisateur doit saisir pour contrôler les différents systèmes du vaisseau. 
Des interfaces guideront le joueur en lui fournissant des conseils sur sa position, l’état des systèmes opérationnels, les dégâts subis, etc.

L’objectif du jeu est de poursuivre la mission du Destiny. Les Anciens auraient détecté une structure complexe dans le fond diffus cosmologique, 
suggérant la présence d'une forme d'intelligence lors de la formation de l'Univers, et que le Big Bang aurait été provoqué intentionnellement. 
La mission du vaisseau est d’approfondir ces recherches en explorant les limites de l’Univers observable. Cependant, 
après avoir découvert l'ascension (la transformation du corps matériel en énergie), les Anciens, appelés Alterans, perdirent tout intérêt pour cette mission.

En 2009, une équipe d'exploration terrienne découvrit le vaisseau lorsqu'elle activa pour la première fois une adresse à 9 chevrons sur une Porte des Étoiles. 
Cette équipe se retrouva piégée à bord du Destiny, poursuivant le chemin prévu par sa programmation. 
L'équipe se retrouva bloquée à bord du Destinée, qui continuait le chemin tel que programmé.

Leur mission consiste à s’adapter à une technologie inconnue tout en collectant des ressources essentielles à leur survie : eau, nourriture, oxygène… 
tout doit être trouvé et réparé. Comme si cela ne suffisait pas, il semblerait que des menaces planent sur eux, 
même aux confins de l’univers. Ces jeunes Terriens devront faire face à de nombreux dangers tout en étant perdus dans les profondeurs de l’univers.

## Organisation des fichiers
ProjectRoot/
├── CMakeLists.txt                  # Fichier de configuration principal
│
├── src/                            # Code source principal
│   ├── main.cpp
│   ├── game.h                      # Classe principale du jeu
│   ├── game.cpp
│   │
│   ├── core/                       # Composants fondamentaux
│   │   ├── gamestate.h            # Gestion des états du jeu
│   │   ├── gamestate.cpp
│   │   ├── settings.h             # Gestionnaire de configuration
│   │   └── settings.cpp
│   │
│   │
│   ├── ui/                        # Interfaces utilisateur
│   │   ├── menus/                 # Menus du jeu
│   │   │   ├── mainmenu.h
│   │   │   ├── mainmenu.cpp
│   │   │   ├── mainmenu.ui
│   │   │   ├── settings/
│   │   │   │   ├── settingsmenu.h
│   │   │   │   ├── settingsmenu.cpp
│   │   │   │   └── settingsmenu.ui
│   │   │   ├── pause/
│   │   │   │    ├── pausemenu.h
│   │   │   │    ├── pausemenu.cpp
│   │   │   │    └── pausemenu.ui
│   │   │   ├──battle/                # Scène de bataille
│   │   │   │     ├── battlescene.h
│   │   │   │     ├── battlescene.cpp
│   │   │   │     └── battlescene.ui
│   │   │   └──Arena/                # Scène de bataille
│   │   │        ├── arenascene.h
│   │   │        ├── arenascene.cpp
│   │   │        └── arenascene.ui            
│   │   └── hud/                   # Éléments d'interface en jeu
│   │       ├── gameinfo.h
│   │       └── gameinfo.cpp
│   │
│   ├── entities/                  # Entités du jeu
│   │   ├── entity.h              # Classe de base pour les entités
│   │   ├── entity.cpp
│   │   ├── ships/                # Vaisseaux
│   │   │   ├── ship.h           # Classe de base pour les vaisseaux
│   │   │   ├── ship.cpp
│   │   │   ├── playership.h
│   │   │   └── playership.cpp
│   │   └── players/              # Joueurs
│   │       ├── player.h
│   │       └── player.cpp
│   │
│   ├── physics/                   # Moteur physique
│   │   ├── physicengine.h
│   │   ├── physicengine.cpp
│   │   ├── collisions/
│   │   │   ├── collisiondetector.h
│   │   │   └── collisiondetector.cpp
│   │   └── movements/
│   │       ├── movement.h
│   │       └── movement.cpp
│   │
│   └── utils/                     # Utilitaires
│       ├── logger.h              # Système de logging
│       ├── logger.cpp
│       ├── config.h             # Configuration
│       └── config.cpp
│
├── resources/                     # Ressources du jeu
│   ├── images/
│   │   ├── sprites/
│   │   ├── backgrounds/
│   │   └── ui/
│   ├── audio/
│   │   ├── music/
│   │   └── sfx/
│   ├── videos/
│   ├── fonts/
│   └── translations/             # Fichiers de traduction
│
├── tests/                        # Tests unitaires
│   ├── CMakeLists.txt
│   ├── test_physics.cpp
│   └── test_entities.cpp
│
└── docs/                         # Documentation
    ├── api/
    ├── design/
    └── README.md
Last edited il y a 5 minutes
