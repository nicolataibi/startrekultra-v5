# Star Trek Ultra: 3D Multi-User Client-Server Edition
**Persistent Galaxy Tactical Navigation & Combat Simulator**

![USS Enterprise](Enterprise.jpg)

Star Trek Ultra è un simulatore spaziale avanzato che unisce la profondità strategica dei classici giochi testuali "Trek" anni '70 con un'architettura moderna Client-Server e una visualizzazione 3D accelerata hardware.

---

## 🛠️ Architettura del Sistema e Dettagli Costruttivi

Il gioco è basato sull'architettura **Subspace-Direct Bridge (SDB)**, un modello di comunicazione ibrido progettato per eliminare i colli di bottiglia tipici dei simulatori multiplayer in tempo reale.

### Il Modello Subspace-Direct Bridge (SDB)
Questa architettura d'avanguardia risolve il problema della latenza e del jitter tipici dei giochi multiplayer intensivi, disaccoppiando completamente la sincronizzazione della rete dalla fluidità della visualizzazione. Il modello SDB trasforma il client in un **Relè Tattico Intelligente**, ottimizzando il traffico remoto e azzerando la latenza locale.

1.  **Subspace Channel (TCP/IP Binary Link)**:
    *   **Ruolo**: Sincronizzazione autoritativa dello stato galattico.
    *   **Tecnologia**: Protocollo binario proprietario con **Interest Management** dinamico. Il server calcola quali oggetti sono visibili al giocatore e invia solo i dati necessari, riducendo l'uso della banda fino all'85%.
    *   **Caratteristiche**: Implementa pacchetti a lunghezza variabile e packing binario (`pragma pack(1)`) per eliminare il padding e massimizzare l'efficienza sui canali remoti.

2.  **Direct Bridge (POSIX Shared Memory Link)**:
    *   **Ruolo**: Interfaccia a latenza zero tra logica locale e motore grafico.
    *   **Tecnologia**: Segmento di memoria condivisa (`/dev/shm`) mappato direttamente negli spazi di indirizzamento di Client e Visualizzatore.
    *   **Efficienza**: Utilizza un approccio **Zero-Copy**. Il Client scrive i dati ricevuti dal server direttamente nella SHM; il Visualizzatore 3D li consuma istantaneamente. La sincronizzazione è garantita da semafori POSIX e mutex, permettendo al motore grafico di girare a 60+ FPS costanti, applicando **Linear Interpolation (LERP)** per compensare i buchi temporali tra i pacchetti di rete.

#### 🔄 Pipeline di Flusso del Dato (Propagazione Tattica)
L'efficacia del modello SDB è visibile osservando il viaggio di un singolo aggiornamento (es. il movimento di un Falco da Guerra Romulano):
1.  **Server Tick (Logic)**: Il server calcola la nuova posizione globale del nemico e aggiorna l'indice spaziale.
2.  **Subspace Pulse (Network)**: Il server serializza il dato nel `PacketUpdate`, lo tronca per includere solo gli oggetti nel quadrante del giocatore e lo invia via TCP.
3.  **Client Relay (Async)**: Il thread `network_listener` del client riceve il pacchetto, valida il `Frame ID` e scrive le coordinate nella **Shared Memory**.
4.  **Direct Bridge Signal (IPC)**: Il client incrementa il semaforo `data_ready`.
5.  **Viewer Wake-up (Rendering)**: Il visualizzatore esce dallo stato di *wait*, acquisisce il mutex, copia le nuove coordinate come `target` e avvia il calcolo LERP per far scivolare fluidamente il vascello verso la nuova posizione durante i successivi frame grafici.

Grazie a questa pipeline, i comandi via terminale viaggiano nel "Subspazio" con la sicurezza del protocollo TCP, mentre la vista tattica sul ponte rimane stabile, fluida e priva di scatti, indipendentemente dalla qualità della connessione internet.

### 1. Il Server Galattico (`trek_server`)
È il "motore" del gioco. Gestisce l'intero universo di 1000 quadranti.
*   **Logica Modulare**: Diviso in moduli (`galaxy.c`, `logic.c`, `net.c`, `commands.c`) per garantire manutenibilità e thread-safety.
*   **Spatial Partitioning**: Utilizza un indice spaziale 3D (Grid Index) per la gestione degli oggetti. Questo permette al server di scansionare solo gli oggetti locali al giocatore, garantendo prestazioni costanti ($O(1)$) indipendentemente dal numero totale di entità nella galassia.
*   **Persistenza**: Salva lo stato dell'intero universo, inclusi i progressi dei giocatori, in `galaxy.dat` con controllo di versione binaria.

### 2. Il Ponte di Comando (`trek_client`)
Il `trek_client` rappresenta il nucleo operativo dell'esperienza utente, agendo come un sofisticato orchestratore tra l'operatore umano, il server remoto e il motore di rendering locale.

*   **Architettura Multi-Threaded**: Il client gestisce simultaneamente diverse pipeline di dati:
    *   Un thread dedicato (**Network Listener**) monitora costantemente il *Subspace Channel*, processando i pacchetti in arrivo dal server senza bloccare l'interfaccia.
    *   Il thread principale gestisce l'input utente e il feedback immediato sul terminale.
