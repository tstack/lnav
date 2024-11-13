#! /bin/bash

# Unsets the following so it does not show up in the term title
unset SSH_CONNECTION
unset XDG_CONFIG_HOME

lnav_test="${top_builddir}/src/lnav-test"
export lnav_test

export HOME="./tui-home"
rm -rf ./tui-home
mkdir -p $HOME

${lnav_test} -nN -c ':config /ui/theme monocai'

for fn in ${srcdir}/tui-captures/*; do
    base_fn=`basename $fn`
    run_test ./scripty -nX -e $fn -- ${lnav_test} -N < /dev/null

    case "$base_fn" in
    tui_echo.0)
      on_error_log "Skipping $fn"
      ;;
    *)
      on_error_log "TUI test ${fn} does not work?"
      ;;
    esac
done

run_test ./scripty -nX -e ${srcdir}/xpath_tui.0 -- \
    ${lnav_test} -I ${test_dir} \
        -c ':goto 2' \
        ${srcdir}/logfile_xml_msg.0

on_error_log "xpath() fields are not working?"
