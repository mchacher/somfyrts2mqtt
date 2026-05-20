# 007 — web UI de pairing

## But
AsyncWebServer en mode AP fallback (premier boot ou WiFi KO). Formulaire pour : config WiFi, config broker MQTT, créer/supprimer une remote virtuelle (`remote_id` + counter init + nom), envoyer une trame PROG manuellement (apprentissage moteur).

## Modules cibles
`include/web_ui.h`, `src/web_ui.cpp`, assets HTML/CSS sous `data/` (LittleFS)

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