*   **Gestione Input Reattivo (Reactive UI)**: Grazie all'uso della modalità `raw` di `termios`, il client intercetta i singoli tasti in tempo reale. Questo permette al giocatore di ricevere messaggi radio, avvisi del computer e aggiornamenti tattici *mentre sta scrivendo* un comando, senza che il cursore o il testo vengano interrotti o sporcati.
*   **Orchestrazione Direct Bridge**: Il client è responsabile del ciclo di vita della memoria condivisa (SHM). All'avvio, crea il segmento di memoria, inizializza i semafori di sincronizzazione e lancia il processo `trek_3dview`. Ogni volta che riceve un `PacketUpdate` dal server, il client aggiorna istantaneamente la matrice di oggetti nella SHM, notificando il visualizzatore tramite segnali POSIX.
*   **Identity & Persistence Hub**: Gestisce la procedura di login e la selezione della fazione/classe, interfacciandosi con il database persistente del server per ripristinare lo stato della missione.

In sintesi, il `trek_client` trasforma un semplice terminale testuale in un ponte di comando avanzato e fluido, tipico di un'interfaccia LCARS.

### 3. La Vista Tattica 3D (`trek_3dview`)
Il visualizzatore 3D è un motore di rendering standalone basato su **OpenGL e GLUT**, progettato per fornire una rappresentazione spaziale immersiva dell'area tattica circostante e dell'intera galassia.

*   **Fluidità Cinematica (LERP)**: Per ovviare alla natura discreta dei pacchetti di rete, il motore implementa algoritmi di **Linear Interpolation (LERP)** sia per le posizioni che per gli orientamenti (Heading/Mark). Gli oggetti non "saltano" da un punto all'altro, ma scivolano fluidamente nello spazio, mantenendo i 60 FPS anche se il server aggiorna la logica a frequenza inferiore.
*   **Rendering ad Alte Prestazioni**: Utilizza **Vertex Buffer Objects (VBO)** per gestire migliaia di stelle di sfondo e la griglia galattica, minimizzando le chiamate alla CPU e massimizzando il throughput della GPU.
*   **Cartografia Stellare (Modalità Mappa)**:
    *   Attivabile tramite il comando `map`, questa modalità trasforma la vista tattica in una mappa galattica globale 10x10x10.
    *   Ogni quadrante è rappresentato da indicatori cromatici che mostrano la densità di basi (verde), nemici (rosso), pianeti (ciano) e buchi neri (viola).
    *   La posizione attuale del giocatore è evidenziata da un **indicatore bianco pulsante**, facilitando la navigazione a lungo raggio.
*   **HUD Tattico Dinamico**: Implementa una proiezione 2D-su-3D (via `gluProject`) per ancorare etichette, barre della salute e identificativi direttamente sopra i vascelli. L'overlay include ora il monitoraggio in tempo reale dell'**Equipaggio (CREW)**, vitale per la sopravvivenza della missione.
*   **Engine degli Effetti (VFX)**:
    *   **Trail Engine**: Ogni nave lascia una scia ionica persistente che aiuta a visualizzarne il vettore di movimento.
    *   **Combat FX**: Visualizzazione in tempo reale di raggi phaser gestiti via **GLSL Shader**, torpedini fotoniche con bagliore dinamico ed esplosioni volumetriche.
    *   **Dismantle Particles**: Un sistema particellare dedicato anima lo smantellamento dei relitti nemici durante le operazioni di recupero risorse.

La Vista Tattica non è solo un elemento estetico, ma uno strumento fondamentale per il combattimento a corto raggio e la navigazione di precisione tra corpi celesti e minacce ambientali.

---

## 📡 Protocolli di Comunicazione

### Rete (Server ↔ Client): Il Subspace Channel
La comunicazione remota è affidata a un protocollo binario state-aware personalizzato, progettato per garantire coerenza e prestazioni su reti con latenza variabile.

*   **Protocollo Binario Deterministico**: A differenza dei protocolli testuali (come JSON o XML), il Subspace Channel utilizza un'architettura **Binary-Only**. Le strutture dati sono allineate tramite `pragma pack(1)` per eliminare il padding del compilatore, garantendo che ogni byte trasmesso sia un'informazione utile.
*   **State-Aware Synchronization**:
    *   Il server non si limita a inviare posizioni, ma sincronizza l'intero stato logico necessario al client (energia, scudi, inventario, messaggi del computer di bordo).
    *   Ogni pacchetto di aggiornamento (`PacketUpdate`) include un **Frame ID** globale, permettendo al client di gestire correttamente l'ordine temporale dei dati.
*   **Interest Management & Delta Optimization**:
    *   **Filtraggio Spaziale**: Il server calcola dinamicamente il set di visibilità di ogni giocatore. Riceverai dati solo sugli oggetti presenti nel tuo quadrante attuale o che influenzano i tuoi sensori a lungo raggio, riducendo drasticamente il carico di rete.
    *   **Truncated Updates**: I pacchetti che contengono liste di oggetti (come navi nemiche o detriti) vengono troncati fisicamente prima dell'invio. Se nel tuo quadrante ci sono solo 2 navi, il server invierà un pacchetto contenente solo quei 2 slot invece dell'intero array fisso, risparmiando KB preziosi ad ogni tick.
*   **Data Integrity & Stream Robustness**:
    *   Implementa un meccanismo di **Atomic Read/Write**. Le funzioni `read_all` e `write_all` garantiscono che, nonostante la natura "stream" del TCP, i pacchetti binari vengano ricostruiti solo quando sono completi e integri, prevenendo la corruzione dello stato logico durante picchi di traffico.
