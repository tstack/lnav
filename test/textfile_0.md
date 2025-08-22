{
"comment": "This is JSON front-matter"
}

# Table of Contents

- [Table of Contents](#table-of-contents)
    - [Test](#test)
    - [Github Alerts](#github-alerts)
    - [Table][1]

[1]: #table

## Test

* One
* Two
* Three

<img src="../docs/lnav-tui.png" />

<img src="../docs/lnav-architecture.png" alt="The internal architecture of lnav" />

<span style="color: #f00; font-weight: bold">Bold red</span>

~~Strikethrough~~

*italic*

**bold**

_underline_

<span style="text-decoration: underline; background-color: darkblue">
Underline</span>

<pre>
  Hello,
  <span class="name">World</span>!
</pre>

Goodbye, <span style="border-left: solid cyan; border-right: dashed green">
World</span>!

Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.

<span style="white-space: nowrap">Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.</span>

<span style="white-space: nowrap"><span style="color: green">Loooooooonnnnnnnggggg looooonnnnnnggggg maaaaaannnnnn</span></span>

<a name="custanchor"><span style="color: green">Loooooooonnnnnnnggggg looooonnnnnnggggg maaaaaannnnnn</span></a>

<span style="color: green">
**Nested Markdown**
</span>

```foolang
foo bar bar
baz "xyz"
```

```c
/*
 * This program prints "Hello, World!"
 */

#include <stdio.h>

int main() {
    printf("Hello, World!\n");
}
```

```python
def hw(name):
    """
    This function prints "Hello, <name>!"
    """

    print(f"Hello, {name}!")  # test comment
```

```xml
<?xml version="1.0" encoding="utf-8" ?>
<books>
    <!-- Line comment -->
    <book id="100">
        <author>Finnegan</author>
    </book>
</books>
```

~~~lnav
;SELECT * FROM syslog_log

:filter-out spam
~~~

## Github Alerts

> [!NOTE]
> Useful information that users should know, even when skimming content.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know to achieve their goal.

> [!WARNING]
> Urgent info that needs immediate user attention to avoid problems.

> [!CAUTION]
> Advises about risks or negative outcomes of certain actions.

## Blockquotes

> > He said
> She said

## Tasks

* [x] Bibimbap
* [x] Waffles
* [ ] Tacos

## Table

|  ID |    Name     | Description   |
|----:|:-----------:|---------------|
|   1 |     One     | The first     |
|   2 |     Two     | The second    |
|   3 |    Three    | The third     |
|   4 |    Four     | The fourth    |
|  .. |     ..      | ..            |
| 100 | One Hundred | The hundredth |

| abc        | def |
|------------|-----|
| foo \| bar | ddd |

<table>
<tr>
<th>
Foo
</th>
<td>
Bar
</td>
</tr>
<tr>
<th>
Foo
</th>
<td>
Bar
</td>
</tr>
</table>
