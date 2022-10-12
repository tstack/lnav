## lnav v0.11.1

Features:
* Additional validation checks for log formats have been
  added and will result in warnings.  Pass `-W` on the
  command-line to view the warnings.  The following new
  check have been added:
  - Each regex must have a corresponding sample log message
    that it matches.
  - Each sample must be matched by only one regex.
* Added built-in support for anonymizing content.  The
  `:write-*` commands now accept an `--anonymize` option
  and there is an `anonymize()` SQL function.  The
  anonymization process will try to replace identifying
  information with random data.  For example, IPv4 addresses
  are replaced with addresses in the 10.0.0.0/8 range.
  (This feature is mainly intended to help with providing
   information to lnav support that does not have sensitive
   values.)
* Added `parse_url()` and `unparse_url()` SQL functions for
  parsing URLs into a JSON object and then back again. Note
  that the implementation relies on libcurl which has some
  limitations, like not supporting all types of schemes
  (e.g. `mailto:`).
* Added the `subsecond-field` and `subsecond-units` log
  format properties to allow for specifying a separate
  field for the sub-second portion of a timestamp.
* Added a keymap for Swedish keyboards.

Breaking changes:
* The `regexp_capture()` table-valued-function now returns NULL
  instead of an empty string for the `capture_name` column if
  the capture is not named.

Fixes:
* Reduce the "no patterns have a capture" error to a warning
  so that it doesn't block lnav from starting up.
* Some ANSI escape sequences will now be removed before testing
  regexes against a log message.
* If a line in a JSON-lines log file does not start with a
  `{`, it will now be shown as-is and will not have the JSON
  parse error.

Cost of Doing Business:
* Migrated from pcre to pcre2.

## lnav v0.11.0

Features:
* Redesigned the top status area to allow for user-specified
  messages and added a second line that displays an interactive
  breadcrumb bar.  The top status line now shows the clock and
  the remaining area displays whatever messages are inserted
  into the lnav_user_notifications table.  The information that
  was originally on top is now in a second line and organized
  as breadcrumbs.  Pressing `ENTER` will activate the breadcrumb bar
  and the left/right cursor keys can be used to select a particular
  crumb while the up/down keys can select a value to switch to.
  While a crumb is selected, you can also type in some text to do
  a fuzzy search on the possibilities or, if the crumb represents
  an array of values, enter the index to jump to.
* The pretty-print view will now show breadcrumbs that indicate the
  location of the top line in the view with the prettified structure.
* Markdown files (those with a .md extension) are now rendered in the
  TEXT view.  The breadcrumb bar at the top will also be updated
  depending on the section of the document that you are in and you
  can use it to jump to different parts of the doc.
* The `:goto` command will now accept anchor links (i.e. `#section-id`)
  as an argument when the text file being viewed has sections.  You
  can also specify an anchor when opening a file by appending
  `#<link-name>`.  For example, `README.md#screenshot`.
* Log message comments are now treated as markdown and rendered
  accordingly in the overlay.  Multi-line comments are now supported
  as well.
* Metadata embedded in files can now be accessed by the
  `lnav_file_metadata` table.  Currently, only the front-matter in
  Markdown files is supported.
* Added an integration with regex101.com to make it easier to edit
  log message regular expressions.  Using the new "management CLI"
  (activated by the `-m` option), a log format can be created from
  a regular expression entry on regex101.com and existing patterns
  can be edited.
* In the spectrogram view, the selected value range is now shown by
  an overlay that includes a summary of the range and the number of
  values that fall in that range.  There is also a detail panel at
  the bottom that shows the log-messages/DB-rows whose values are in
  that range.  You can then press TAB to focus on the detail view
  and scroll around.
* Add initial support for pcap(3) files using tshark(1).
* SQL statement execution can now be canceled by pressing `CTRL+]`
  (same as canceling out of a prompt).
* To make it possible to automate some operations, there is now an
  `lnav_events` table that is updated when internal events occur
  within lnav (e.g. opening a file, format is detected).  You
  can then add SQLite `TRIGGER`s to this table that can perform a
  task by updating other tables.
* Tags can automatically be added to messages by defining a pattern
  in a log format.  Under a format definition, add the tag name
  into the "tags" object in a format definition.  The "pattern"
  property specifies the regular expression to match against a line
  in a file that matches the format.  If a match is found, the tag
  will be applied to the log message.  To restrict matches to
  certain files, you can add a "paths" array whose object elements
  contain a "glob" property that will be matched against file names.
* Log messages can now be detected automatically via "watch
  expressions".  These are SQL expressions that are executed for
  each log message.  If the expressions evaluates to true, an
  event is published to the `lnav_events` table that includes the
  message contents.
* Added the `regexp_capture_into_json()` table-valued-function that
  is similar to `regexp_capture()`, but returns a single row with a
  JSON value for each match instead of a row for each capture.
* Added a `top_meta` column to the lnav_views table that contains
  metadata related to the top line in the view.
* Added a `log_opid` hidden column to all log tables that contains
  the "operation ID" as specified in the log format.
* Moved the `log_format` column from the all_logs table to a hidden
  column on all tables.
* Add format for UniFi gateway.
* Added a `glob` property to search tables defined in log formats
  to constrain searches to log messages from files that have a
  matching log_path value.
