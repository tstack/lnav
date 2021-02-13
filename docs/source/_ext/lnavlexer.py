
__all__ = ['LnavCommandLexer']

import re

from pygments.token import Whitespace, Text, Keyword, Literal
from pygments.lexers._mapping import LEXERS
from pygments.lexers.python import RegexLexer

class LnavCommandLexer(RegexLexer):
    name = 'lnav'

    flags = re.IGNORECASE
    tokens = {
        'root': [
            (r'\s+', Whitespace),
            (r':[\w\-]+', Keyword),
            (r'\<[\w\-]+\>', Literal.String.Doc),
            (r'.', Text),
        ]
    }

def setup(app):
    LEXERS['LnavCommandLexer'] = (
        '_ext.lnavlexer', 'lnav', ('lnav',), ('*.lnav',), ('text/lnav',))
    app.add_lexer('lnav', LnavCommandLexer)