*   **Multiplexing del Segnale**: Il protocollo gestisce diversi tipi di pacchetti (`Login`, `Command`, `Update`, `Message`, `Query`) sullo stesso socket, agendo come un multiplexer di segnale subspaziale.

Questa implementazione permette al simulatore di scalare fluidamente, mantenendo la latenza di comando (Input Lag) minima e la coerenza della galassia assoluta per tutti i capitani connessi.

### IPC (Client ↔ Visualizzatore): Il Direct Bridge
Il link tra il ponte di comando e la vista tattica è realizzato tramite un'interfaccia di comunicazione inter-processo (IPC) basata su **POSIX Shared Memory**, progettata per eliminare la latenza di scambio dati locale.

*   **Architettura Shared-Memory**: Il `trek_client` alloca un segmento di memoria dedicato (`/st_shm_PID`) in cui risiede la struttura `GameState`. Questa struttura funge da rappresentazione speculare dello stato locale, accessibile in tempo reale sia dal client (scrittore) che dal visualizzatore (lettore).
*   **Sincronizzazione Ibrida (Mutex & Semaphores)**:
    *   **PTHREAD_PROCESS_SHARED Mutex**: La coerenza dei dati all'interno della memoria condivisa è garantita da un mutex configurato per l'uso tra processi diversi. Questo impedisce al visualizzatore di leggere dati parziali mentre il client sta aggiornando la matrice degli oggetti.
    *   **POSIX Semaphores**: Un semaforo (`sem_t data_ready`) viene utilizzato per implementare un meccanismo di notifica di tipo "Producer-Consumer". Invece di interrogare costantemente la memoria (polling), il visualizzatore rimane in uno stato di attesa efficiente finché il client non segnala la disponibilità di un nuovo frame logico.
*   **Zero-Copy Efficiency**: Poiché i dati risiedono fisicamente nella stessa area della RAM mappata in entrambi gli spazi di indirizzamento, il passaggio dei parametri telemetrici non comporta alcuna copia di memoria (memcpy) aggiuntiva, massimizzando le prestazioni del bus di sistema.
*   **Latching degli Eventi**: La SHM gestisce il "latching" di eventi rapidi (come esplosioni o scariche phaser). Il client deposita l'evento nella memoria e il visualizzatore, dopo averlo renderizzato, provvede a resettare il flag, garantendo che nessun effetto tattico venga perso o duplicato.
*   **Orchestrazione del Ciclo di Vita**: Il client funge da supervisore, gestendo la creazione (`shm_open`), il dimensionamento (`ftruncate`) e la distruzione finale della risorsa IPC, assicurando che il sistema non lasci orfani di memoria in caso di crash.

Questo approccio trasforma il visualizzatore 3D in un puro slave grafico reattivo, permettendo al motore di rendering di concentrarsi esclusivamente sulla fluidità visiva e sul calcolo geometrico.

---

## 🔍 Specifiche Tecniche

Star Trek Ultra non è solo un simulatore tattico, ma un'architettura software complessa che implementa pattern di design avanzati per la gestione dello stato distribuito e il calcolo real-time.

### 1. Il Core Logic Engine (Tick-Based Simulation)
Il server opera su un loop deterministico a **30 Tick Per Second (TPS)**. Ogni ciclo logico segue una pipeline rigorosa:
*   **Input Reconciliation**: Processamento dei comandi atomici ricevuti dai client via epoll.
*   **Predictive AI Update**: Calcolo dei vettori di movimento per gli NPC basato su matrici di inseguimento e pesi tattici (fazione, energia residua).
*   **Spatial Indexing (Grid Partitioning)**: Gli oggetti non vengono iterati linearmente ($O(N)$), ma mappati in una griglia tridimensionale 10x10x10. Questo riduce la complessità delle collisioni e dei sensori a $O(1)$ per l'area locale del giocatore.
*   **Physics Enforcement**: Applicazione del clamping galattico e risoluzione delle collisioni con corpi celesti statici.

### 2. ID-Based Object Tracking & Shared Memory Mapping
Il sistema di tracciamento degli oggetti utilizza un'architettura a **Identificativi Persistenti**:
*   **Server Side**: Ogni entità (nave, stella, pianeta) ha un ID univoco globale. Durante il tick, solo gli ID visibili al giocatore vengono serializzati.
*   **Client/Viewer Side**: Il visualizzatore mantiene un buffer locale di 200 slot. Attraverso una **Hash Map implicita**, il client associa l'ID del server a uno slot SHM. Se un ID scompare dal pacchetto di rete, il sistema di *Stale Object Purge* invalida istantaneamente lo slot locale, garantendo la coerenza visiva senza latenza di timeout.

### 3. Modello di Networking Ibrido (Binary stream su TCP)
Per ovviare alla natura "stream" del TCP, il simulatore implementa un protocollo di **Framing Binario**:
*   **Atomic Reassembly**: Le funzioni `read_all`/`write_all` garantiscono che i pacchetti non vengano mai processati parzialmente, prevenendo corruzioni di memoria nelle strutture `pragma pack(1)`.
*   **Interest Management**: Il server tronca fisicamente il payload dei pacchetti UPDATE in base al numero di oggetti effettivamente presenti nel raggio d'azione del giocatore, minimizzando l'uso del bus di rete.