* Initial indexing of large files should be faster.  Decompression
  and searching for line-endings are now pipelined, so they happen
  in a thread that is separate from the regular expression matcher.
* Writing to the clipboard now falls back to OSC 52 escape sequence
  if none of the clipboard commands could be detected.  Your
  terminal software will need to support the sequence and you may
  need to explicitly enable it in the terminal.
* Added the `:export-session-to <path>` command that writes the
  current session state to a file as a list of commands/SQL
  statements.  This script file can be executed to restore the
  majority of the current state.
* Added the `echoln()` SQL function that behaves similarly to the
  `:echo` command, writing its first argument to the current
  output.
* Added `encode()` and `decode()` SQL functions for transcoding
  blobs or text values using one of the following algorithms:
  base64, hex, or uri.
* In regular expressions, capture group names are now semantically
  highlighted (e.g. in the capture, `(?<name>\w+)`, "name" would
  have a unique color).  Also, operations or previews that use
  that regular expression will highlight the matched data with
  the same color.
* Added an lnav_views_echo table that is a real SQLite table that
  you can create TRIGGERs on in order to perform actions when
  scrolling in a view.
* Added a `yaml_to_json()` SQL function that converts a YAML
  document to the equivalent JSON.

Breaking Changes:
* Formats definitions are now checked to ensure that values have a
  corresponding capture in at least one pattern.
* Added a 'language' column to the lnav_view_filters table that
  specifies the language of the 'pattern' column, either 'regex'
  or 'sql'.
* Timestamps that do not have a day or month are rewritten to a
  full timestamp like YYYY-MM-DD HH:MM:SS.
* Removed the summary overlay at the bottom of the log view that
  displayed things like "Error rate" and the time span.  It doesn't
  seem like anyone used it.
* Removed the `log_msg_instance` column from the logline and search
  tables since it causes problems with performance.
* Search tables now search for multiple matches within a message
  instead of stopping at the first hit.  Each additional match is
  returned as a separate row.  A `match_index` column has been
  added to capture the index of the match within the message.
  The table regex is also compiled with the "multiline" flag enabled
  so the meaning of the `^` and `$` metacharacters are changed
  to match the start/end of a line instead of the start/end of
  the entire message string.
* Search tables defined in formats are now constrained to only
  match log messages that are in that log format instead of all
  log messages.  As a benefit, the search table now includes
  the columns that are defined as part of the format.
* The lnav_view_filters table will treats the tuple of
  (view_name, type, language, pattern) as a `UNIQUE` index and
  will raise a conflict error on an `INSERT`.  Use `REPLACE INTO`
  instead of `INSERT INTO` to ignore conflict error.
* The types of SQL values stored as local variables in scripts
  is now preserved when used as bound variables at a later point
  in the script.

Fixes:
* Toggling enabled/disabled filters when there is a SQL expression
  no longer causes a crash.
* Fix a crash related to long lines that are word wrapped.
* Multiple SQL statements in a SQL block of a script are now
  executed instead of just the first one.
* In cases where there were many colors on screen, some text would
  be colored incorrectly.
* The pretty-print view now handles ANSI escape sequences.
* The "overstrike" convention for doing bold and underline is now
  supported.  (Overstrike is a character followed by a backspace
  and then the same character for bold or an underscore for
  underline.)
* The `:eval` command now works with searching (using the '/'
  prefix).

## lnav v0.10.1

Features:
* Added `:show-only-this-file` command that hides all files except the
  one for the top line in the view.
* The `:write-raw-to` command now accepts a `--view` flag that specifies
  the source view for the data to write.  For example, to write the
  results of a SQL query, you would pass `--view=db` to the command.
* The commands used to access the clipboard are now configured through
  the "tuning" section of the configuration.
* Added an `lnav_version()` SQL function that returns the current
  version string.
* Added basic support for the logfmt file format.  Currently, only files
  whose lines are entirely logfmt-encoded are supported.  The lines
  must also contain either a field named `time` or `ts` that contains
  the timestamp.
* Added the `logfmt2json()` SQL function to convert a string containing
  a logfmt-encoded message into a JSON object that can be operated on
  more easily.
* Added the `gzip()` and `gunzip()` SQL functions to compress values
  into a blob and decompress a blob into a string.
Interface changes:
* The xclip implementation for accessing the system clipboard now writes
  to the "clipboard" selection instead of the "primary" selection.
* The 'query' bookmark type and `y`/`Y` hotkeys have been removed due to
  performance issues and the functionality is probably rarely used.
Bug Fixes:
* The text "send-input" would show up on some terminals instead of
  ignoring the escape sequence.  This control sequence was only
  intended to be used in the test suite.
* Remote file synchronization has been optimized a bit.
* Configuration values loaded from the `~/.lnav/configs` directory
  are now included in the default configuration, so they won't be
  saved into the `~/.lnav/config.json` user configuration file.
* Key handling in the visual filter editor will no longer swallow
  certain key-presses when editing a filter.
* Scrolling performance restored in the SQL view.
* The `:redirect-to` command now works with `/dev/clipboard`
* The field overlay (opened by pressing 'p') now shows `log_time`
  for the timestamp field instead of the name defined in the format.
* The search term in the bottom status bar will now update properly
  when switching views.
