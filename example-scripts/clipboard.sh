#!/bin/sh
# Wrapper for various clipboard I/O on Linux desktop environments and Windows emulations thereof

if [ -z "$STDIN_COPY_COMMAND" ] || [ -z "$STDOUT_PASTE_COMMAND" ]
then
	if [ -n "$WAYLAND_DISPLAY" ]
	then
		STDIN_COPY_COMMAND="wl-copy --foreground --type text/plain"
		STDOUT_PASTE_COMMAND="wl-paste --no-newline"
	elif [ -n "$DISPLAY" ]
	then
		if command -v xclip
		then
			STDIN_COPY_COMMAND="xclip -quiet -i -selection clipboard"
			STDOUT_PASTE_COMMAND="xclip -o -selection clipboard"
		elif command -v xsel
		then
			STDIN_COPY_COMMAND="xsel --nodetach -i --clipboard"
			STDOUT_PASTE_COMMAND="xsel -o --clipboard"
		fi
	elif command -v lemonade
	then
		STDIN_COPY_COMMAND="lemonade copy"
		STDOUT_PASTE_COMMAND="lemonade paste"
	elif command -v doitclient
	then
		STDIN_COPY_COMMAND="doitclient wclip"
		STDOUT_PASTE_COMMAND="doitclient wclip -r"
	elif command -v win32yank.exe
	then
		STDIN_COPY_COMMAND="win32yank.exe -i --crlf"
		STDOUT_PASTE_COMMAND="win32yank.exe -o --lf"
	elif command -v clip.exe
	then
		STDIN_COPY_COMMAND="clip.exe"
		STDOUT_PASTE_COMMAND=":"
	elif [ -n "$TMUX" ]
	then
		STDIN_COPY_COMMAND="tmux load-buffer -"
		STDOUT_PASTE_COMMAND="tmux save-buffer -"
	else
		echo 'No clipboard command' >&2
		exit 10
	fi > /dev/null
fi

case $1 in
	copy) exec $STDIN_COPY_COMMAND > /dev/null 2>/dev/null ;;
	paste) exec $STDOUT_PASTE_COMMAND < /dev/null 2>/dev/null ;;
	"") # Try to guess intention
		if ! [ -t 0 ] # stdin is piped
		then
			exec $STDIN_COPY_COMMAND > /dev/null 2>/dev/null
		elif ! [ -t 1 ] # stdout is piped
		then
			exec $STDOUT_PASTE_COMMAND < /dev/null 2>/dev/null
		else
			export STDIN_COPY_COMMAND STDOUT_PASTE_COMMAND
		fi
		;;
	*) cat << EOF
Usage:
	clipboard copy
	clipboard paste
	. clipboard
EOF
	exit 10
	;;
esac
