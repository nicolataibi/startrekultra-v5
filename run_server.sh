#!/bin/bash
# STAR TREK ULTRA - GALAXY SERVER BOOTLOADER
# Professional Startup Script

# Colori
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

clear
echo -e "${RED}  ____________________________________________________________________________"
echo -e ' /                                                                            \'
echo -e " | ${CYAN}  ███████╗████████╗ █████╗ ██████╗     ████████╗██████╗ ███████╗██╗  ██╗${RED}   |"
echo -e " | ${CYAN}  ██╔════╝╚══██╔══╝██╔══██╗██╔══██╗    ╚══██╔══╝██╔══██╗██╔════╝██║ ██╔╝${RED}   |"
echo -e " | ${CYAN}  ███████╗   ██║   ███████║██████╔╝       ██║   ██████╔╝█████╗  █████╔╝ ${RED}   |"
echo -e " | ${CYAN}  ╚════██║   ██║   ██╔══██║██╔══██╗       ██║   ██╔══██╗██╔══╝  ██╔═██╗ ${RED}   |"
echo -e " | ${CYAN}  ███████║   ██║   ██║  ██║██║  ██║       ██║   ██║  ██║███████╗██║  ██╗${RED}   |"
echo -e " | ${CYAN}  ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝       ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝${RED}   |"
echo -e ' |                                                                            |'
echo -e " | ${RED}                    ---  G A L A X Y   S E R V E R  ---${RED}                    |"
echo -e ' \____________________________________________________________________________/'"${NC}"
echo ""

# Verifica eseguibile
if [ ! -f "./trek_server" ]; then
    echo -e "${RED}[ERROR]${NC} Eseguibile 'trek_server' non trovato."
    echo -e "${YELLOW}[HINT]${NC} Esegui 'make' per compilare il progetto."
    exit 1
fi

# Gestione Sicurezza (Master Key)
if [ -z "$TREK_SUB_KEY" ]; then
    echo -e "${YELLOW}[SECURITY]${NC} Inizializzazione protocollo Subspace..."
    echo -e "${CYAN}[AUTH]${NC} Inserisci la Master Key per questo settore:"
    read -sp "> " key
    echo ""
    export TREK_SUB_KEY="$key"
fi

echo -e "${GREEN}[SYSTEM]${NC} Master Key validata. Avvio sequenza di boot..."
echo -e "${BLUE}[INFO]${NC} Server in ascolto sulla porta 5000 (TCP/Binary)"
echo ""

# Esecuzione del server
./trek_server "$@"