* The "Out-Of-Time-Order Message" overlay will be shown again.
* The tab for the "Files" panel will be highlighted in red if there
  is an issue opening a file.
* Overwritten files should be reloaded again.
* The `jget()` SQL function now returns numbers with the correct type.
* The `json_contains()` SQL function now returns false if the first
  argument is NULL instead of NULL.
* The local copies of remote files are now cleaned up after a couple
  days of the host not being accessed.
* The initial loading and indexing phase has been optimized.

## lnav v0.10.0

Features:
* Files on remote machines can be viewed/tailed if they are accessible
  via SSH.  The syntax for specifying the host and path is similar to
  scp.  For example, to view the files in the /var/log directory on the
  machine `host1.example.org`:
    ```console
    $ lnav user@host1.example.org:/var/log
    ```
  Note that you must be able to log into the machine without any
  interaction.
* Added the `:filter-expr` command to filter log messages based on an SQL
  expression.  This command allows much greater control over filtering.
* Added the `:mark-expr` command to mark log messages based on an SQL
  expression.  This command makes it easier to programmatically mark
  log messages compared to using SQL.
* Added support for archive files, like zip, and other compression formats,
  like xz, when compiled with libarchive.  When one of these types of
  files is detected, they are unpacked into a temporary directory and
  all the files are loaded into lnav.
* Added an `xpath()` table-valued function for extracting values from
  strings containing XML snippets.
* Added the `:prompt` command to allow for more customization of prompts.
  Combined with a custom keymapping, you can now open a prompt and prefill
  it with a given value.  For example, a key could be bound to the
  following command to open the command prompt with `:filter-in `
  already filled in:
    ```lnav
    :prompt command : 'filter-in '
    ```
* Added support for the W3C Extended Log File Format with the name
  `w3c_log`.  Similarly to the bro log format, the header is used to
  determine the columns in a particular file.  However, since the columns
  can be different between files, the SQL table only has a well-known set
  of columns and the remainder are accessible through JSON-objects stored
  in columns like `cs_headers` and `sc_headers`.
* Added support for the S3 Access File Format.
* To jump to the first search hit above the top line in a view, you can
  press `CTRL+J` instead of `ENTER` in the search prompt.  Pressing `ENTER`
  will jump to the first hit below the current window.
* Filtering, as a whole, can be now disabled/enabled without affecting
  the state of individual filters.  This includes text and time-filters
  (i.e. `:hide-lines-before`).  You can enable/disable filtering by:
  pressing `f` in the filter editor UI; executing the `:toggle-filtering`
  command; or by doing an `UPDATE` on the "filtering" column of the
  `lnav_views` SQLite table.
* Themes can now include definitions for text highlights under:
    `/ui/theme-defs/<theme_name>/highlights`
* Added a "grayscale" theme that isn't so colorful.
* Added the `humanize_file_size()` SQL function that converts a numeric size
  to a human-friendly string.
* Added the `sparkline()` SQL function that returns a "sparkline" bar made
  out of unicode characters.  It can be used with a single value or as
  an aggregator.
* Added a `log_time_msecs` hidden column to the log tables that returns
  the timestamp as the number of milliseconds from the epoch.
* Added an `lnav_top_file()` SQL function that can be used to get the
  name of the top line in the top view or NULL if the line did not come
  from a file.
* Added a `mimetype` column to the lnav_file table that returns a guess as
  to the MIME type of the file contents.
* Added a `content` hidden column to the lnav_file table that can be used
  to read the contents of the file.  The contents can then be passed to
  functions that operate on XML/JSON data, like `xpath()` or `json_tree()`.
* Added an `lnav_top_view` SQL VIEW that returns the row for the top view
  in the lnav_views table.
* The `generate_series()` SQLite extension is now included by default.
  One change from the standard implementation is that both the start and
  stop are required parameters.
* Added the `;.read` SQL command for executing a plain SQL file.
* Added the `-N` flag so that lnav will run without opening the default
  syslog file.

Interface Changes:
* When copying log lines, the file name and time offset will be included
  in the copy if they are enabled.
* Log messages that cannot be parsed properly will be given an "invalid"
  log level and the invalid portions colored yellow.
* The range_start and range_stop values of the `regexp_capture()` results
  now start at 1 instead of zero to match with what the other SQL string
  functions expect.
* The `:write-cols-to` command has been renamed to `:write-table-to`.
* The DB view will limit the maximum column width to 120 characters.
* The `:echo` command now evaluates its message to do variable
  substitution.
* The `:write-raw-to` command has been changed to write the original
  log file content of marked lines.  For example, when viewing a JSON
  log, the JSON-Line values from the log file will be written to the
  output file.  The `:write-view-to` command has been added to perform
  the previous work of `:write-raw-to` where the raw content of the view
  is written to the file.

Fixes:
* Unicode text can now be entered in prompts.
* The `replicate()` SQL function would cause a crash if the number of
  replications was zero.
* Many internal improvements.

## lnav v0.9.0

Features:
* Added support for themes and included a few as well: default, eldar,
  monocai, night-owl, solarized-light, and solarized-dark.  The theme
  can be changed using the `:config` command, like so:
    ```lnav
    :config /ui/theme night-owl
    ```
  Consult the online documentation for defining a new theme at:
    https://lnav.readthedocs.io/en/latest/config.html#theme-definitions
