Ho impostato i remote nel repository /opt/other/GrADS-2.2.5:

- origin → https://github.com/davidesacchetti/GrADS.git (il tuo);
- upstream → https://github.com/paolo-oliveri/GrADS.git (quello di Paolo, prima si chiamava origin).

Il ramo master ora segue upstream/master. Per aggiornarlo dal repository di Paolo: git pull upstream master.

Il push non l'ho fatto, perché da qui non posso autenticarmi su GitHub. Inoltre il tuo repository probabilmente non esiste ancora: l'accesso in lettura senza credenziali non ha funzionato, quindi è privato o non è stato creato.

Per pubblicare:
1. Crea il repository vuoto su GitHub, davidesacchetti/GrADS, senza README né licenza, per evitare conflitti al primo push. In alternativa fai un fork di quello di Paolo dalla sua pagina: in quel caso contiene già master.
2. Fai il push dei rami:
cd /opt/other/GrADS-2.2.5
git push -u origin master 2.2.5
   Con https, come password GitHub vuole un personal access token, non la password dell'account. Si crea da Settings → Developer settings → Personal access tokens, con il permesso repo.

   In alternativa usa ssh: aggiungi una tua chiave pubblica su GitHub (Settings → SSH keys) e cambia l'URL:
git remote set-url origin git@github.com:davidesacchetti/GrADS.git
   Al primo collegamento ssh ti chiederà di accettare la chiave host di github.com.

Sul ramo 2.2.5 ci sono tutti i commit di questi giorni: widget Cairo, dropmenu, mappe e release 2.2.5-3, fino al tuo 882b9b8.
