# Atelier 2 - Simulateur de blockchain PoW/PoS

Cette application fournit le noyau de simulation blockchain, les scénarios d'attaque, la collecte de métriques et un premier détecteur explicable. Elle reprend les notions de hachage, de chaînage, de PoW et de PoS des exercices précédents.

## Capacités actuelles

- Blockchain en mémoire avec bloc genesis, hashes SHA-256 et chaînage des blocs.
- Comptes, stake bloqué, solde disponible, transactions et récompenses de bloc.
- PoW : sélection du mineur pondérée par sa puissance de calcul, puis recherche effective d'un nonce.
- PoS : sélection déterministe pondérée par le stake bloqué ; les récompenses augmentent le solde disponible mais pas le stake.
- Participants et transactions configurables depuis le CLI.
- Validation complète de la chaîne : parent, hash, nonce PoW, producteur attendu, transactions, soldes et récompenses.
- Point d'extension `ProducerStrategy` pour les stratégies d'attaque ou de défense.
- Scénarios 51 % / double dépense, selfish mining, block withholding et Sybil, exécutés sur des copies de chaîne.
- Export CSV des événements et métriques (`data/experiments/run_*`) et rapport de détection dans le terminal.

## Compilation

Depuis `codes/atelier2` :

```powershell
& "C:\msys64\mingw64\bin\gcc.exe" -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude src\sha256.c src\blockchain.c src\accounts_store.c src\session_store.c src\metrics.c src\detection.c src\mitigation.c src\evaluation.c src\analysis.c src\attack.c src\attack_51.c src\attack_selfish.c src\attack_withholding.c src\attack_sybil.c src\menu.c src\main.c -o atelier2.exe -lm
```

`src/detection.c`, `src/mitigation.c` et `-lm` sont requis : le détecteur emploie les fonctions mathématiques de la bibliothèque standard et la mitigation s'appuie sur ses alertes. La commande ci-dessus est également celle utilisée par les scripts de test.

## État fonctionnel

Les modules d'attaque (51 %, selfish mining, block withholding et Sybil), de métriques, de détection et de mitigation sont présents. Les expériences d'attaque exportent `summary.csv`, `events.csv`, `nodes.csv`, `scenario.csv`, `detection.csv`, `detection_nodes.csv` et `mitigation.csv` dans `data/experiments/run_*`.

Une campagne d'évaluation reproductible compare `NORMAL`, `ATTAQUE` et `PROTEGE` sur 3 seeds, 3 tailles de réseau et 3 délais simulés. Ses CSV et graphiques SVG sont produits dans `data/evaluations/campaign_*`.

## Mode interactif

Lancez le programme sans argument pour ouvrir le menu :

```powershell
.\atelier2.exe
```

Le menu charge directement les comptes sauvegardes, puis permet de modifier les parametres, gerer les participants et la file de transactions, afficher les metriques et verifier l'integrite. Le nom d'un participant est son identifiant immuable : seule sa suppression puis la creation d'un autre compte peut changer ce nom. La file affiche l'index, le statut EN_ATTENTE, l'emetteur, le destinataire et les unites.

L'option `Lancer une campagne d'evaluation` ne modifie jamais la session interactive ni les comptes persistants. Elle produit les fichiers `runs.csv`, `detection.csv`, `thresholds.csv`, `performance.csv`, `aggregate.csv` et cinq graphiques SVG.

L'option `Gerer les protections` active ou desactive globalement, ou attaque par attaque, les contre-mesures persistantes. Les seuils pedagogiques sont fixes : alerte `ELEVE` ou `CRITIQUE`, six confirmations minimales, reorganisation maximale d'un bloc confirme et au plus 50 % de voisins Sybil. Une simulation affiche les trois colonnes `NORMAL`, `ATTAQUE` et `PROTEGE`. Une branche critique protegee ne peut pas etre adoptee.

Le cycle interactif de creation d'un bloc est volontairement explicite :

1. ajouter les transactions dans la file ;
2. preparer un bloc candidat en choisissant manuellement jusqu'a huit indices, dans l'ordre voulu, puis son consensus PoW ou PoS ;
3. lors de cette premiere preparation, le genesis est cree automatiquement ;
4. visualiser la prevalidation (consensus, soldes, producteur, parent et recompense) ;
5. confirmer le minage et l'ajout, ou abandonner le brouillon sans modifier la file.

Une transaction ne devient VALIDEE qu'apres l'ajout du bloc. Chaque bloc conserve son consensus dans son hash : une meme chaine peut donc contenir des blocs PoW et PoS. La difficulte configuree est appliquee a tous les blocs PoW. Pendant qu'un brouillon est actif, les files sont verrouillees afin que sa selection reste coherente.

Le stake est un montant bloque du solde total. Le menu affiche donc le stake bloque, le solde disponible et le solde total. Seul le solde disponible peut financer une transaction. Apres le genesis automatique, les comptes ne sont plus modifies directement. Le menu « Gerer les mises a jour de ressources » permet de demander un nouveau hash power et un nouveau stake bloque absolus pour un compte. Une hausse bloque la difference depuis le solde disponible ; une baisse la debloque apres le bloc valide. Ces demandes sont incluses dans le hash du bloc, appliquees apres le choix du producteur, les transactions et la recompense, puis deviennent actives pour le bloc suivant.

## Comptes persistants

Au premier lancement, le registre est absent et la liste de comptes est vide. Ajoutez les participants depuis le menu de gestion. Chaque ajout, modification, suppression et ajout valide de bloc est sauvegarde immediatement.

Le registre est stocke a cote de atelier2.exe dans data/comptes.csv. Il contient une ligne par compte avec les quatre colonnes name, hash_power, locked_stake et available_balance, separees par des points-virgules. L'ancien format stake et balance reste lu sans perte : l'ancien solde est considere disponible et l'ancien stake deja bloque.

Au lancement suivant, la session interactive complete est restauree depuis les fichiers texte de data/ : chaine, blocs, comptes, soldes, ressources, files en attente, metriques et brouillon actif. Le registre comptes.csv reste maintenu pour compatibilite. L'option de reinitialisation efface la session blockchain sauvegardee mais conserve les comptes.

Le programme conserve ses options de lancement lorsqu'au moins un argument est fourni : elles restent utiles pour les futures experiences automatisables.

## Scenarios d'attaque

Le menu propose l'option `Simuler une attaque`. Chaque scenario travaille sur une copie de la chaine, affiche la branche de reference, la branche attaquee, la branche protegee et une trace des evenements. Une chaine attaquee ne peut etre adoptee qu'apres un succes, une validation complete et l'autorisation de la mitigation.

Le CLI permet de rejouer un scenario avec `--attack 51|selfish|withholding|sybil`. Les comptes choisis via `--attacker` doivent exister parmi les options `--node`. L'attaque 51 % accepte `--consensus pow|pos`, `--spender`, `--merchant`, `--private-recipient`, `--amount`, `--confirmations` et `--rounds`. Selfish mining ajoute `--gamma`; block withholding utilise `--pool-member`; Sybil utilise `--sybil-victim`, `--sybils` et `--victim-degree`.

```powershell
.\atelier2.exe --consensus pow --difficulty 0 --seed 7 `
  --node Alice:70:70:100 --node Bob:30:30:100 `
  --attack 51 --attacker Alice --spender Alice --merchant Bob `
  --private-recipient Alice --amount 10 --confirmations 1 --rounds 30
```