* Added support for custom keymaps and included the following: de, fr,
  uk, us.  The keymap can be changed using the `:config` command, like so:
    ```lnav
    :config /ui/keymap uk
    ```
  Consult the online documentation for defining a new keymap at:
    https://lnav.readthedocs.io/en/latest/config.html#keymap-definitions
* The following JSON-Schemas have been published for the log format and
  configuration JSON files:
    - https://lnav.org/schemas/format-v1.schema.json
    - https://lnav.org/schemas/config-v1.schema.json

  Formats should be updated to reference the schema using the `$schema`
  property.
* Indexing of new data in log files can now be paused by pressing `=`
  and unpaused by pressing it again.  The bottom status bar will display
  'Paused' in the right corner while paused.
* CMake is now a supported way to build.
* When viewing data from the standard-input, a symbolic name can be used
  to preserve session state.  The name can be changed using the
  `|rename-stdin` lnav script or by doing an `UPDATE` to the filepath
  column of the lnav_file table.  For example, to assign the name
  "journald", the following SQL statement can be executed in lnav:
    ```lnav
    ;UPDATE lnav_file SET filepath='journald' WHERE filepath='stdin'
    ```
* The size of the terminal can be accessed in SQL using the `$LINES` and
  `$COLS` variables.
* The `raise_error(msg)` SQL function has been added to make it easier to
  raise an error in an lnav script to stop execution and notify the user.
* Added the `json_concat()` function to make it easier to append/concatenate
  values onto arrays.
* Added the `:write-jsonlines-to` command that writes the result of a SQL
  query to a file in the JSON Lines format.

Interface Changes:
* Data piped into lnav is no longer dumped to the console after exit.
  Instead, a file containing the data is left in `.lnav/stdin-captures/`
  and a message is printed to the console indicating the file name.
* In time-offset mode, the deltas for messages before the first mark
  are now negative instead of relative to the start of the log.
* The $XDG_CONFIG_HOME environment variable (or `~/.config` directory) are
  now respected for storing lnav's configuration.  If you have an existing
  `~/.lnav` directory, that will continue to be used until you move it to
  `$XDG_CONFIG_HOME/lnav` or `~/.config/lnav`.
* Removed the `:save-config` command. Changes to the configuration are now
  immediately saved.

Fixes:
* Added 'notice' log level.
* If a `timestamp-format` is used in an element of a `line-format`, the
  field name is ignored and a formatted timestamp is always used.
* Ignore stdin when it is connected to `/dev/null`.

## lnav v0.8.5

Features:
* Added a visual filter editor to make it easier to update existing
  filters.  The editor can be opened by pressing `TAB`.  Once the editor
  is opened, you can create/delete, enable/disable, and edit the patterns
  with hotkeys.
* Added an `lnav_view_filters` SQL table that can be used to
  programmatically manipulate filters.
* Added an `lnav_view_filter_stats` SQL table that contains the number of
  times a given filter matched a line in the view.
* Added a `log_filters` column to log tables that can be used to see what
  filters matched the log message.
* A history of locations in a view is now kept so that you can jump back
  to where you were previously using the `{` and `}` keys.  The location
  history can also be accessed through the `:prev-location` and
  `:next-location` commands.
* The `:write-*` commands will now accept `/dev/clipboard` as a file name
  that writes to the system clipboard.
* The `:write-to` and `:write-raw-to` commands will now print out comments
  and tags attached to the lines.
* Added a `:redirect-to <path>` command to redirect command output to the
  given file.  This command is mostly useful in scripts where one might
  want to redirect all output from commands like `:echo` and `:write-to -`
  to a single file.
* If a log file format has multiple patterns for matching log messages,
  each pattern is now tried to match a message in a file.  Previously,
  only one pattern was ever used for an entire file.
* Added haproxy log format from Peter Hoffmann.
* Added `spooky_hash()` and `group_spooky_hash()` SQL functions to
  generate a hash of their parameters.
* Added `time_offset` to the `lnav_file` table so that the timestamps in
  a file can be adjusted programmatically.

Interface Changes:
* The auto-complete behavior in the prompt has been modified to fall back
  to a fuzzy search if the prefix search finds no matches.  For example,
  typing in `:fin` and pressing TAB would previously not do anything.
  Now, the `:fin` will be completed to `:filter-in ` since that is a
  strong fuzzy match.  If there are multiple matches, as would happen
  with `:dfil`, readline's menu-complete behavior will be engaged and
  you can press `TAB` cycle through the options.
* Added `CTRL+F` to toggle the enabled/disabled state of all filters for the
  current view.
* The `-r` flag is now for recursively loading files.  The functionality
  for loading rotated files is now under the `-R` flag.
* The current search term is now shown in the bottom status bar.
* Some initial help text is now shown for the search and SQL prompts to
  refresh the memory.
* When entering the `:comment` command for a line with a comment, the
  command prompt will be filled in with the existing comment to make
  editing easier.
* Hidden fields now show up as a unicode vertical ellipsis (⋮) instead of
  three-dot ellipsis to save space.
* Pressing 7/8 will now move to the previous/next minute.
* The `:write-raw-to` command has been changed to write the entire
  contents of the current view and a `:write-screen-to` command has been
  added to write only the current screen contents.
