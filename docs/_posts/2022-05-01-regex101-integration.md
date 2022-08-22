---
layout: post
title: Integration with regex101.com
excerpt: Create/edit format files using regex101.com
---

*(This change will be in the upcoming v0.11.0 release)*

Creating and updating format files for **lnav** can be a bit tedious and
error-prone. To help streamline the process, an integration with regex101.com
has been added. Now, you can create regular expressions for plaintext log
files on https://regex101.com and then create a skeleton format file with a
simple command. If you already have a format file that needs to be updated,
you can push the regexes up to regex101, edit them with their interface, and
then pull the changes back down as a format patch file.

To further improve the experience of developing with format files, there is
also work underway to improve error messages. Many messages should be clearer,
more context is provided, and they should look nicer as well. For example, the
following error is displayed when a format regex is not valid:

![Screenshot of an error message](/assets/images/lnav-invalid-regex-error.png)

## Management CLI

The regex101 integration can be accessed through the new "management-mode CLI".
This mode can be accessed by passing `-m` as the first option to **lnav**. The
management CLI is organized as a series of nested commands. If you're not sure
what to do at a given level, run the command as-is and the CLI should print out
help text to guide you through the hierarchy of commands and required
parameters.

### Create a format from a regular expression

The `regex101 import` command can be used to import a regular expression from
regex101.com and create or patch a format file. The command takes the URL of
the regex, the format name, and the name of the regex in the log format (
defaults to "std" if not given). For example, the following command can be used
to import the regex at "https://regex101.com/r/zpEnjV/2" into the format named "
re101_example_log":

```console
$ lnav -m regex101 import https://regex101.com/r/zpEnjV/2 re101_example_log
```

If the import was successful, the path to the skeleton format file will be
printed. You will most likely need to edit the file to fill in more details
about your log format.

### Editing an existing regular expression

If you have a log format with a regex that needs to be updated, you can push
the regex to regex101.com for editing with a command like (replace
"myformat_log"/"std" with the name of your format and regex):

```console
$ lnav -m format myformat_log regex std regex101 push
```

Along with the regex, the format's samples will be added to the entry to ensure
changes won't break existing matches. If the push was successful, the URL for
the new regex101.com entry will be printed out. You can use that URL to edit the
regex to your needs. Once you're done editing the regex, you can pull the
changes down to a "patch" file using the following command:

```console
$ lnav -m format myformat_log regex std regex101 pull
```

The patch file will be evaluated after the original format file and override
the values from the original. Once you are satisfied with the changes, you
can move the contents of the patch file to the original file and delete the
patch.
