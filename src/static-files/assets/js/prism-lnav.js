/**
 * Prism language definition for the LNAV scripting language
 * https://lnav.org/
 *
 * This language extends Prism's built-in SQLite definition to support
 * LNAV scripting constructs such as:
 *  - Commands prefixed with ':'
 *  - Script inclusions ('|')
 *  - Variables like $1, $0, __all__
 *  - LNAV-style comments (#)
 *  - SQL statements prefixed with ';'
 *
 * @author ChatGPT
 * @language lnav
 * @alias lnav
 * @requires prism-sql
 */

(function (Prism) {
    if (!Prism.languages.sql) {
        console.warn('Prism: SQL language not found. Load prism-sql before prism-lnav.');
        return;
    }

    // Extend the SQL grammar
    Prism.languages.lnav = Prism.languages.extend('sql', {
        // Support LNAV comments (#) as well as SQL comments
        'comment': [
            {
                pattern: /^[ \t]*#.*/m,
                greedy: true
            },
            Prism.languages.sql.comment ? Prism.languages.sql.comment[0] : undefined
        ].filter(Boolean),

        // LNAV commands start with a colon, e.g. :filter-in, :eval
        'command': {
            pattern: /^:[a-zA-Z0-9_\-]+(?:\s+(?!^:)\S+)*/m,
            greedy: true,
            inside: {
                'keyword': /^:[a-zA-Z0-9_\-]+/,
                'argument': /\s+\S+/,
            },
            alias: 'function'
        },

        // Script inclusion lines (start with a pipe)
        'script-inclusion': {
            pattern: /^\|[^\r\n]*/m,
            alias: 'builtin'
        },

        // Search lines (start with a slash)
        'search': {
            pattern: /^\/.+/,
            alias: 'builtin'
        },

        // Variable substitution ($1, $0, LNAV_HOME_DIR, etc.)
        'variable': {
            pattern: /\$(?:[0-9]+|[a-zA-Z_][a-zA-Z0-9_]*|\{[^}]*})/,
            alias: 'variable'
        }
    });

    // Lines starting with ';' contain SQL — reuse SQLite highlighting
    Prism.languages.insertBefore('lnav', 'keyword', {
        'sql-prompt': {
            pattern: /^;[^\r\n]*/m,
            greedy: true,
            inside: Prism.languages.sql
        }
    });

    // Inline eval expressions (e.g. :eval $1 + 2)
    Prism.languages.insertBefore('lnav', 'variable', {
        'eval': {
            pattern: /:eval\s+/,
            alias: 'function',
            inside: {
                'keyword': /^:[a-zA-Z0-9_\-]+/,
            },
        }
    });

    // Aliases
    Prism.languages['lnav'] = Prism.languages.lnav;

})(Prism);
