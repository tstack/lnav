
.. _adjust_log_time:

:adjust-log-time *timestamp*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Change the timestamps of the focused file to be relative to the given date

  **Parameters**
    * **timestamp\*** --- The new timestamp for the focused line in the view

  **Examples**
    To set the focused timestamp to a given date:

    .. code-block::  lnav

      :adjust-log-time 2017-01-02T05:33:00

    To set the focused timestamp back an hour:

    .. code-block::  lnav

      :adjust-log-time -1h


----


.. _alt_msg:

:alt-msg *msg*
^^^^^^^^^^^^^^

  Display a message in the alternate command position

  **Parameters**
    * **msg\*** --- The message to display

  **Examples**
    To display 'Press t to switch to the text view' on the bottom right:

    .. code-block::  lnav

      :alt-msg Press t to switch to the text view

  **See Also**
    :ref:`cd`, :ref:`echo`, :ref:`eval`, :ref:`export_session_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _annotate:

:annotate
^^^^^^^^^

  Analyze the focused log message and attach annotations

  **See Also**
    :ref:`comment`, :ref:`tag`

----


.. _append_to:

:append-to *path*
^^^^^^^^^^^^^^^^^

  Append marked lines in the current view to the given file

  **Parameters**
    * **path\*** --- The path to the file to append to

  **Examples**
    To append marked lines to the file /tmp/interesting-lines.txt:

    .. code-block::  lnav

      :append-to /tmp/interesting-lines.txt

  **See Also**
    :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _cd:

:cd *dir*
^^^^^^^^^

  Change the current directory

  **Parameters**
    * **dir\*** --- The new current directory

  **See Also**
    :ref:`alt_msg`, :ref:`echo`, :ref:`eval`, :ref:`export_session_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _clear_adjusted_log_time:

:clear-adjusted-log-time
^^^^^^^^^^^^^^^^^^^^^^^^

  Clear the adjusted time for the focused line in the view


----


.. _clear_comment:

:clear-comment
^^^^^^^^^^^^^^

  Clear the comment attached to the focused log line

  **See Also**
    :ref:`annotate`, :ref:`comment`, :ref:`tag`

----


.. _clear_file_timezone:

:clear-file-timezone *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Clear the timezone setting for the focused file or the given glob pattern.

  **Parameters**
    * **pattern\*** --- The glob pattern to match against files that should no longer use this timezone

  **See Also**
    :ref:`set_file_timezone`

----


.. _clear_filter_expr:

:clear-filter-expr
^^^^^^^^^^^^^^^^^^

  Clear the filter expression

  **See Also**
    :ref:`filter_expr`, :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _clear_highlight:

:clear-highlight *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Remove a previously set highlight regular expression

  **Parameters**
    * **pattern\*** --- The regular expression previously used with :highlight

  **Examples**
    To clear the highlight with the pattern 'foobar':

    .. code-block::  lnav

      :clear-highlight foobar

  **See Also**
    :ref:`enable_word_wrap`, :ref:`hide_fields`, :ref:`highlight`, :ref:`set_text_view_mode`

----


.. _clear_mark_expr:

:clear-mark-expr
^^^^^^^^^^^^^^^^

  Clear the mark expression

  **See Also**
    :ref:`hide_unmarked_lines`, :ref:`mark_expr`, :ref:`mark`, :ref:`next_mark`, :ref:`prev_mark`

----


.. _clear_partition:

:clear-partition
^^^^^^^^^^^^^^^^

  Clear the partition the focused line is a part of


----


.. _close:

:close *path*
^^^^^^^^^^^^^

  Close the given file(s) or the focused file in the view

  **Parameters**
    * **path** --- A path or glob pattern that specifies the files to close

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _comment:

:comment *text*
^^^^^^^^^^^^^^^

  Attach a comment to the focused log line.  The comment will be displayed right below the log message it is associated with. The comment can contain Markdown directives for styling and linking.

  **Parameters**
    * **text\*** --- The comment text

  **Examples**
    To add the comment 'This is where it all went wrong' to the focused line:

    .. code-block::  lnav

      :comment This is where it all went wrong

  **See Also**
    :ref:`annotate`, :ref:`clear_comment`, :ref:`tag`

