# Configurateur USB Web Serial

Le configurateur est une page statique, sans Node.js ni dépendance externe.
Il utilise Web Serial dans Chrome ou Edge et ignore les logs ESP-IDF qui ne commencent pas par `@@CFG:`.

## Utilisation

```sh
cd tools/configurator
python -m http.server 8080
```

Ouvrir <http://localhost:8080>, cliquer sur **Connecter l’ESP32**, puis choisir le port série de la carte.
Fermer `idf.py monitor`, minicom et tout autre terminal avant la connexion : un seul lecteur série est autorisé.
Le débit est 115200, 8N1, sans contrôle de flux.

**Actualiser le cache** relit les appareils déjà connus (environ une demi-seconde). **Rescanner Matter** relance toute la découverte (environ 30 à 45 secondes avec 167 endpoints) et conserve la liste en cache à l’écran pendant l’analyse.

La recherche porte sur le nom Matter canonique, l’endpoint et la pièce. Les appareils peuvent être filtrés, triés par nom, endpoint, pièce, disponibilité ou pilotabilité, et sélectionnés en groupe pour affecter, déplacer ou retirer une pièce. Chaque endpoint affiche aussi sa sortie On/Off lorsqu’elle est connue, sinon « état inconnu » ou « non pilotable ». Un endpoint indisponible reste visible et configurable; une joignabilité absente est affichée comme inconnue.

Le bouton **Enregistrer sur l’ESP32** effectue la validation firmware, écrit uniquement l’espace NVS `control_ui`, puis relit la configuration pour vérification. Les actions d’éclairage restent asynchrones et utilisent la file Matter existante.

**Exporter JSON** produit un fichier local contenant la version d’outil, le schéma, le node ID, les pièces et les bindings. Il ne contient ni Wi-Fi, ni fabric, ni clé opérationnelle. À l’import, le node ID et les endpoints inconnus sont signalés avant confirmation.

En cas de perte de connexion, fermer puis rouvrir le port série et cliquer à nouveau sur **Connecter l’ESP32**. Les permissions Web Serial doivent être accordées à l’origine `http://localhost:8080`; `file://` n’est pas pris en charge.

Les tests rapides du parseur sont disponibles dans `tests.js` et peuvent être exécutés dans la console du navigateur avec `import('./tests.js')`.

## Accès par Wi-Fi

Le firmware héberge aussi cette même interface sur son adresse Wi-Fi : aucun
serveur à démarrer sur le PC et aucun câble USB nécessaire après le premier flash.
Sur l’écran, ouvrir **Configurer les appareils** : l’adresse `http://…` et le code
Wi-Fi sont affichés sous les actions. Ils apparaissent également dans le
configurateur USB après connexion.

Depuis un PC sur le même réseau, ouvrir cette adresse, saisir le code puis cliquer
sur **Connecter en Wi-Fi**. Appareils, pagination, rescans Matter, commandes et
sauvegarde/relecture utilisent les mêmes validations et le même worker que l’USB.
Le code est propre à la carte, conservé au redémarrage et n’est pas inclus dans les
exports JSON. Il reste en mémoire dans l’onglet, sans stockage navigateur.

L’API locale est `POST /api/config`, avec `Content-Type: application/json` et
`Authorization: Bearer <code>`. Elle reçoit les mêmes objets `{cmd, request_id, …}`
que l’USB et renvoie la réponse JSON finale, y compris pour les commandes Matter.
Les opérations réseau sont traitées une par une, avec une attente maximale de
60 secondes. Une erreur réseau ou un délai dépassé ne garantit pas qu’une commande
ou une sauvegarde n’a pas déjà été appliquée : relire l’état avant de recommencer.

Cet accès utilise HTTP sur le réseau local : le code et les échanges ne sont pas
chiffrés. Ne pas exposer le port 80 sur Internet. Le serveur ne fournit pas de mise
à jour du firmware par Wi-Fi.

Vérifications du client et des modèles : `node tools/dev/test-config-wifi.mjs`
depuis la racine du projet.