* Disabled filters are now saved in sessions.
* The `:adjust-log-time` command now accepts relative times as input.

Fixes:
* The `:write-json-to` command will now pass through JSON cells as their
  JSON values instead of a JSON-encoded string.

## lnav v0.8.4

Features:
* Added the `:comment` command that can be used to attach a comment to a
  log line.  The comment will be displayed below the line, like so:
    ```
    2017-01-01T15:30:00 error: computer is on fire
      + This is where it all went wrong
    ```
  The `:clear-comment` command will remove the attached comment.  Comments
  are searchable with the standard search mechanism and they are available
  in SQL through the `log_comment` column.
* Added the `:tag`, `:untag`, and `:delete-tags` commands that can be used
  to attach/detach tags on the top log line and delete all instances of
  a tag.  Tags are also searchable and are available in SQL as a JSON
  array in the `log_tags` column.
* Pressing left-arrow while viewing log messages will reveal the source
  file name for each line and the unique parts of the source path.
  Pressing again will reveal the full path.
* The file name section of the top status line will show only the unique
  parts of the log file path if there is not enough room to show the full
  path.
* Added the `:hide-unmarked-lines` and `:show-unmarked-lines` commands
  that hide/show lines based on whether they are bookmarked.
* Added the `json_contains()` SQL function to check if a JSON value
  contains a number of a string.
* The relative time parser recognizes "next" at the beginning of the
  input, for example, "next hour" or "next day".  Handy for use in the
  `:goto` command.
* Added a "text-transform" option for formatting JSON log messages.  The
  supported options are: none, uppercase, lowercase, and capitalize.
* Added a special `__level__` field name for formatting JSON messages so
  that the lnav level name can be used instead of the internal value in
  the JSON object.
* Added a log format for journald JSON logs.

Interface Changes:
* When typing in a search, instead of moving the view to the first match
  that was found, the first ten matches will be displayed in the preview
  window.
* The pretty-print view maintains highlighting from the log view.
* The pretty-print view no longer tries to reverse lookup IP addresses.
* The online help for commands and SQL functions now includes a 'See Also'
  section that lists related commands/functions.

Fixes:
* The HOME key should now work in the command-prompt and move the cursor
  to the beginning of the line.
* The `:delete-filter` command should now tab-complete existing filters.
* Milliseconds can now be used in relative times (e.g. 10:00:00.123)
* The `J`/`K` hotkeys were not marking lines correctly when the bottom of
  the view was reached.
* The level field in JSON logs should now be recognized by the level
  patterns in the format.

## lnav v0.8.3

