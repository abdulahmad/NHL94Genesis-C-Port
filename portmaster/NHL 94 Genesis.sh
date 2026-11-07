#!/bin/bash
# PortMaster launcher for NHL '94 Genesis (Overbar / PulseMan host).
# User-supplied dump only. Never ships a ROM.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/nhl94g"
CONFDIR="$GAMEDIR/conf"
BIN="nhl94g.${DEVICE_ARCH}"

mkdir -p "$CONFDIR" "$GAMEDIR/roms" "$GAMEDIR/states"
cd "$GAMEDIR" || exit 1

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export OVERBAR_HANDHELD=1
export PORTMASTER=1

if [ ! -f "$GAMEDIR/roms/nhl94.gen" ]; then
  pm_message "Copy your NHL '94 Genesis dump to roms/nhl94.gen (1 MB US/Europe)."
  pm_finish
  exit 1
fi

if [ ! -x "$GAMEDIR/$BIN" ]; then
  pm_message "Missing $BIN. Cross-compile the Genesis C-port for aarch64 and copy it here."
  pm_finish
  exit 1
fi

# gptokeyb: quit combo (Select+Start) plus L1 rewind / R1 fast-forward.
# Face buttons stay with SDL GameController.
$GPTOKEYB "$BIN" -c "$GAMEDIR/nhl94g.gptk" &
pm_platform_helper "$GAMEDIR/$BIN"
./"$BIN" --rom "$GAMEDIR/roms/nhl94.gen" --sram "$CONFDIR/nhl94.srm"
pm_finish