### 4. Rendering Pipeline GLSL (Hardware-Accelerated Aesthetics)
Il visualizzatore 3D implementa una pipeline di ombreggiatura programmabile:
*   **Vertex Stage**: Gestione delle trasformazioni di proiezione HUD e calcolo dei vettori di luce per-pixel.
*   **Fragment Stage**: 
    *   **Aztek Shader**: Generazione procedurale di texture di scafo basate su coordinate di frammento, eliminando la necessità di asset grafici esterni.
    *   **Fresnel Rim Lighting**: Calcolo del dot product tra normale e vettore di vista per evidenziare i profili strutturali dei vascelli.
    *   **Plasma Flow Simulation**: Animazione temporale dei parametri emissivi per simulare lo scorrimento di energia nelle gondole warp.

### 5. Robustezza e Session Continuity
*   **Atomic Save-State**: Il database `galaxy.dat` viene aggiornato tramite flush periodici con lock del mutex globale, assicurando uno snapshot coerente della memoria.
*   **Emergency Rescue Protocol**: Una logica di salvataggio euristica interviene al login per risolvere stati di errore (collisioni o navi distrutte), garantendo la persistenza della carriera del giocatore anche in caso di fallimento della missione.

---

## 🕹️ Manuale Operativo dei Comandi

Di seguito la lista completa dei comandi disponibili, raggruppati per funzione.

### 🚀 Navigazione
*   `nav <H> <M> <W>`: **Warp Navigation**. Imposta rotta e velocità di curvatura.
    *   `H`: Heading (0-359).
    *   `M`: Mark (-90 a +90).
    *   `W`: Warp Factor (0-8).
*   `imp <H> <M> <S>`: **Impulse Drive**. Motori sub-luce.
    *   `S`: Speed (0.0 - 1.0).
    *   `imp 0 0 0`: Arresto motori (All Stop).
*   `cal <QX> <QY> <QZ>`: **Navigation Calculator**. Calcola H, M, W per raggiungere un quadrante.
*   `apr <ID> <DIST>`: **Approach Autopilot**. Avvicinamento automatico al bersaglio ID fino a distanza DIST.
*   `doc`: **Docking**. Attracco a una Base Stellare (richiede distanza ravvicinata).
*   `map`: **Stellar Cartography**. Attiva la visualizzazione 3D globale 10x10x10 dell'intera galassia.
    *   **Legenda Colori**: Nemici (Rosso), Basi (Verde), Pianeti (Ciano), Stelle (Giallo), Buchi Neri (Viola).
    *   **Localizzazione**: La posizione attuale della nave è indicata da un **cubo bianco pulsante**.

### 🔬 Sensori e Scanner
*   `srs`: **Short Range Sensors**. Scansione dettagliata del quadrante attuale.
*   `lrs`: **Long Range Sensors**. Scansione 3x3x3 dei quadranti circostanti.
*   `aux probe <QX> <QY> <QZ>`: Lancia una sonda a lungo raggio verso un quadrante specifico.
*   `sta`: **Status Report**. Rapporto completo stato nave, missione e monitoraggio dell'**Equipaggio**.
*   `dam`: **Damage Report**. Dettaglio danni ai sistemi.
*   `who`: Lista dei capitani attivi nella galassia.

### ⚔️ Combattimento Tattico
*   `pha <E>`: **Fire Phasers**. Spara phaser con energia E. Danno basato sulla distanza.
*   `tor <H> <M>`: **Fire Photon Torpedo**. Lancia un siluro.
    *   Se `lock` attivo: Guida automatica (basta digitare `tor`).
    *   Senza lock: Traiettoria balistica manuale.
*   `lock <ID>`: **Target Lock**. Aggancia i sistemi di puntamento sul bersaglio ID (0 per sbloccare).
*   `she <F> <R> <T> <B> <L> <RI>`: **Shield Configuration**. Distribuisce energia ai 6 scudi.
*   `clo`: **Cloaking Device**. Attiva/Disattiva occultamento (consuma energia).
*   `pow <E> <S> <W>`: **Power Distribution**. Ripartisce energia reattore (Motori, Scudi, Armi %).
*   `aux jettison`: **Eject Warp Core**. Espelle il nucleo (Manovra suicida / Ultima risorsa).
*   `xxx`: **Self-Destruct**. Autodistruzione sequenziale.

### 📦 Operazioni e Risorse
*   `bor`: **Boarding Party**. Invia squadre d'abbordaggio (Dist < 1.0). Può causare perdite di equipaggio se respinto.
*   `dis`: **Dismantle**. Smantella relitti nemici per risorse (Dist < 1.5).
*   `min`: **Mining**. Estrae risorse da un pianeta in orbita (Dist < 2.0).
*   `sco`: **Solar Scooping**. Raccoglie energia da una stella (Dist < 2.0).
*   `har`: **Harvest Antimatter**. Raccoglie antimateria da un buco nero (Dist < 2.0).
*   `con <T> <A>`: **Convert Resources**. Converte risorse in stiva.
    *   `1`: Dilithium -> Energia.
    *   `3`: Verterium -> Siluri.
*   `load <T> <A>`: **Load Cargo**. Carica dalla stiva ai sistemi.
    *   `1`: Energia.
    *   `2`: Siluri.
*   `inv`: **Inventory**. Mostra il contenuto della stiva (Cargo Bay).
*   `rep <ID>`: **Repair**. Ripara un sistema danneggiato usando risorse.
*   **Gestione Equipaggio**: 
    *   Il numero iniziale di personale dipende dalla classe della nave (es. 1012 per la Galaxy, 50 per la Defiant).
    *   **Integrità Vitale**: Se il sistema di **Supporto Vitale** (`Life Support`) scende sotto il 75%, l'equipaggio inizierà a subire perdite periodiche.
    *   **Condizione di Fallimento**: Se l'equipaggio raggiunge quota **zero**, la missione termina e la nave è considerata perduta.