----


.. _config:

:config *option* *\[value\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Read or write a configuration option

  **Parameters**
    * **option\*** --- The path to the option to read or write
    * **value** --- The value to write.  If not given, the current value is returned

  **Examples**
    To read the configuration of the '/ui/clock-format' option:

    .. code-block::  lnav

      :config /ui/clock-format

    To set the '/ui/dim-text' option to 'false':

    .. code-block::  lnav

      :config /ui/dim-text false

  **See Also**
    :ref:`reset_config`

----


.. _convert_time_to:

:convert-time-to *zone*
^^^^^^^^^^^^^^^^^^^^^^^

  Convert the focused timestamp to the given timezone

  **Parameters**
    * **zone\*** --- The timezone name


----


.. _create_logline_table:

:create-logline-table *table-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Create an SQL table using the focused line of the log view as a template

  **Parameters**
    * **table-name\*** --- The name for the new table

  **Examples**
    To create a logline-style table named 'task_durations':

    .. code-block::  lnav

      :create-logline-table task_durations

  **See Also**
    :ref:`create_search_table`, :ref:`create_search_table`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_view_to`

----


.. _create_search_table:

:create-search-table *table-name* *\[pattern\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Create an SQL table based on a regex search

  **Parameters**
    * **table-name\*** --- The name of the table to create
    * **pattern** --- The regular expression used to capture the table columns.  If not given, the current search pattern is used.

  **Examples**
    To create a table named 'task_durations' that matches log messages with the pattern 'duration=(?<duration>\d+)':

    .. code-block::  lnav

      :create-search-table task_durations duration=(?<duration>\d+)

  **See Also**
    :ref:`create_logline_table`, :ref:`create_logline_table`, :ref:`delete_search_table`, :ref:`delete_search_table`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_view_to`

----


.. _current_time:

:current-time
^^^^^^^^^^^^^

  Print the current time in human-readable form and seconds since the epoch


----


.. _delete_filter:

:delete-filter *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^

  Delete the filter created with ':filter-in' or ':filter-out'

  **Parameters**
    * **pattern\*** --- The regular expression to match

  **Examples**
    To delete the filter with the pattern 'last message repeated':

    .. code-block::  lnav

      :delete-filter last message repeated

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _delete_logline_table:

:delete-logline-table *table-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Delete a table created with create-logline-table

  **Parameters**
    * **table-name\*** --- The name of the table to delete

  **Examples**
    To delete the logline-style table named 'task_durations':

    .. code-block::  lnav

      :delete-logline-table task_durations

  **See Also**
    :ref:`create_logline_table`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`create_search_table`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_view_to`

----


.. _delete_search_table:

:delete-search-table *table-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Delete a search table

  **Parameters**
    * **table-name** --- The name of the table to delete

  **Examples**
    To delete the search table named 'task_durations':

    .. code-block::  lnav

      :delete-search-table task_durations

  **See Also**
    :ref:`create_logline_table`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`create_search_table`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_view_to`

----


.. _delete_tags:

:delete-tags *tag*
^^^^^^^^^^^^^^^^^^

  Remove the given tags from all log lines

  **Parameters**
    * **tag** --- The tags to delete

  **Examples**
    To remove the tags '#BUG123' and '#needs-review' from all log lines:

    .. code-block::  lnav

      :delete-tags #BUG123 #needs-review

  **See Also**
    :ref:`annotate`, :ref:`comment`, :ref:`tag`

----


.. _disable_filter:

:disable-filter *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Disable a filter created with filter-in/filter-out

  **Parameters**
    * **pattern\*** --- The regular expression used in the filter command

  **Examples**
    To disable the filter with the pattern 'last message repeated':

    .. code-block::  lnav

      :disable-filter last message repeated

  **See Also**
    :ref:`enable_filter`, :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _disable_word_wrap:

:disable-word-wrap
^^^^^^^^^^^^^^^^^^

  Disable word-wrapping for the current view

  **See Also**
    :ref:`enable_word_wrap`, :ref:`hide_fields`, :ref:`highlight`, :ref:`set_text_view_mode`

----


.. _echo:

:echo *\[-n\]* *msg*
^^^^^^^^^^^^^^^^^^^^

  Echo the given message to the screen or, if :redirect-to has been called, to output file specified in the redirect.  Variable substitution is performed on the message.  Use a backslash to escape any special characters, like '$'

  **Parameters**
    * **-n** --- Do not print a line-feed at the end of the output
    * **msg\*** --- The message to display

  **Examples**
    To output 'Hello, World!':

    .. code-block::  lnav

      :echo Hello, World!

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _enable_filter:

:enable-filter *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^

  Enable a previously created and disabled filter

  **Parameters**
    * **pattern\*** --- The regular expression used in the filter command

  **Examples**
    To enable the disabled filter with the pattern 'last message repeated':

    .. code-block::  lnav

      :enable-filter last message repeated

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _enable_word_wrap:

:enable-word-wrap
^^^^^^^^^^^^^^^^^

  Enable word-wrapping for the current view

  **See Also**
    :ref:`disable_word_wrap`, :ref:`hide_fields`, :ref:`highlight`, :ref:`set_text_view_mode`

----


.. _eval:

:eval *command*
^^^^^^^^^^^^^^^

  Evaluate the given command/query after doing environment variable substitution

  **Parameters**
    * **command\*** --- The command or query to perform substitution on.

  **Examples**
    To substitute the table name from a variable:

    .. code-block::  lnav

      :eval ;SELECT * FROM ${table}

  **See Also**
    :ref:`alt_msg`, :ref:`cd`, :ref:`echo`, :ref:`export_session_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _export_session_to:

:export-session-to *path*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Export the current lnav state to an executable lnav script file that contains the commands needed to restore the current session

  **Parameters**
    * **path\*** --- The path to the file to write

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _filter_expr:

:filter-expr *expr*
^^^^^^^^^^^^^^^^^^^

  Set the filter expression

  **Parameters**
    * **expr\*** --- The SQL expression to evaluate for each log message.  The message values can be accessed using column names prefixed with a colon

  **Examples**
    To set a filter expression that matched syslog messages from 'syslogd':

    .. code-block::  lnav

      :filter-expr :log_procname = 'syslogd'

    To set a filter expression that matches log messages where 'id' is followed by a number and contains the string 'foo':

    .. code-block::  lnav

      :filter-expr :log_body REGEXP 'id\d+' AND :log_body REGEXP 'foo'

  **See Also**
    :ref:`clear_filter_expr`, :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _filter_in:

:filter-in *pattern*
^^^^^^^^^^^^^^^^^^^^

  Only show lines that match the given regular expression in the current view

  **Parameters**
    * **pattern\*** --- The regular expression to match

  **Examples**
    To filter out log messages that do not have the string 'dhclient':

    .. code-block::  lnav

      :filter-in dhclient

  **See Also**
    :ref:`delete_filter`, :ref:`disable_filter`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _filter_out:

:filter-out *pattern*
^^^^^^^^^^^^^^^^^^^^^

  Remove lines that match the given regular expression in the current view

  **Parameters**
    * **pattern\*** --- The regular expression to match

  **Examples**
    To filter out log messages that contain the string 'last message repeated':

    .. code-block::  lnav

      :filter-out last message repeated

  **See Also**
    :ref:`delete_filter`, :ref:`disable_filter`, :ref:`filter_in`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _goto:

:goto *line#|N%|timestamp|#anchor*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Go to the given location in the top view

  **Parameters**
    * **line#|N%|timestamp|#anchor\*** --- A line number, percent into the file, timestamp, or an anchor in a text file

  **Examples**
    To go to line 22:

    .. code-block::  lnav

      :goto 22

    To go to the line 75% of the way into the view:

    .. code-block::  lnav

      :goto 75%

    To go to the first message on the first day of 2017:

    .. code-block::  lnav

      :goto 2017-01-01

    To go to the Screenshots section:

    .. code-block::  lnav

      :goto #screenshots

  **See Also**
    :ref:`next_location`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _help:

:help
^^^^^

  Open the help text view


----


.. _hide_fields:

:hide-fields *field-name*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Hide log message fields by replacing them with an ellipsis

  **Parameters**
    * **field-name** --- The name of the field to hide in the format for the focused log line.  A qualified name can be used where the field name is prefixed by the format name and a dot to hide any field.

  **Examples**
    To hide the log_procname fields in all formats:

    .. code-block::  lnav

      :hide-fields log_procname

    To hide only the log_procname field in the syslog format:

    .. code-block::  lnav

      :hide-fields syslog_log.log_procname

  **See Also**
    :ref:`enable_word_wrap`, :ref:`highlight`, :ref:`set_text_view_mode`, :ref:`show_fields`

----


.. _hide_file:

:hide-file *path*
^^^^^^^^^^^^^^^^^

  Hide the given file(s) and skip indexing until it is shown again.  If no path is given, the current file in the view is hidden

  **Parameters**
    * **path** --- A path or glob pattern that specifies the files to hide


----


.. _hide_lines_after:

:hide-lines-after *date*
^^^^^^^^^^^^^^^^^^^^^^^^

  Hide lines that come after the given date

  **Parameters**
    * **date\*** --- An absolute or relative date

  **Examples**
    To hide the lines after the focused line in the view:

    .. code-block::  lnav

      :hide-lines-after here

    To hide the lines after 6 AM today:

    .. code-block::  lnav

      :hide-lines-after 6am

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`show_lines_before_and_after`, :ref:`toggle_filtering`

----


.. _hide_lines_before:

:hide-lines-before *date*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Hide lines that come before the given date

  **Parameters**
    * **date\*** --- An absolute or relative date

  **Examples**
    To hide the lines before the focused line in the view:

    .. code-block::  lnav

      :hide-lines-before here

    To hide the log messages before 6 AM today:

    .. code-block::  lnav

      :hide-lines-before 6am

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_unmarked_lines`, :ref:`show_lines_before_and_after`, :ref:`toggle_filtering`

----


.. _hide_unmarked_lines:

:hide-unmarked-lines
^^^^^^^^^^^^^^^^^^^^

  Hide lines that have not been bookmarked

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`mark`, :ref:`next_mark`, :ref:`prev_mark`, :ref:`toggle_filtering`

----


.. _highlight:

:highlight *pattern*
^^^^^^^^^^^^^^^^^^^^

  Add coloring to log messages fragments that match the given regular expression

  **Parameters**
    * **pattern\*** --- The regular expression to match

  **Examples**
    To highlight numbers with three or more digits:

    .. code-block::  lnav

      :highlight \d{3,}

  **See Also**
    :ref:`clear_highlight`, :ref:`enable_word_wrap`, :ref:`hide_fields`, :ref:`set_text_view_mode`

----


.. _load_session:

:load-session
^^^^^^^^^^^^^

  Load the latest session state


----


.. _mark:

:mark
^^^^^

  Toggle the bookmark state for the focused line in the current view

  **See Also**
    :ref:`hide_unmarked_lines`, :ref:`next_mark`, :ref:`prev_mark`

----


.. _mark_expr:

:mark-expr *expr*
^^^^^^^^^^^^^^^^^

  Set the bookmark expression

  **Parameters**
    * **expr\*** --- The SQL expression to evaluate for each log message.  The message values can be accessed using column names prefixed with a colon

  **Examples**
    To mark lines from 'dhclient' that mention 'eth0':

    .. code-block::  lnav

      :mark-expr :log_procname = 'dhclient' AND :log_body LIKE '%eth0%'

  **See Also**
    :ref:`clear_mark_expr`, :ref:`hide_unmarked_lines`, :ref:`mark`, :ref:`next_mark`, :ref:`prev_mark`

----


.. _next_location:

:next-location
^^^^^^^^^^^^^^

  Move to the next position in the location history

  **See Also**
    :ref:`goto`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _next_mark:

:next-mark *type*
^^^^^^^^^^^^^^^^^

  Move to the next bookmark of the given type in the current view

  **Parameters**
    * **type** --- The type of bookmark -- error, warning, search, user, file, meta

  **Examples**
    To go to the next error:

    .. code-block::  lnav

      :next-mark error

  **See Also**
    :ref:`goto`, :ref:`hide_unmarked_lines`, :ref:`mark`, :ref:`next_location`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`prev_mark`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _next_section:

:next-section
^^^^^^^^^^^^^

  Move to the next section in the document

  **See Also**
    :ref:`goto`, :ref:`next_location`, :ref:`next_mark`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _open:

:open *path*
^^^^^^^^^^^^

  Open the given file(s) in lnav.  Opening files on machines accessible via SSH can be done using the syntax: [user@]host:/path/to/logs

  **Parameters**
    * **path** --- The path to the file to open

  **Examples**
    To open the file '/path/to/file':

    .. code-block::  lnav

      :open /path/to/file

    To open the remote file '/var/log/syslog.log':

    .. code-block::  lnav

      :open dean@host1.example.com:/var/log/syslog.log

  **See Also**
    :ref:`append_to`, :ref:`close`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _partition_name:

:partition-name *name*
^^^^^^^^^^^^^^^^^^^^^^

  Mark the focused line in the log view as the start of a new partition with the given name

  **Parameters**
    * **name\*** --- The name for the new partition

  **Examples**
    To mark the focused line as the start of the partition named 'boot #1':

    .. code-block::  lnav

      :partition-name boot #1


----


.. _pipe_line_to:

:pipe-line-to *shell-cmd*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Pipe the focused line to the given shell command.  Any fields defined by the format will be set as environment variables.

  **Parameters**
    * **shell-cmd\*** --- The shell command-line to execute

  **Examples**
    To write the focused line to 'sed' for processing:

    .. code-block::  lnav

      :pipe-line-to sed -e 's/foo/bar/g'

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _pipe_to:

:pipe-to *shell-cmd*
^^^^^^^^^^^^^^^^^^^^

  Pipe the marked lines to the given shell command

  **Parameters**
    * **shell-cmd\*** --- The shell command-line to execute

  **Examples**
    To write marked lines to 'sed' for processing:

    .. code-block::  lnav

      :pipe-to sed -e s/foo/bar/g

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _prev_location:

:prev-location
^^^^^^^^^^^^^^

  Move to the previous position in the location history

  **See Also**
    :ref:`goto`, :ref:`next_location`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_mark`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _prev_mark:

:prev-mark *type*
^^^^^^^^^^^^^^^^^

  Move to the previous bookmark of the given type in the current view

  **Parameters**
    * **type** --- The type of bookmark -- error, warning, search, user, file, meta

  **Examples**
    To go to the previous error:

    .. code-block::  lnav

      :prev-mark error

  **See Also**
    :ref:`goto`, :ref:`hide_unmarked_lines`, :ref:`mark`, :ref:`next_location`, :ref:`next_mark`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_section`, :ref:`relative_goto`

----


.. _prev_section:

:prev-section
^^^^^^^^^^^^^

  Move to the previous section in the document

  **See Also**
    :ref:`goto`, :ref:`next_location`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`relative_goto`

----


.. _prompt:

:prompt *type* *\[--alt\]* *\[prompt\]* *\[initial-value\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Open the given prompt

  **Parameters**
    * **type\*** --- The type of prompt
    * **--alt** --- Perform the alternate action for this prompt by default
    * **prompt** --- The prompt to display
    * **initial-value** --- The initial value to fill in for the prompt

  **Examples**
    To open the command prompt with 'filter-in' already filled in:

    .. code-block::  lnav

      :prompt command : 'filter-in '

    To ask the user a question:

    .. code-block::  lnav

      :prompt user 'Are you sure? '


----


.. _quit:

:quit
^^^^^

  Quit lnav


----


.. _rebuild:

:rebuild
^^^^^^^^

  Forcefully rebuild file indexes

  **See Also**
    :ref:`alt_msg`, :ref:`cd`, :ref:`echo`, :ref:`eval`, :ref:`export_session_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _redirect_to:

:redirect-to *\[path\]*
^^^^^^^^^^^^^^^^^^^^^^^

  Redirect the output of commands that write to stdout to the given file

  **Parameters**
    * **path** --- The path to the file to write.  If not specified, the current redirect will be cleared

  **Examples**
    To write the output of lnav commands to the file /tmp/script-output.txt:

    .. code-block::  lnav

      :redirect-to /tmp/script-output.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _redraw:

:redraw
^^^^^^^

  Do a full redraw of the screen


----


.. _relative_goto:

:relative-goto *line-count|N%*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Move the current view up or down by the given amount

  **Parameters**
    * **line-count|N%\*** --- The amount to move the view by.

  **Examples**
    To move 22 lines down in the view:

    .. code-block::  lnav

      :relative-goto +22

    To move 10 percent back in the view:

    .. code-block::  lnav

      :relative-goto -10%

  **See Also**
    :ref:`goto`, :ref:`next_location`, :ref:`next_mark`, :ref:`next_section`, :ref:`prev_location`, :ref:`prev_mark`, :ref:`prev_section`

----


.. _reset_config:

:reset-config *option*
^^^^^^^^^^^^^^^^^^^^^^

  Reset the configuration option to its default value

  **Parameters**
    * **option\*** --- The path to the option to reset

  **Examples**
    To reset the '/ui/clock-format' option back to the builtin default:

    .. code-block::  lnav

      :reset-config /ui/clock-format

  **See Also**
    :ref:`config`

----


.. _reset_session:

:reset-session
^^^^^^^^^^^^^^

  Reset the session state, clearing all filters, highlights, and bookmarks


----


.. _save_session:

:save-session
^^^^^^^^^^^^^

  Save the current state as a session


----


.. _session:

:session *lnav-command*
^^^^^^^^^^^^^^^^^^^^^^^

  Add the given command to the session file (~/.lnav/session)

  **Parameters**
    * **lnav-command\*** --- The lnav command to save.

  **Examples**
    To add the command ':highlight foobar' to the session file:

    .. code-block::  lnav

      :session :highlight foobar


----


.. _set_file_timezone:

:set-file-timezone *zone* *\[pattern\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Set the timezone to use for log messages that do not include a timezone.  The timezone is applied to the focused file or the given glob pattern.

  **Parameters**
    * **zone\*** --- The timezone name
    * **pattern** --- The glob pattern to match against files that should use this timezone

  **See Also**
    :ref:`clear_file_timezone`

----


.. _set_min_log_level:

:set-min-log-level *log-level*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Set the minimum log level to display in the log view

  **Parameters**
    * **log-level\*** --- The new minimum log level

  **Examples**
    To set the minimum log level displayed to error:

    .. code-block::  lnav

      :set-min-log-level error


----


.. _set_text_view_mode:

:set-text-view-mode *mode*
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Set the display mode for text files

  **Parameters**
    * **mode\*** --- The display mode

  **See Also**
    :ref:`enable_word_wrap`, :ref:`hide_fields`, :ref:`highlight`

----


.. _sh:

:sh *--name=<name>* *cmdline*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Execute the given command-line and display the captured output

  **Parameters**
    * **--name=<name>\*** --- The name to give to the captured output
    * **cmdline\*** --- The command-line to execute.

  **See Also**
    :ref:`alt_msg`, :ref:`cd`, :ref:`echo`, :ref:`eval`, :ref:`export_session_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _show_fields:

:show-fields *field-name*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Show log message fields that were previously hidden

  **Parameters**
    * **field-name** --- The name of the field to show

  **Examples**
    To show all the log_procname fields in all formats:

    .. code-block::  lnav

      :show-fields log_procname

  **See Also**
    :ref:`enable_word_wrap`, :ref:`hide_fields`, :ref:`highlight`, :ref:`set_text_view_mode`

----


.. _show_file:

:show-file *path*
^^^^^^^^^^^^^^^^^

  Show the given file(s) and resume indexing.

  **Parameters**
    * **path** --- The path or glob pattern that specifies the files to show


----


.. _show_lines_before_and_after:

:show-lines-before-and-after
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Show lines that were hidden by the 'hide-lines' commands

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`toggle_filtering`

----


.. _show_only_this_file:

:show-only-this-file
^^^^^^^^^^^^^^^^^^^^

  Show only the file for the focused line in the view


----


.. _show_unmarked_lines:

:show-unmarked-lines
^^^^^^^^^^^^^^^^^^^^

  Show lines that have not been bookmarked

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`, :ref:`hide_unmarked_lines`, :ref:`mark`, :ref:`next_mark`, :ref:`prev_mark`, :ref:`toggle_filtering`

----


.. _spectrogram:

:spectrogram *field-name*
^^^^^^^^^^^^^^^^^^^^^^^^^

  Visualize the given message field or database column using a spectrogram

  **Parameters**
    * **field-name\*** --- The name of the numeric field to visualize.

  **Examples**
    To visualize the sc_bytes field in the access_log format:

    .. code-block::  lnav

      :spectrogram sc_bytes


----


.. _summarize:

:summarize *column-name*
^^^^^^^^^^^^^^^^^^^^^^^^

  Execute a SQL query that computes the characteristics of the values in the given column

  **Parameters**
    * **column-name\*** --- The name of the column to analyze.

  **Examples**
    To get a summary of the sc_bytes column in the access_log table:

    .. code-block::  lnav

      :summarize sc_bytes


----


.. _switch_to_view:

:switch-to-view *view-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Switch to the given view

  **Parameters**
    * **view-name\*** --- The name of the view to switch to.

  **Examples**
    To switch to the 'schema' view:

    .. code-block::  lnav

      :switch-to-view schema


----


.. _tag:

:tag *tag*
^^^^^^^^^^

  Attach tags to the focused log line

  **Parameters**
    * **tag** --- The tags to attach

  **Examples**
    To add the tags '#BUG123' and '#needs-review' to the focused line:

    .. code-block::  lnav

      :tag #BUG123 #needs-review

  **See Also**
    :ref:`annotate`, :ref:`comment`, :ref:`delete_tags`, :ref:`untag`

----


.. _toggle_filtering:

:toggle-filtering
^^^^^^^^^^^^^^^^^

  Toggle the filtering flag for the current view

  **See Also**
    :ref:`filter_in`, :ref:`filter_out`, :ref:`hide_lines_after`, :ref:`hide_lines_before`, :ref:`hide_unmarked_lines`

----


.. _toggle_view:

:toggle-view *view-name*
^^^^^^^^^^^^^^^^^^^^^^^^

  Switch to the given view or, if it is already displayed, switch to the previous view

  **Parameters**
    * **view-name\*** --- The name of the view to toggle the display of.

  **Examples**
    To switch to the 'schema' view if it is not displayed or switch back to the previous view:

    .. code-block::  lnav

      :toggle-view schema


----


.. _unix_time:

:unix-time *seconds*
^^^^^^^^^^^^^^^^^^^^

  Convert epoch time to a human-readable form

  **Parameters**
    * **seconds\*** --- The epoch timestamp to convert

  **Examples**
    To convert the epoch time 1490191111:

    .. code-block::  lnav

      :unix-time 1490191111


----


.. _untag:

:untag *tag*
^^^^^^^^^^^^

  Detach tags from the focused log line

  **Parameters**
    * **tag** --- The tags to detach

  **Examples**
    To remove the tags '#BUG123' and '#needs-review' from the focused line:

    .. code-block::  lnav

      :untag #BUG123 #needs-review

  **See Also**
    :ref:`annotate`, :ref:`comment`, :ref:`tag`

----


.. _write_csv_to:

:write-csv-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write SQL results to the given file in CSV format

  **Parameters**
    * **--anonymize** --- Anonymize the row contents
    * **path\*** --- The path to the file to write

  **Examples**
    To write SQL results as CSV to /tmp/table.csv:

    .. code-block::  lnav

      :write-csv-to /tmp/table.csv

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_debug_log_to:

:write-debug-log-to *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write lnav's internal debug log to the given path.  This can be useful if the `-d` flag was not passed on the command line

  **Parameters**
    * **path\*** --- The destination path for the debug log


----


.. _write_json_to:

:write-json-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write SQL results to the given file in JSON format

  **Parameters**
    * **--anonymize** --- Anonymize the JSON values
    * **path\*** --- The path to the file to write

  **Examples**
    To write SQL results as JSON to /tmp/table.json:

    .. code-block::  lnav

      :write-json-to /tmp/table.json

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_jsonlines_to:

:write-jsonlines-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write SQL results to the given file in JSON Lines format

  **Parameters**
    * **--anonymize** --- Anonymize the JSON values
    * **path\*** --- The path to the file to write

  **Examples**
    To write SQL results as JSON Lines to /tmp/table.json:

    .. code-block::  lnav

      :write-jsonlines-to /tmp/table.json

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_raw_to:

:write-raw-to *\[--view={log,db}\]* *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  In the log view, write the original log file content of the marked messages to the file.  In the DB view, the contents of the cells are written to the output file.

  **Parameters**
    * **--view={log,db}** --- The view to use as the source of data
    * **--anonymize** --- Anonymize the lines
    * **path\*** --- The path to the file to write

  **Examples**
    To write the marked lines in the log view to /tmp/table.txt:

    .. code-block::  lnav

      :write-raw-to /tmp/table.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_screen_to:

:write-screen-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write the displayed text or SQL results to the given file without any formatting

  **Parameters**
    * **--anonymize** --- Anonymize the lines
    * **path\*** --- The path to the file to write

  **Examples**
    To write only the displayed text to /tmp/table.txt:

    .. code-block::  lnav

      :write-screen-to /tmp/table.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_table_to:

:write-table-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write SQL results to the given file in a tabular format

  **Parameters**
    * **--anonymize** --- Anonymize the table contents
    * **path\*** --- The path to the file to write

  **Examples**
    To write SQL results as text to /tmp/table.txt:

    .. code-block::  lnav

      :write-table-to /tmp/table.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_to`, :ref:`write_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_to:

:write-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Overwrite the given file with any marked lines in the current view

  **Parameters**
    * **--anonymize** --- Anonymize the lines
    * **path\*** --- The path to the file to write

  **Examples**
    To write marked lines to the file /tmp/interesting-lines.txt:

    .. code-block::  lnav

      :write-to /tmp/interesting-lines.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_view_to`, :ref:`write_view_to`, :ref:`xopen`

----


.. _write_view_to:

:write-view-to *\[--anonymize\]* *path*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Write the text in the top view to the given file without any formatting

  **Parameters**
    * **--anonymize** --- Anonymize the lines
    * **path\*** --- The path to the file to write

  **Examples**
    To write the top view to /tmp/table.txt:

    .. code-block::  lnav

      :write-view-to /tmp/table.txt

  **See Also**
    :ref:`alt_msg`, :ref:`append_to`, :ref:`cd`, :ref:`create_logline_table`, :ref:`create_search_table`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echo`, :ref:`echoln`, :ref:`eval`, :ref:`export_session_to`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`rebuild`, :ref:`redirect_to`, :ref:`redirect_to`, :ref:`sh`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_to`, :ref:`xopen`

----


.. _xopen:

:xopen *path*
^^^^^^^^^^^^^

  Use an external command to open the given file(s)

  **Parameters**
    * **path** --- The path to the file to open

  **Examples**
    To open the file '/path/to/file':

    .. code-block::  lnav

      :xopen /path/to/file

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`open`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _zoom_to:

:zoom-to *zoom-level*
^^^^^^^^^^^^^^^^^^^^^

  Zoom the histogram view to the given level

  **Parameters**
    * **zoom-level\*** --- The zoom level

  **Examples**
    To set the zoom level to '1-week':

    .. code-block::  lnav

      :zoom-to 1-week


----

