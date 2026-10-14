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
    run_test ./scripty -nX -e $fn -- ${lnav_test} -d /tmp/tui.err -N < /dev/null

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

# These checks exercise startup and session restoration without comparing transient
# screen redraws. scripty still checks that the child exits successfully.
run_test ./scripty -nrX -e ${srcdir}/goto_tui.0 -- \
    ${lnav_test} -d ./tui-home/goto-default.err ${srcdir}/logfile_partitions.0
on_error_fail_with "Could not open the log at its default position"
run_test grep -E 'log view actual top/selection: [0-9]+/81$' ./tui-home/goto-default.err
on_error_fail_with "Default log selection should be the last line"

run_test ./scripty -nrX -e ${srcdir}/goto_tui.0 -- \
    ${lnav_test} -d ./tui-home/goto-explicit.err -c ':goto 2' ${srcdir}/logfile_partitions.0
on_error_fail_with "Could not run the initial goto command"
run_test grep -E 'log view actual top/selection: [0-9]+/2$' ./tui-home/goto-explicit.err
on_error_fail_with "Initial goto selection was lost when restoring the session"

run_test ./scripty -nrX -e ${srcdir}/goto_tui.0 -- \
    ${lnav_test} -d ./tui-home/goto-restored.err ${srcdir}/logfile_partitions.0
on_error_fail_with "Could not reopen the log"
run_test grep -E 'log view actual top/selection: [0-9]+/2$' ./tui-home/goto-restored.err
on_error_fail_with "Saved log selection was not restored"

run_test ./scripty -nrX -e ${srcdir}/goto_tui.0 -- \
    ${lnav_test} -d ./tui-home/goto-override.err -c ':goto 5' ${srcdir}/logfile_partitions.0
on_error_fail_with "Could not override the saved log selection"
run_test grep -E 'log view actual top/selection: [0-9]+/5$' ./tui-home/goto-override.err
on_error_fail_with "Initial goto should override the saved selection"