*   `load <T> <A>`: **Load Cargo**. Carica dalla stiva ai sistemi.
    *   `1`: Energia.
    *   `2`: Siluri.
*   `inv`: **Inventory**. Mostra il contenuto della stiva (Cargo Bay).
*   `rep <ID>`: **Repair**. Ripara un sistema danneggiato usando risorse.

### 📡 Comunicazioni e Varie
*   `rad <MSG>`: Invia messaggio radio a tutti (Canale aperto).
*   `rad @Fac <MSG>`: Invia alla fazione (es. `@Romulan Avvistati nemici!`).
*   `rad #ID <MSG>`: Messaggio privato al giocatore ID.
*   `psy`: **Psychological Warfare**. Tenta bluff (Manovra Corbomite).
*   `axs` / `grd`: Attiva/Disattiva guide visive 3D (Assi / Griglia).
Il visualizzatore 3D non è una semplice finestra grafica, ma un'estensione del ponte di comando che fornisce dati telemetrici sovrapposti (Augmented Reality) per supportare il processo decisionale del capitano.

#### 🎯 Proiezione Tattica Integrata
Il sistema utilizza algoritmi di proiezione spaziale per ancorare le informazioni direttamente sopra le entità rilevate:
*   **Targeting Tags**: Ogni vascello viene identificato con un'etichetta dinamica. Per i giocatori mostra `Classe (Nome Capitano)`, per gli NPC `Fazione [ID]`.
*   **Health Bars**: Indicatori cromatici della salute (Verde/Giallo/Rosso) visualizzati sopra ogni nave e stazione, permettendo di valutare istantaneamente lo stato del nemico senza consultare i log testuali.
*   **Visual Latching**: Gli effetti di combattimento (phaser, esplosioni) sono sincronizzati temporalmente con la logica del server, fornendo un feedback visivo immediato all'impatto dei colpi.

#### 🧭 Bussola Tattica 3D (`axs`)
Attivando gli assi visivi (`axs`), il simulatore proietta un sistema di riferimento sferico centrato sulla propria nave:
*   **Anello del Heading**: Un disco orizzontale graduato che ruota con la nave, indicando i 360 gradi del piano galattico.
*   **Arco del Mark**: Una guida verticale che visualizza l'inclinazione zenitale (-90/+90), fondamentale per inseguimenti tridimensionali complessi.
*   **Assi Galattici**: Linee di riferimento per gli assi X (rosso), Y (verde) e Z (blu) che delimitano i confini del settore attuale.

#### 📟 Telemetria e Monitoraggio HUD
L'interfaccia a schermo (Overlay) fornisce un monitoraggio costante dei parametri vitali:
*   **Stato Reattore & Scudi**: Visualizzazione in tempo reale dell'energia disponibile e della potenza media della griglia difensiva.
*   **Coordinate di Settore**: Conversione istantanea dei dati spaziali in coordinate relative `[S1, S2, S3]` (0.0 - 10.0), speculari a quelle utilizzate nei comandi `nav` e `imp`.
*   **Rilevatore di Minacce**: Un contatore dinamico indica il numero di vascelli ostili rilevati dai sensori nel quadrante attuale.

#### 🛠️ Personalizzazione della Vista
Il comandante può configurare la propria interfaccia tramite comandi CLI rapidi:
*   `grd`: Attiva/Disattiva la **Griglia Tattica Galattica**, utile per percepire profondità e distanze.
*   `axs`: Attiva/Disattiva la **Bussola e gli Assi di Riferimento**.
*   `h` (tasto rapido): Nasconde completamente l'HUD per una visione "cinematica" del settore.
*   **Zoom & Rotazione**: Controllo totale della telecamera tattica tramite mouse o tasti `W/S` e frecce direzionali.

---

## ⚠️ Rapporto Tattico: Minacce e Ostacoli

### Capacità delle Navi NPC
Le navi controllate dal computer (Klingon, Romulani, Borg, ecc.) operano con protocolli di combattimento standardizzati:
*   **Armamento Primario**: Attualmente, le navi NPC sono equipaggiate esclusivamente con **Banchi Phaser**.
*   **Potenza di Fuoco**: I phaser nemici infliggono un danno costante di **100 unità** di energia per colpo.
*   **Portata d'Ingaggio**: Le navi ostili apriranno il fuoco automaticamente se un giocatore entra nel raggio di **6.0 unità** (Settore).
*   **Cadenza di Tiro**: Circa un colpo ogni 5 secondi.
*   **Tattica**: Le navi NPC non utilizzano siluri fotonici. La loro strategia principale consiste nell'avvicinamento diretto (Chase) o nella fuga se l'energia scende sotto livelli critici.

### Dinamiche dei Siluri Fotonici
I siluri (comando `tor`) sono armi fisiche simulate con precisione:
*   **Collisione**: I siluri devono colpire fisicamente il bersaglio (distanza < 0.5) per esplodere.
*   **Guida**: Se lanciati con un `lock` attivo, i siluri correggono la rotta del 20% per tick verso il bersaglio, permettendo di colpire navi in movimento.
*   **Ostacoli**: Corpi celesti come **Stelle, Pianeti e Buchi Neri** sono oggetti fisici solidi. Un siluro che impatta contro di essi verrà assorbito/distrutto senza colpire il bersaglio dietro di essi. Sfruttate il terreno galattico per coprirvi!
*   **Basi Stellari**: Anche le basi stellari bloccano i siluri. Attenzione al fuoco amico o incidentale.

