#!/bin/bash
# STAR TREK ULTRA - TACTICAL BRIDGE INTERFACE
# Professional Startup Script

# Colori
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

clear
echo -e "${CYAN}  ____________________________________________________________________________"
echo -e ' /                                                                            \'
echo -e " | ${BLUE}  ███████╗████████╗ █████╗ ██████╗     ████████╗██████╗ ███████╗██╗  ██╗${CYAN}   |"
echo -e " | ${BLUE}  ██╔════╝╚══██╔══╝██╔══██╗██╔══██╗    ╚══██╔══╝██╔══██╗██╔════╝██║ ██╔╝${CYAN}   |"
echo -e " | ${BLUE}  ███████╗   ██║   ███████║██████╔╝       ██║   ██████╔╝█████╗  █████╔╝ ${CYAN}   |"
echo -e " | ${BLUE}  ╚════██║   ██║   ██╔══██║██╔══██╗       ██║   ██╔══██╗██╔══╝  ██╔═██╗ ${CYAN}   |"
echo -e " | ${BLUE}  ███████║   ██║   ██║  ██║██║  ██║       ██║   ██║  ██║███████╗██║  ██╗${CYAN}   |"
echo -e " | ${BLUE}  ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝       ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝${CYAN}   |"
echo -e ' |                                                                            |'
echo -e " | ${YELLOW}                    ██╗   ██╗██╗     ████████╗██████╗  █████╗${CYAN}              |"
echo -e " | ${YELLOW}                    ██║   ██║██║     ╚══██╔══╝██╔══██╗██╔══██╗${CYAN}             |"
echo -e " | ${YELLOW}                    ██║   ██║██║        ██║   ██████╔╝███████║${CYAN}             |"
echo -e " | ${YELLOW}                    ██║   ██║██║        ██║   ██╔══██╗██╔══██║${CYAN}             |"
echo -e " | ${YELLOW}                    ╚██████╔╝███████╗   ██║   ██║  ██║██║  ██║${CYAN}             |"
echo -e " | ${YELLOW}                     ╚═════╝ ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝${CYAN}             |"
echo -e ' \____________________________________________________________________________/'"${NC}"
echo ""

# Verifica eseguibile
if [ -f "./trek_client" ]; then
    TREK_BIN="./trek_client"
elif command -v trek_client > /dev/null; then
    TREK_BIN="trek_client"
else
    echo -e "${RED}[ERROR]${NC} Interfaccia tattica non trovata."
    echo -e "${YELLOW}[HINT]${NC} Esegui 'make' o installa il pacchetto RPM."
    exit 1
fi

# Gestione Sicurezza (Master Key)
if [ -z "$TREK_SUB_KEY" ]; then
    echo -e "${BLUE}[SYSTEM]${NC} Richiesta autorizzazione subspaziale..."
    echo -e "${CYAN}[AUTH]${NC} Inserisci la Master Key del Server:"
    read -sp "> " key
    echo ""
    export TREK_SUB_KEY="$key"
fi

echo -e "${GREEN}[LINK]${NC} Master Key caricata. Inizializzazione link neurale..."
echo ""

# Esecuzione del client
$TREK_BIN "$@"