Par defaut, les protections CLI sont actives et ne lisent pas la session locale. Elles peuvent etre controlees avec :

```text
--mitigation on|off
--mitigation-51 on|off
--mitigation-selfish on|off
--mitigation-withholding on|off
--mitigation-sybil on|off
```

## Exemples

Simulation CLI PoW par defaut : trois noeuds (Alice, Bob, Charlie), deux transactions et cinq blocs apres le genesis. Ce mode n utilise pas le registre persistant.

```powershell
.\atelier2.exe --consensus pow --blocks 5 --difficulty 3
```

Simulation PoW contrôlée :

```powershell
.\atelier2.exe --consensus pow --blocks 4 --difficulty 3 --seed 42
```

Simulation PoS avec participants et transactions personnalisés :

```powershell
.\atelier2.exe --consensus pos --blocks 4 --seed 7 `
  --node Awa:40:50:100 --node Bilal:35:30:100 --node Chloe:25:20:100 `
  --tx Awa:Bilal:25 --tx Bilal:Chloe:10
```

Options disponibles :

```text
--consensus pow|pos
--blocks N
--difficulty N          (0 a 6)
--seed N
--reward N
--node nom:hash_power:stake_bloque:solde_total
--tx emetteur:destinataire:montant
--attack 51|selfish|withholding|sybil
--mitigation on|off
--mitigation-51 on|off
--mitigation-selfish on|off
--mitigation-withholding on|off
--mitigation-sybil on|off
--campaign
--campaign-profile quick|complete
--campaign-seeds N
--campaign-nodes 3,5,8
--campaign-delays 1,2,3
--campaign-attacks 51,selfish,withholding,sybil
--evaluation-dir chemin
--analyze chemin_campagne
--help
```

Les nœuds doivent posséder une puissance totale strictement positive en PoW, ou un stake total strictement positif en PoS. Pour chaque nœud, le stake bloqué ne peut pas dépasser le solde total. Une transaction est refusée si un compte est inconnu, si son montant est nul ou si son émetteur ne possède pas le solde disponible requis.

## Protocole reproductible

1. Compiler avec la commande PowerShell placée au début de ce fichier, puis exécuter les vérifications avec `./tests/run_tests.cmd`.
2. Lancer la campagne complète déterministe (108 scénarios attaqués, chacun décliné en états normal, attaqué et protégé) :

```powershell
.\atelier2.exe --campaign --campaign-profile complete --seed 2026 --evaluation-dir data\evaluations
```

3. Analyser les CSV du dossier produit. La commande génère `statistics.csv`, `comparison.csv` et `interpretation.md` sans modifier la session interactive :

```powershell
.\atelier2.exe --analyze data\evaluations\campaign_70073c59
```

Les graphiques SVG et les fichiers de campagne sont enregistrés dans `data/evaluations/campaign_<empreinte>/`. Les ticks, le travail PoW logique et les résultats CSV sont déterministes pour une même configuration ; le temps CPU reste indicatif et dépend de la machine.

## Tests

La commande suivante compile le programme, le test du moteur et vérifie aussi les erreurs CLI attendues :

```powershell
.\tests\run_tests.cmd
```

Les tests couvrent SHA-256, les chaînes honnêtes PoW/PoS, les transactions invalides, l'intégrité (transaction, parent, nonce et producteur), les scénarios d'attaque et le scénario interactif.

## Limites de cette itération

Le simulateur reste pédagogique : les délais sont des ticks logiques, les signatures et une diffusion P2P complète ne sont pas implémentées. Les attaques sont des scénarios contrôlés, non un réseau distribué réel. La détection repose sur des règles explicables et des métriques simulées ; elle n'applique pas encore de contre-mesure automatique.