---

## 🎖️ Registro Storico dei Comandanti

Questa sezione fornisce un riferimento ufficiale ai comandanti più celebri della galassia, utile per l'ispirazione dei giocatori o per la designazione di vascelli d'élite.

### 🔴 Comandanti delle Potenze Galattiche

#### 1. Impero Klingon
*   **Kor**: Il leggendario "Dahar Master", pioniere dei primi contatti tattici con la Federazione.
*   **Martok**: Comandante Supremo delle forze Klingon durante la Guerra del Dominio.
*   **Gowron**: Cancelliere e veterano della Guerra Civile Klingon.

#### 2. Impero Stellare Romulano
*   **Tomalak**: Comandante di vascelli di classe D'deridex e storico avversario tattico.
*   **Sela**: Comandante operativo e stratega specializzata in operazioni di infiltrazione.
*   **Donatra**: Comandante della *Valdore*, nota per la cooperazione tattica durante la crisi di Shinzon.

#### 3. Collettivo Borg
*   **Locutus**: Designazione tattica del Capitano Picard durante l'incursione a Wolf 359.
*   **La Regina**: Nodo centrale di coordinamento del Collettivo.
*   **Unimatrix 01**: Designazione di comando per vascelli di classe Diamond o Cubi Tattici.

#### 4. Unione Cardassiana
*   **Gul Dukat**: Leader delle forze di occupazione e comandante della stazione Terok Nor.
*   **Gul Madred**: Esperto in interrogatori e operazioni di intelligence.
*   **Gul Damar**: Leader della resistenza cardassiana e successore al comando supremo.

#### 5. Dominion (Jem'Hadar)
*   **Remata'Klan**: Primo dei Jem'Hadar, simbolo di disciplina e lealtà assoluta.
*   **Ikat'ika**: Comandante delle forze di terra e maestro del combattimento tattico.
*   **Karat'Ulan**: Comandante operativo nel Quadrante Gamma.

#### 6. Assemblea Tholiana
*   **Loskene**: Comandante noto per l'impiego della rete energetica Tholiana.
*   **Terev**: Ambasciatore e comandante coinvolto nelle dispute territoriali.
*   **Sthross**: Comandante di flottiglia esperto in tattiche di confinamento energetico.

#### 7. Egemonia Gorn
*   **Slar**: Comandante guerriero attivo durante le prime fasi di espansione.
*   **S'Sless**: Capitano incaricato della difesa degli avamposti di frontiera.
*   **Varn**: Comandante di flotta durante le schermaglie nel Quadrante Alfa.

#### 8. Alleanza Ferengi
*   **DaiMon Bok**: Noto per l'impiego di tecnologie di simulazione e vendette personali.
*   **DaiMon Tog**: Comandante specializzato in acquisizioni tecnologiche forzate.
*   **DaiMon Goss**: Rappresentante tattico durante i negoziati per il controllo dei Wormhole.

#### 9. Specie 8472
*   **Boothby (Impersonatore)**: Entità dedicata all'infiltrazione e allo studio del comando della Flotta.
*   **Bio-Nave Alpha**: Designazione del coordinatore tattico dei vascelli organici.
*   **Valerie Archer (Impersonatrice)**: Soggetto di infiltrazione per missioni di ricognizione profonda.

#### 10. Confederazione Breen
*   **Thot Pran**: Comandante di alto rango durante l'offensiva nel Quadrante Alfa.
*   **Thot Gor**: Leader operativo durante l'alleanza strategica con il Dominio.
*   **Thot Tarek**: Comandante delle forze d'attacco Breen.

#### 11. Hirogen
*   **Karr**: Alpha Hirogen esperto in simulazioni di caccia su vasta scala.
*   **Idrin**: Cacciatore veterano e comandante di vascelli da preda.
*   **Turanj**: Comandante specializzato nel tracciamento a lungo raggio.

---

---

### 🔵 Comandanti della Flotta Stellare per Classe di Vascello

In Star Trek Ultra, la scelta della classe di vascello non è solo estetica, ma definisce il profilo operativo del Comandante. Di seguito, i riferimenti storici e tattici per le classi disponibili:

#### 🏛️ Classe Constitution (Incrociatore Pesante)
Il simbolo dell'esplorazione della Flotta Stellare nel XXIII secolo. Vascello bilanciato, versatile e robusto.
*   **Comandanti Celebri**:
    *   **James T. Kirk**: Leggendario capitano della *USS Enterprise* (NCC-1701), noto per le sue soluzioni tattiche non convenzionali.
    *   **Christopher Pike**: Predecessore di Kirk, simbolo di integrità e coraggio.
    *   **Robert April**: Il primo comandante a portare la classe Constitution nelle profondità dello spazio inesplorato.

#### 🛰️ Classe Miranda (Vascello Multiruolo)
Affidabile e longeva, la classe Miranda è la spina dorsale del supporto tattico e scientifico.
*   **Comandanti Celebri**:
    *   **Clark Terrell**: Comandante della *USS Reliant*, tragicamente coinvolto nella crisi del Progetto Genesis.
    *   **Walker Keel**: Amico intimo di Picard e figura chiave nella scoperta dell'infiltrazione parassitaria nel Comando della Flotta.

