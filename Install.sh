#!/bin/sh
# SwitchRecomp: downloads what's missing into the SwitchRecomp folder (no password, nothing goes on the system):
# SDL2 for the tool and GCC for building games. Python comes from the system (almost every distro has python3); if
# it doesn't, this asks the package manager for it (that one does ask for your password).
# Double-click it with internet, or run it in a terminal. SwitchRecomp also runs it by itself when something is
# missing.   ./Install.sh          everything that's missing
#            ./Install.sh sdl|gcc  only that
cd "$(dirname "$(readlink -f "$0")")" || exit 1
if [ ! -t 1 ] && [ -z "$SWR_IN_TERMINAL" ]; then
  # started from the file manager: open a terminal so the progress shows
  export SWR_IN_TERMINAL=1
  me="$(pwd)/Install.sh"
  for t in x-terminal-emulator gnome-terminal konsole xfce4-terminal mate-terminal lxterminal tilix kitty alacritty xterm; do
    command -v "$t" >/dev/null 2>&1 || continue
    case "$t" in
      gnome-terminal|tilix) exec "$t" -- "$me" "$@" ;;
      xfce4-terminal) exec "$t" -x "$me" "$@" ;;
      kitty) exec "$t" "$me" "$@" ;;
      *) exec "$t" -e "$me" "$@" ;;
    esac
  done
  exec "$me" "$@" >install.log 2>&1
fi

find_python() {
  for p in ./python/bin/python3 python3; do
    if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys; sys.exit(sys.version_info < (3, 8))' 2>/dev/null; then echo "$p"; return 0; fi
  done
  return 1
}
PY=$(find_python)
if [ -z "$PY" ]; then
  echo "Python 3 is not installed. Asking the package manager for it (it may ask for your password)..."
  su=""; [ "$(id -u)" != 0 ] && { command -v pkexec >/dev/null 2>&1 && su=pkexec || su=sudo; }
  if command -v apt-get >/dev/null 2>&1; then $su apt-get install -y python3
  elif command -v dnf >/dev/null 2>&1; then $su dnf install -y python3
  elif command -v pacman >/dev/null 2>&1; then $su pacman -S --noconfirm python
  elif command -v zypper >/dev/null 2>&1; then $su zypper --non-interactive install python3
  fi
  PY=$(find_python)
fi
if [ -z "$PY" ]; then
  echo "Python 3 is still missing. Install python3 with your distro's package manager and run this again."
  r=1
else
  "$PY" -u -I tool/get_deps.py "$@"; r=$?
fi
if [ -t 0 ] && [ -z "$SWR_NO_PAUSE" ]; then printf "\nPress Enter to close. "; read x; fi
exit $r
