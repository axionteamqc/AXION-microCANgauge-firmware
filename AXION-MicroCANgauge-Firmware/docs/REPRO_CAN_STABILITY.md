# CAN TX Passive Repro (simulator stays alive)

But : vérifier que la jauge ne “tue” pas le simulateur après boot (ACK-only, pas de reopen loop).

Pré-requis :
- Un simulateur ECU lançable en ligne de commande (stdout produit des trames/logs). Exemple : `SIM_CMD='C:\path\to\sim.exe --port COM5 --bitrate 500000'`.
- La jauge alimentée séparément (boot manuel).

Script (Windows/PowerShell) :
```
cd tools/dev
.\repro_can_stability.ps1 -SimCommand $env:SIM_CMD
```
Étapes automatisées :
- Lance le simulateur et redirige stdout -> `%TEMP%\sim_can_repro.log`.
- Attend 5 s, puis invite à alimenter la jauge.
- Laisse tourner 70 s. À la fin, vérifie que le simulateur est toujours en vie et affiche les 10 dernières lignes du log.

Critère de succès :
- Le simulateur ne s’arrête pas (processus toujours vivant) après 60+ s de cohabitation.
- `%TEMP%\sim_can_repro.log` continue de se remplir (≥ quelques lignes).

Notes :
- Le script n’émet aucune trame CAN côté jauge (TX app désactivé). Si le simulateur stoppe, suspecter le bitrate ou le câblage.
- Adapter `-DurationSeconds` ou `-BootDelaySeconds` si besoin. напит
