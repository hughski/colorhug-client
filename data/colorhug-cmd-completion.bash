#
#  bash completion support for ColorHug console commands.
#
#  Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
#  02110-1301  USA


__colorhug_cmd_commandlist="
    boot-flash
    clear-calibration
    eeprom-erase
    eeprom-read
    eeprom-write
    flash-firmware
    get-adc-vrefs
    get-calibration
    get-calibration-map
    get-color-select
    get-dark-offsets
    get-firmware-version
    get-hardware-version
    get-integral-time
    get-leds
    get-measure-mode
    get-multiplier
    get-owner-email
    get-owner-name
    get-pcb-errata
    get-post-scale
    get-pre-scale
    get-remote-hash
    get-serial-number
    get-temperature
    list-calibration
    reset
    remote-profile-download
    remote-profile-upload
    self-test
    set-calibration
    set-calibration-ccmx
    set-calibration-map
    set-color-select
    set-dark-offsets
    set-flash-success
    set-integral-time
    set-leds
    set-measure-mode
    set-multiplier
    set-owner-email
    set-owner-name
    set-pcb-errata
    set-post-scale
    set-pre-scale
    set-remote-hash
    set-serial-number
    sram-read
    sram-write
    take-reading-array
    take-reading-raw
    take-readings
    take-readings-xyz
    write-eeprom
    "

__colorhug_cmdcomp ()
{
	local all c s=$'\n' IFS=' '$'\t'$'\n'
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ $# -gt 2 ]; then
		cur="$3"
	fi
	for c in $1; do
		case "$c$4" in
		*.)    all="$all$c$4$s" ;;
		*)     all="$all$c$4 $s" ;;
		esac
	done
	IFS=$s
	COMPREPLY=($(compgen -P "$2" -W "$all" -- "$cur"))
	return
}

_colorhug_cmd ()
{
	local i c=1 command

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		--help|--verbose|-v|-h|-?) ;;
		*) command="$i"; break ;;
		esac
		c=$((++c))
	done

    if [ $c -eq $COMP_CWORD -a -z "$command" ]; then
		case "${COMP_WORDS[COMP_CWORD]}" in
		--*=*) COMPREPLY=() ;;
		--*)   __colorhug_cmdcomp "
			--verbose
			--help
			"
			;;
        -*) __colorhug_cmdcomp "
            -v
            -h
            -?
            "
            ;;
		*)     __colorhug_cmdcomp "$__colorhug_cmd_commandlist" ;;
		esac
		return
	fi

	case "$command" in
	*)           COMPREPLY=() ;;
	esac
}

complete -o default -o nospace -F _colorhug_cmd colorhug-cmd
