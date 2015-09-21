/**
 * Copyright (c) 2015, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "data_scanner.hh"

bool data_scanner::tokenize2(pcre_context &pc, data_token_t &token_out)
{
#   define YYCTYPE char
#   define CAPTURE(tok) { \
        pi.pi_next_offset = YYCURSOR.val - pi.get_string(); \
        cap[0].c_end = pi.pi_next_offset; \
        cap[1].c_end = pi.pi_next_offset; \
        token_out = tok; \
    }
#   define RET(tok) { \
        CAPTURE(tok); \
        return true; \
    }
    pcre_input &pi = this->ds_pcre_input;
    struct _YYCURSOR {
        const YYCTYPE operator*() const {
            if (this->val < this->lim) {
                return *val;
            }
            return '\0';
        }

        operator const YYCTYPE *() const {
            return this->val;
        }

        const YYCTYPE *operator=(const YYCTYPE *rhs) {
            this->val = rhs;
            return rhs;
        }

        const YYCTYPE *operator+(int rhs) {
            return this->val + rhs;
        }

        _YYCURSOR& operator++() {
            this->val += 1;
            return *this;
        }

        const YYCTYPE *val;
        const YYCTYPE *lim;
    } YYCURSOR;
    YYCURSOR = pi.get_string() + pi.pi_next_offset;
    const YYCTYPE *YYLIMIT = pi.get_string() + pi.pi_length;
    const YYCTYPE *YYMARKER = YYCURSOR;
    const YYCTYPE *YYCTXMARKER = YYCURSOR;
    pcre_context::capture_t *cap = pc.all();

    YYCURSOR.lim = YYLIMIT;

    pc.set_count(2);
    cap[0].c_begin = pi.pi_next_offset;
    cap[1].c_begin = pi.pi_next_offset;

    /*!re2c
       re2c:yyfill:enable = 0;

       SPACE = [ \t\r\n];
       ALPHA = [a-zA-Z];
       NUM = [0-9];
       ALPHANUM = [a-zA-Z0-9_];
       EOF = "\x00";
       IPV4SEG  = ("25"[0-5]|("2"[0-4]|"1"{0,1}[0-9]){0,1}[0-9]);
       IPV4ADDR = (IPV4SEG"."){3,3}IPV4SEG;
       IPV6SEG  = [0-9a-fA-F]{1,4};
       IPV6ADDR = (
                  (IPV6SEG":"){7,7}IPV6SEG|
                  (IPV6SEG":"){1,7}":"|
                  (IPV6SEG":"){1,6}":"IPV6SEG|
                  (IPV6SEG":"){1,5}(":"IPV6SEG){1,2}|
                  (IPV6SEG":"){1,4}(":"IPV6SEG){1,3}|
                  (IPV6SEG":"){1,3}(":"IPV6SEG){1,4}|
                  (IPV6SEG":"){1,2}(":"IPV6SEG){1,5}|
                  IPV6SEG":"((":"IPV6SEG){1,6})|
                  ":"((":"IPV6SEG){1,7}|":")|
                  [a-fA-F0-9]{4}":"(":"IPV6SEG){0,4}"%"[0-9a-zA-Z]{1,}|
                  "::"('ffff'(":0"{1,4}){0,1}":"){0,1}IPV4ADDR|
                  (IPV6SEG":"){1,4}":"IPV4ADDR
                  );

       EOF { return false; }

       ("u"|"r")?'"'('\\'.|'""'|[^\x00\"])*'"' {
           CAPTURE(DT_QUOTED_STRING);
           switch (pi.get_string()[cap[1].c_begin]) {
           case 'u':
           case 'r':
               cap[1].c_begin += 1;
               break;
           }
           cap[1].c_begin += 1;
           cap[1].c_end -= 1;
           return true;
       }
       ("u"|"r")?"'"('\\'.|"''"|[^\x00\'])*"'" {
           CAPTURE(DT_QUOTED_STRING);
           switch (pi.get_string()[cap[1].c_begin]) {
           case 'u':
           case 'r':
               cap[1].c_begin += 1;
               break;
           }
           cap[1].c_begin += 1;
           cap[1].c_end -= 1;
           return true;
       }
       [a-zA-Z0-9]+"://"[^\x00\r\n\t '"\[\](){}]+[/a-zA-Z0-9\-=&?%] { RET(DT_URL); }
       ("/"|"./"|"../")[a-zA-Z0-9_\.\-\~/!@#$%^&*()]* { RET(DT_PATH); }
       (SPACE|NUM)NUM":"NUM{2}/[^:] { RET(DT_TIME); }
       (SPACE|NUM)NUM":"NUM{2}":"NUM{2}("."NUM{3,6})?/[^:] { RET(DT_TIME); }
       [0-9a-fA-F][0-9a-fA-F](":"[0-9a-fA-F][0-9a-fA-F])+ {
           if ((YYCURSOR - pi.get_string()) == 17) {
               RET(DT_MAC_ADDRESS);
           } else {
               RET(DT_HEX_DUMP);
           }
       }
       (NUM{4}"/"NUM{1,2}"/"NUM{1,2}|NUM{4}"-"NUM{1,2}"-"NUM{1,2}|NUM{2}"/"ALPHA{3}"/"NUM{4})"T"? {
           RET(DT_DATE);
       }
       IPV6ADDR/[^:a-zA-Z0-9] { RET(DT_IPV6_ADDRESS); }

       "<""?"?[a-zA-Z0-9_:]+SPACE*([a-zA-Z0-9_:]+(SPACE*'='SPACE*('"'(('\\'.|[^\x00"])+)'"'|"'"(('\\'.|[^\x00'])+)"'"|[^\x00>]+)))*SPACE*("/"|"?")">" {
           RET(DT_XML_EMPTY_TAG);
       }

       "<"[a-zA-Z0-9_:]+SPACE*([a-zA-Z0-9_:]+(SPACE*"="SPACE*('"'(('\\'.|[^\x00"])+)'"'|"'"(('\\'.|[^\x00'])+)"'"|[^\x00>]+)))*SPACE*">" {
           RET(DT_XML_OPEN_TAG);
       }

       "</"[a-zA-Z0-9:]+SPACE*">" {
           RET(DT_XML_CLOSE_TAG);
       }

       ":" { RET(DT_COLON); }
       "=" { RET(DT_EQUALS); }
       "," { RET(DT_COMMA); }
       ";" { RET(DT_SEMI); }
       "{" { RET(DT_LCURLY); }
       "}" { RET(DT_RCURLY); }
       "[" { RET(DT_LSQUARE); }
       "]" { RET(DT_RSQUARE); }
       "(" { RET(DT_LPAREN); }
       ")" { RET(DT_RPAREN); }
       "<" { RET(DT_LANGLE); }
       ">" { RET(DT_RANGLE); }

       IPV4ADDR/[^0-9] {
           RET(DT_IPV4_ADDRESS);
       }

       [0-9a-fA-F]{8}("-"[0-9a-fA-F]{4}){3}"-"[0-9a-fA-F]{12} { RET(DT_UUID); }

       [0-9]"."[0-9]+'e'[\-\+][0-9]+ { RET(DT_NUMBER); }

       [0-9]+("."[0-9]+[a-zA-Z0-9_]*){2,}("-"[a-zA-Z0-9_]+)?|[0-9]+("."[0-9]+[a-zA-Z0-9_]*)+"-"[a-zA-Z0-9_]+ {
           RET(DT_VERSION_NUMBER);
       }

       "-"?"0"[0-7]+ { RET(DT_OCTAL_NUMBER); }
       "-"?[0-9]+("."[0-9]+)?[ ]*"%" { RET(DT_PERCENTAGE); }
       "-"?[0-9]+("."[0-9]+)?([eE][\-+][0-9]+)? { RET(DT_NUMBER); }
       "-"?("0x"|[0-9])[0-9a-fA-F]+ { RET(DT_HEX_NUMBER); }

       [a-zA-Z0-9\._%+-]+"@"[a-zA-Z0-9\.-]+"."[a-zA-Z]+ { RET(DT_EMAIL); }

       ("true"|"True"|"TRUE"|"false"|"False"|"FALSE"|"None"|"null"|"NULL") { RET(DT_CONSTANT); }
       ("re-")?[a-zA-Z][a-z']+/([\r\n\t \(\)!\*:;'\"\?,]|[\.\!,\?]SPACE|EOF) { RET(DT_WORD); }

       [^\x00"; \t\r\n:=,\(\)\{\}\[\]\+#!%\^&\*'\?<>\~`\|\\]+("::"[^\x00"; \r\n\t:=,\(\)\{\}\[\]\+#!%\^&\*'\?<>\~`\|\\]+)* {
           RET(DT_SYMBOL);
       }

       ("\r"?"\n"|"\n") { RET(DT_LINE); }
       SPACE+ { RET(DT_WHITE); }
       "." { RET(DT_DOT); }
       . { RET(DT_GARBAGE); }

     */
}