Features:
* Support for the Bro Network Security Monitor (https://www.bro.org) log
  file format.
* Added an `fstat()` table-valued function for querying the local
  filesystem.
* Added `readlink()` and `realpath()` SQL functions.
* Highlights specified in log formats can now specify the colors to use
  for the highlighted parts of the log message.
* Added a `:quit` command.
* Added a `/ui/default-colors` configuration option to specify that the
  terminal's default background and foreground colors should be used
  instead of black and white.

Interface Changes:
* Pressing delete at a command-prompt will exit the prompt if there is no
  other input.

Fixes:
* The help view now includes all the command-help that would pop up as
  you entered commands and SQL queries.
* Hidden fields and lines hidden before/after times are now saved in the
  current session and restored.
* Unicode characters should now be displayed correctly (make sure you
  have LANG set to a UTF-8 locale).

## lnav v0.8.2

Features:
* The timestamp format for JSON log files can be specified with the
  `timestamp-format` option in the `line-format` array.
* Added "min-width", "max-width", "align", and "overflow" options to the
  "line-format" in format definitions for JSON log files.  These options
  give you more control over how the displayed line looks.
* Added a "hidden" option to log format values so that you can hide JSON
  log fields from being displayed if they are not in the line format.
* Added a `rewriter` field to log format value definitions that is a
  command used to rewrite the field in the pretty-printed version of a
  log message.  For example, the HTTP access log format will rewrite the
  status code field to include the textual version (e.g. 200 (OK)).
* Log message fields can now be hidden using the `:hide-fields` command or
  by setting the 'hidden' property in the log format.  When hidden, the
  fields will be replaced with a yellow ellipsis when displayed.  Hiding
  large fields that contain extra details can make the log easier to read.
  The `x` hotkey can be used to quickly toggle whether these fields are
  displayed or not.
* Added a `:mark` command to bookmark the top line in the current view.
* Added an `:alt-msg` command that can be used to set the text to be
  displayed in the bottom right of the command line.  This command is
  mostly intended for use by hotkey maps to set the help text.
* In lnav scripts, the first row of a SQL query result will now be turned
  into local variables that can be referenced in other commands or
  queries.  For example, the following script will print the number one:
    ```lnav
    ;SELECT 1 as foobar
    :eval :echo ${foobar}
    ```
* Added an `lnav_view_stack` SQL table that gives access to the view
  stack.
* Added a `top_time` column to the lnav_views table so that you can get
  the timestamp for the top line in views that are time-based as well as
  allowing you to move the view to a given time with an UPDATE statement.
* Added a 'search' column to the lnav_views table so that you can perform
  a text search programmatically.
* Added a `regexp_capture(<string>, <pattern>)` table-valued function for
  getting detailed results from matching a regular expression against a
  string.
* Added a `timediff(<time1>, <time2>)` SQL function for computing the
  difference between two relative or absolute timestamps.
* Log formats can now define a default set of highlights with the
  "highlights" property.
* Added a `|search-for <pattern>` built-in script that can be used to
  start a search from the command-line.
* Log format definitions can now specify the expected log level for a
  sample line.  This check should make it easier to validate the
  definition.

Interface Changes:
* Command and SQL documentation is now displayed in a section at the
  bottom of the screen when a command or query is being entered.  Some
  commands will also display a preview of the command results.  For
  example, the `:open` command will display the first ten lines of the
  file to be opened and the `:filter-out` command will highlight text
  that matches in the current view.  The preview pane can be shown/hidden
  by pressing `CTRL-P`.
* The color used for text colored via `:highlight` is now based on the
  the regex instead of randomly picked so that colors are consistent
  across invocations.
* The "graph" view has been removed since it's functionality has been
  obsoleted by other features, like `:create-search-table`.
* When doing a search, if a hit is found within a second after hitting
  `<ENTER>`, the view will move to the matched line.  The previous behavior
  was to stay on the current line, which tended to be a surprise to new
  users.
* Pressing `n`/`N` to move through the next/previous search hit will now
  skip adjacent lines, up to the vertical size of the view.  This should
  make scanning through clusters of hits much faster.  Repeatedly
  pressing these keys within a short time will also accelerate scanning
  by moving the view at least a full page at a time.

Breaking Changes:
* The captured timestamp text in log files must fully match a known format
  or an error will be reported.  The previous behavior was to ignore any
  text at the end of the line.

Fixes:
* You can now execute commands from the standard input by using a dash (`-`)
  with the `-f` command-line argument.  Reading commands from a file
  descriptor should also work, for example, with the following bash
  syntax:
     ```console
     $ lnav -f <(echo :open the-file-to-open)
     ```
* Programming language syntax highlighting should now only be applied to
  source code files instead of everywhere.

## lnav v0.8.1

Features:
* Added a spectrogram command and view that displays the values of a
  numeric field over time.  The view works for log message fields or
  for database result columns.
* Log formats can now create SQL views and execute other statements
  by adding `.sql` files to their format directories.  The SQL scripts
  will be executed on startup.
* Added `json_group_object` and `json_group_array` aggregate SQL
  functions that collects values from a GROUP BY query into a JSON
  object or array, respectively.
* The SQL view will now graph values found in JSON objects/arrays in
  addition to the regular columns in the result.
* Added an `regexp_match(<re>, <str>)` SQL function that can be used to
  extract values from a string using a regular expression.
* Added an `extract(<str>)` SQL function that extracts values using the
  same data discover/extraction parser used in the `logline` table.
* Added a "summary" overlay line to the bottom of the log view that
  displays how long ago the last message was received, along with the
  total number of files and the error rate over the past five minutes.
* Pressing `V` in the DB view will now check for a column with a
  timestamp and move to the corresponding time in the log view.
* Added `a`/`A` hotkeys to restore a view previously popped with `q`/`Q`.
* Added `:hide-lines-before`, `:hide-lines-after`, and
  `:show-lines-before-and-after` commands so that you can filter out
  log lines based on time.
* Scripts containing lnav commands/queries can now be executed using
  the pipe (`|`) hotkey.  See the documentation for more information.
* Added an `:eval` command that can be used to execute a command or
  query after performing environment variable substitution.
* Added an `:echo` command that can be useful for scripts to message
  the user.
* The `log_part` column can now be set with an SQL `UPDATE` statement.
* Added a `log_body` hidden column that returns the body of the log
  message.
* Added `:config`, `:reset-config`, and `:save-config` commands to change
  configuration options, reset to default, and save them for future
  executions.
* Added a `/ui/clock-format` configuration option that controls the time
  format in the top-left corner.
* Added a `/ui/dim-text` configuration option that controls the brightness
  of text in the UI.
* Added support for TAI64 timestamps (http://cr.yp.to/libtai/tai64.html).
* Added a safe execution mode. If the `LNAVSECURE` environment variable is
  set before executing lnav, the following commands are disabled:
  - `:open`
  - `:pipe-to`
  - `:pipe-line-to`
  - `:write-*-to`

  This makes it easier to run lnav with escalated privileges in restricted
  environments, without the risk of users being able to use the above
  mentioned commands to gain privileged access.

Interface Changes:
* The `o`/`O` hotkeys have been reassigned to navigate through log
  messages that have a matching "opid" field.  The old action of
  moving forward and backward by 60 minutes can be simulated by
  using the `:goto` command with a relative time and the `r`/`R`
  hotkeys.
* Log messages with timestamps that pre-date previous log messages will
  have the timestamp highlighted in yellow and underlined.  These out-
  of-time-order messages will be assigned the time of the previous
  message for sorting purposes.  You can press the 'p' hotkey to examine
  the 'Received Time' of the message as well as the time parsed from the
  original message.  A `log_actual_time` hidden field has also been
  added to the SQLite virtual table so you can operate on the original
  message time from the file.
* The `A`/`B` hotkeys for moving forward/backward by 10% line increments
  have been reassigned to `[` and `]`.  The `a` and `A` hotkeys are now
  used to return to the previously popped view while trying to preserve
  the time range.  For example, after leaving the spectrogram view with
  'q', you can press 'A' return to the view with the top time in the
  spectrogram matching the top time in the log view.
* The 'Q' hotkey now pops the current view off of the stack while
  maintaining the top time between views.

Fixes:
* Issues with tailing JSON logs have been fixed.
* The `jget()` SQL function should now work for objects nested in arrays.

## lnav v0.8.0

Features:
* Integration with "papertrailapp.com" for querying and tailing
  server log and syslog messages.  See the Papertrail section in
  the online help for more details.
* Remote files can be opened when lnav is built with libcurl v7.23.0+
* SQL queries can now be done on lines that match a regular expression
  using the `log_search` table or by creating custom tables with the
  `:create-search-table` command.
* Log formats that are "containers" for other log formats, like
  syslog, are now supported.  See the online help for more
  information.
* Formats can be installed from git repositories using the `-i` option.
  A standard set of extra formats can be installed by doing
  `lnav -i extra`. (You must have git installed for this to work.)
* Added support for 'VMware vSphere Auto Deploy' log format.
* Added a 'sudo' log format.
* Added hotkeys to move left/right by a smaller increment (H/L or
  Shift+Left/Shift+Right).
* A color-coded bar has been added to the left side to show where
  messages from one file stop and messages from another file start.
* The `-C` option will now try to check any specified log files to
  make sure the format(s) match all of the lines.
* Added an `all_logs` SQLite table that contains the message format
  extracted from each log line.  Also added a `;.msgformat` SQL command
  that executes a query that returns the counts for each format and the
  first line where the format was seen.
* Added an `lnav_views` SQLite table that can be used to query and
  change the lnav view state.
* When typing in a command, the status bar will display a short
  summary of the currently entered command.
* Added a `:delete-filter` command.
* Added a `log_msg_instance` column to the logline and log_search
  tables to make it easier to join tables that are matching log
  messages that are ordered.
* Added a `timeslice()` function to SQLite so that it is easier to
  group log messages by time buckets.
* The `:goto` command now supports relative time values like
  `a minute ago`, `an hour later`, and many more.

Interface Changes:
* The `r`/`R` hotkeys have been reassigned to navigate through the log
  messages by the relative time value that was last used with the
  `:goto` command.

Fixes:
* The pretty-print view should now work for text files.
* Nested fields in JSON logs are now supported for levels, bodies, etc...
* Tab-completion should work for quoted SQL identifiers.
* 'lo-fi' mode key shortcut changed to `CTRL+L`.
* 'redraw' shortcut removed. Relegated to just a command.
* Fixed lnav hang in pretty-print mode while doing a dns lookup.
* The generic log message parser used to extract data has been
  optimized and should be a bit faster.

## lnav v0.7.3

Features:
* Add `:pipe-to` and `:pipe-line-to` commands that pipe the currently
  marked lines or the current log message to a shell command,
  respectively.
* Added a "pretty-print" view (P hotkey) that tries to reformat log
  messages so that they are easier to read.
* Added a `:redraw` command (CTRL+L hotkey) to redraw the window in
  case it has been corrupted.
* Added a `:relative-goto` command to move the current view relative
  to its current position.
* Experimental support for linking with jemalloc.
* The plain text view now supports filtering.
* Added `:next-mark` and `:prev-mark` commands to jump to the next or
  previous bookmarked line (e.g. error, warning, ...)
* Added a `:zoom-to` command to change the zoom level of the histogram
  view.
* Log formats can now define their own timestamp formats with the
  `timestamp-format` field.

Fixes:
* Autotools scripts overhaul.
* Added a configure option to disable linking with libtinfo. The newer
  versions of ncurses don't require it, however the build silently pulls
  it in as a dependency, if it is available on the system. This can be
  explicitly disabled using the `--disable-tinfo` option during configure.
* Fixed the configure script behavior to ignore the values specified using
  the CFLAGS and LDFLAGS environment variables while searching for sqlite3
  when `--with-sqlite3` switch was specified without the prefix.
* The configure script now recognizes libeditline symlink'ed to masquerade
  as libreadline. This previously used to cause problems at compile time,
  specially on OS X. If you come across this error, use the
  `--with-readline=prefix` switch to specify the path to the correct
  location of libreadline.
* The order that log formats are tried against a log file is now
  automatically determined so that more specific formats are tested
  before more general ones.  The order is determined on startup based on
  how each format matches each other formats sample lines.
* Command files (i.e. those executed via the `-f` flag) now support
  commands/queries that span more than one line.
* Added more log levels: stats, debug2 - debug5.

## lnav v0.7.2

* Added log formats for vdsm, openstack, and the vmkernel.
* Added a "lo-fi" mode (L hotkey) that dumps the displayed log lines
  to the terminal without any decorations.  The `:write-to`, `:write-json-to`,
  and `:write-csv-to` commands will also write their output to the terminal
  when passed `-` as the file name.  This mode can be useful for copying
  plain text lines to the clipboard.
* (OS X) Text search strings are copied to the system's "find" clipboard.
  Also, when starting a new search, the current value in the "find"
  clipboard can be tab-completed.

## lnav v0.7.1

Features:
* Added an `environ` SQL table that reflects lnav's environment
  variables.  The table can be read and written to using SQL
  `SELECT`, `INSERT`, `UPDATE`, and `DELETE` statements.  Setting
  variables can be a way to use SQL query results in lnav commands.
* Added a `jget()` SQLite function that can extract fields from a JSON-
  encoded value.
* Added log formats for the OpenAM identity provider.
* Added a `:clear-highlight` command to clear previous calls to the
  `:highlight` command.
* Fixed some performance bugs in indexing JSON log formats.  Loading
  times should be at least five times faster.
* Filtering performance should be improved so that enabling/disabling
  filters should be almost instantaneous.
* The `:filter-in`, `:filter-out`, and `:highlight` commands now support
  tab-completion of text in the current view.
* Add a `-i` flag that installs format files in: `~/.lnav/formats/installed`

## lnav v0.7.0

Features:
* Add the '.schema' SQL command to open a view that displays the schema
  for the internal tables and any attached databases.  If lnav was only
  executed with a SQLite database and no text files, this view will open
  by default.
* The scroll bar now indicates the location of errors/warnings, search
  hits, and bookmarks.
* The xterm title is update to reflect the file name for the top line
  in the view.
* Added a "headless" mode so that you can execute commands and run SQL
  queries from the command-line without having to do it from the curses
  UI.
* When doing a search or SQL query, any text that is currently being
  displayed can be tab-completed.
* The `-H` option was added so you can view the internal help text.
* Added the 'g/G' hotkeys to move to the top/bottom of the file.
* Added a `log_mark` column to the log tables that indicates whether or
  not a log message is bookmarked.  The field is writable, so you can
  bookmark lines using an SQL UPDATE query.
* Added syntax-highlighting when editing SQL queries or search regexes.
* Added a `:write-json-to` command that writes the result of a SQL query
  to a JSON-formatted file.
* The "elapsed time" column now uses red/green coloring to indicate
  sharp changes in the message rate.
* Added a `:set-min-log-level` command to filter out log messages that
  are below a given level.

Fixes:
* Performance improvements.
* Multi-line filtering has been fixed.
* A collator has been added to the log_level column in the log tables
  so that you can write expressions like `log_level > 'warning'`.
* The log_time datetime format now matches what is returned by
  `datetime('now')` so that collating works correctly.
* If a search string is not valid PCRE syntax, a search is done for
  the exact string instead of just returning an error.
* Static-linking has been cleaned up.
* OpenSSL is no longer a requirement.
* Alpha support for Windows/cygwin.
* Environment variables can now be accessed in SQL queries using
  the syntax: `$VAR_NAME`
* An internal log is kept and written out on a crash.
* Partition bookmarks are now tracked separately from regular user
  bookmarks.  You can start a partition with the 'partition-name'
  command and remove it with the 'clear-partition' command.
* Improved display of possible matches during tab-completion in the
  command-prompt.  The matches are now shown in a separate view and
  pressing tab repeatedly will scroll through the view.
* The "open" command now does shell word expansion for file names.
* More config directory paths have been added: `/etc/lnav`,
  `$prefix/etc/lnav`, and directories passed on the command-line
  with `-I`.

## lnav v0.6.2

Features:
* Word-wrap support.

Fixes:
* Fix some OS X Mavericks build/runtime issues.

## lnav v0.6.1
Features:
* Support for JSON-encoded log files.

Fixes:
* Some minor fixes and performance improvements.

## lnav v0.6.0

Features:
* Custom log formats and more builtin formats
* Automatic extraction of data from logs
* UI improvements, support for 256 color terminals

## lnav v0.5.1

Features:
* Added the `-t` and `-w` options which can be used to prepend a
  timestamp to any data piped in on stdin and to specify a file to
  write the contents of stdin to.

Fixes:
* Cleanup for packaging.

## lnav v0.5.0

Features:
* Files can be specified on the command-line using wildcards so that
  new files are automatically loaded.  Directories can also be passed
  as command-line arguments to read all of the files in the directory.
* Builds on cygwin again.
* Added the `C` hotkey to clear any existing user bookmarks.
* Added experimental support for accepting input from mice.

Fixes:
* Internal cleanup.
* Copying to the clipboard on OS X is now supported.
* Many bug fixes.

## lnav v0.4.0

Features:
* Files that are not recognized as containing log messages have been
  broken out to a separate text files view.  You can flip between the
  log view and the text file view with the `t` hotkey.  When viewing
  text files, the `f` hotkey will switch between files.
* Files compressed with bzip2 are recognized and decompressed on the
  fly.
* Added a "session" file and command for storing commands that should
  be executed on startup.  For example, if you always want some
  highlighting to be done, you can add that command to the session
  file.

Fixes:
* Add some more log file formats for generic log files.
* Performance improvements for compressed files.
* Works on OS X now.

## lnav v0.3.0

Changes:
* The hotkey for the SQL view was changed to `v` and `V` from '.'.

Features:
* You can now switch between the SQL result view and the log view while
  keeping the top of the views in sync with the `log_line` column.

Fixes:
* The `log_line` column is no longer included in the SQL result view's
  stacked bar graph.
* Added a "warnings" count to the histogram view.