#### ⚔️ Classe Defiant (Scorta Pesante)
Progettata specificamente per il combattimento contro i Borg. Piccola, estremamente manovrabile e pesantemente armata.
*   **Comandanti Celebri**:
    *   **Benjamin Sisko**: Il "Delegato dei Profeti", che ha guidato la *USS Defiant* durante le fasi più critiche della Guerra del Dominio.
    *   **Worf**: Maestro tattico Klingon che ha spesso comandato la Defiant in scenari di prima linea.

#### 👑 Classe Galaxy (Esploratore a Lungo Raggio)
Una "città nello spazio". Progettata per missioni decennali e diplomazia di alto livello, dotata di una potenza di fuoco massiccia.
*   **Comandanti Celebri**:
    *   **Jean-Luc Picard**: Comandante della *USS Enterprise-D*, diplomatico impareggiabile e fine stratega.
    *   **Edward Jellico**: Noto per il suo rigido protocollo militare durante la crisi Cardassiana.

#### ⚡ Classe Sovereign (Incrociatore da Battaglia)
Il pinnacolo della tecnologia bellica della Flotta Stellare alla fine del XXIV secolo. Progettata per affrontare le minacce più estreme.
*   **Comandanti Celebri**:
    *   **Jean-Luc Picard**: Al comando della *USS Enterprise-E* durante la battaglia del Settore 001 e l'insurrezione Ba'ku.
    *   **William T. Riker**: Ha guidato la Sovereign in assenza di Picard, dimostrando audacia tattica superiore.

#### 🔭 Classe Intrepid (Esploratore Scientifico)
Veloce e dotata di bio-circuiti neurali. Progettata per la ricerca e l'autonomia in quadranti remoti.
*   **Comandanti Celebri**:
    *   **Kathryn Janeway**: La leggendaria capitana che ha guidato la *USS Voyager* per 70.000 anni luce attraverso il Quadrante Delta.
    *   **Rudy Ransom**: Comandante della *USS Equinox*, la cui storia rappresenta il lato oscuro della sopravvivenza nello spazio profondo.

#### 🛡️ Altre Classi Operative
*   **Excelsior**: Guidata da **Hikaru Sulu**, pioniere dell'esplorazione dello spazio profondo.
*   **Constellation**: La classe della *USS Stargazer*, primo comando di Picard.
*   **Akira / Steamrunner**: Vascelli di punta per la difesa del territorio durante le invasioni su larga scala.
*   **Ambassador**: Un ponte tecnologico tra la classe Excelsior e la Galaxy, guidata da **Rachel Garrett**.
*   **Oberth**: Vascello scientifico leggero, essenziale per le scansioni stellari avanzate.

---

## 💾 Persistenza e Continuità
L'architettura di Star Trek Ultra è progettata per sostenere una galassia persistente e dinamica. Ogni azione, dalla scoperta di un nuovo sistema planetario al caricamento della Cargo Bay, viene preservata tramite un sistema di archiviazione binaria a basso livello.

#### 🗄️ Il Database Galattico (`galaxy.dat`)
Il file `galaxy.dat` costituisce la memoria storica del simulatore. Utilizza una struttura di **Serializzazione Diretta** della memoria RAM del server:
*   **Galaxy Master Matrix**: Una griglia tridimensionale 10x10x10 che memorizza la densità di massa e la composizione di ogni quadrante (codifica BPNBS).
*   **Registri Entità**: Un dump completo degli array globali (`npcs`, `stars_data`, `planets`, `bases`), preservando coordinate relative, livelli di energia e timer di cooldown.
*   **Integrità dei Dati**: Implementa un controllo di versione rigido (`GALAXY_VERSION`). Se il server rileva un file generato con parametri strutturali diversi (es. numero massimo di NPC variato), invalida il caricamento per prevenire corruzioni di memoria, rigenerando un universo coerente.

#### 🔄 Pipeline di Sincronizzazione (Auto-Save)
La continuità è garantita da un loop di sincronizzazione asincrona:
*   **Flush Periodico**: Ogni 60 secondi, il thread logico avvia una procedura di salvataggio.
*   **Thread Safety**: Durante l'operazione di I/O su disco, il sistema acquisisce il `game_mutex`. Questo assicura che il database salvato sia uno **snapshot atomico** e coerente dell'intero universo in quel preciso istante temporale.

#### 🆔 Identità e Restauro Profilo
Il sistema di continuità per i giocatori si basa sulla **Persistent Identity**:
*   **Riconoscimento**: Inserendo lo stesso nome capitano utilizzato in precedenza, il server interroga il database dei giocatori attivi e storici.
*   **Session Recovery**: Vengono ripristinati istantaneamente:
    *   **Coordinate Globali (`gx, gy, gz`)**: La nave riappare esattamente nell'ultimo settore visitato.
    *   **Inventario Strategico**: Livelli di Dilithio, Tritanio e Cristalli Isolineari nella Cargo Bay.
    *   **Stato Sistemi**: Danni ai motori, efficienza dei sensori e carica dei banchi phaser.
*   **Hot-Swap Connessione**: Se un capitano perde la connessione (crash del client o instabilità di rete), il server mantiene il vascello "attivo" ma immobile per un periodo di grazia, permettendo al giocatore di riprenderne il controllo immediato al rientro.

#### 🆘 Protocollo EMERGENCY RESCUE (Salvataggio d'Emergenza)
Per garantire la continuità della carriera anche nelle situazioni tattiche più disastrose, il simulatore implementa un protocollo di recupero automatico attivato durante la fase di login.

*   **Rilevamento Collisioni**: Se un giocatore tenta di ricollegarsi e il server rileva che la nave è posizionata all'interno di un corpo celeste (stella o pianeta), viene attivata la procedura di soccorso per evitare il "Death Loop".
*   **Stato Critico**: Il protocollo interviene anche se il vascello è stato precedentemente distrutto (energia a zero o perdita totale dell'equipaggio).
*   **Azioni di Soccorso**: Il Comando della Flotta Stellare esegue le seguenti operazioni automatiche:
    *   **Rilocazione Spaziale**: La nave viene teletrasportata in un settore casuale e sicuro della galassia, lontano da ogni minaccia immediata.
    *   **Ripristino Sistemi**: Riparazione d'urgenza di tutti gli 8 sottosistemi core fino all'**80% di integrità**.
    *   **Ricarica Energetica**: Fornitura di **50.000 unità** di energia di emergenza.
    *   **Equipaggio di Soccorso**: Se il personale era a zero, viene assegnato un team di soccorso minimo di **100 membri**.

Questa architettura garantisce che Star Trek Ultra non sia solo una sessione di gioco, ma una vera e propria carriera spaziale in evoluzione.

## 🛠️ Tecnologie e Requisiti di Sistema

Il progetto Star Trek Ultra è una dimostrazione di ingegneria del software orientata alle massime prestazioni, utilizzando le più recenti evoluzioni del linguaggio C e delle interfacce di sistema Linux/POSIX.

### 🏗️ Core Engine & Standard del Linguaggio
*   **C23 (ISO/IEC 9899:2024)**: Il simulatore adotta l'ultimo standard del linguaggio C. L'uso di `-std=c2x` permette l'impiego di funzionalità moderne come le costanti `true/false` native, gli attributi di standardizzazione e un supporto più rigoroso ai tipi, garantendo un codice robusto e a prova di futuro.
*   **High-Performance Event Loop (`epoll`)**: Il server abbandona il modello tradizionale `select` in favore di **Linux epoll**. Grazie alla modalità **Edge-Triggered (EPOLLET)**, il kernel notifica il server solo quando i dati sono effettivamente pronti sul socket, riducendo drasticamente le chiamate di sistema e permettendo la gestione di centinaia di client con un overhead della CPU prossimo allo zero.
*   **Binary Protocol Efficiency**: La comunicazione nel *Subspace Channel* è ottimizzata tramite `pragma pack(1)`. Questo forza il compilatore a eliminare ogni byte di padding tra i membri delle strutture, garantendo che i pacchetti binari siano i più piccoli possibili e portabili tra diverse architetture.

### 🧵 Concorrenza e Real-Time IPC
*   **Zero-Copy Shared Memory**: L'architettura *Direct Bridge* sfrutta **POSIX Shared Memory (`shm_open`)** e **mmap**. Questo permette di condividere l'intero stato galattico tra il Client e il Visualizzatore senza alcuna operazione di copia in memoria (Zero-Copy), riducendo la latenza IPC a livelli nanosecondari.
*   **Sincronizzazione Deterministica**: Il sistema utilizza un mix di **Pthread Mutex Shared** (per l'integrità atomica dei dati) e **POSIX Semaphores** (per il wake-up guidato dagli eventi). Questo assicura che il visualizzatore renderizzi i dati esattamente quando vengono ricevuti dal server, eliminando il jitter visivo.
*   **Timer ad alta precisione**: Il loop logico del server è temporizzato tramite `clock_nanosleep` su orologi mononotici del sistema, garantendo che il calcolo della fisica e del movimento sia costante e indipendente dal carico del sistema.

### 🎨 Tactical Rendering Pipeline (3D View)
La visualizzazione tattica è gestita da un motore OpenGL moderno che combina tecniche classiche e programmabili:
*   **GLSL Shader Engine**: Gli effetti di bagliore delle stelle, la distorsione del Buco Nero e le scariche phaser sono processati tramite shader scritti in **OpenGL Shading Language**. Questo sposta il carico estetico sulla GPU, liberando la CPU per la logica di rete.
*   **Vertex Buffer Objects (VBO)**: La geometria statica (stelle di fondo, griglia tattica) viene caricata nella memoria della scheda video all'avvio. Durante il rendering, viene inviato un singolo comando di disegno alla GPU, massimizzando il throughput grafico.
*   **Projective HUD Technology**: Utilizza trasformazioni di matrice inverse e `gluProject` per mappare coordinate 3D spaziali in coordinate 2D di schermo, permettendo all'interfaccia di agganciare dinamicamente i tag di identificazione sopra i vascelli in movimento.

### 📦 Dipendenze e Compilazione
Per compilare il progetto, è necessario un ambiente di sviluppo Unix-like (preferibilmente Linux) con i seguenti requisiti:
*   **Compilatore**: GCC 13+ o Clang 16+ (per supporto C23 completo).
*   **Librerie**: 
    *   `freeglut3` e `libglu1-mesa` (per il visualizzatore).
    *   `librt` e `libpthread` (per SHM e multithreading).

#### Ubuntu / Debian
```bash
sudo apt-get install build-essential freeglut3-dev libglu1-mesa-dev libglew-dev
```

#### Fedora / Red Hat / AlmaLinux / CentOS
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install freeglut-devel mesa-libGLU-devel glew-devel
```

---
*STARTREK ULTRA - 3D LOGIC ENGINE. Sviluppato con eccellenza tecnica da Nicola Taibi.*