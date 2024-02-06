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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "base/date_time_scanner.hh"
#include "config.h"
#include "data_scanner.hh"

/*!conditions:re2c*/

nonstd::optional<data_scanner::tokenize_result> data_scanner::tokenize2(text_format_t tf)
{
    data_token_t token_out = DT_INVALID;
    capture_t cap_all;
    capture_t cap_inner;
#   define YYCTYPE unsigned char
#   define CAPTURE(tok) { \
        if (YYCURSOR.val == EMPTY) { \
            this->ds_next_offset = this->ds_input.length(); \
        } else { \
            this->ds_next_offset = YYCURSOR.val - this->ds_input.udata(); \
        } \
        cap_all.c_end = this->ds_next_offset; \
        cap_inner.c_end = this->ds_next_offset; \
        token_out = tok; \
    }

#   define RET(tok) { \
        CAPTURE(tok); \
        return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()}; \
    }
    static const unsigned char *EMPTY = (const unsigned char *) "";

    struct _YYCURSOR {
        YYCTYPE operator*() const {
            if (this->val < this->lim) {
                return *val;
            }
            return '\0';
        }

        operator const YYCTYPE *() const {
            if (this->val < this->lim) {
                return this->val;
            }
            return EMPTY;
        }

        const YYCTYPE *operator=(const YYCTYPE *rhs) {
            this->val = rhs;
            return rhs;
        }

        const YYCTYPE *operator+(int rhs) {
            return this->val + rhs;
        }

        const _YYCURSOR *operator-=(int rhs) {
            this->val -= rhs;
            return this;
        }

        _YYCURSOR& operator++() {
            this->val += 1;
            return *this;
        }

        const YYCTYPE *val{nullptr};
        const YYCTYPE *lim{nullptr};
    } YYCURSOR;
    YYCURSOR = (const unsigned char *) this->ds_input.udata() + this->ds_next_offset;
    _YYCURSOR yyt1;
    _YYCURSOR yyt2;
    _YYCURSOR yyt3;
    _YYCURSOR yyt4;
    _YYCURSOR hunk_heading;
    const YYCTYPE *YYLIMIT = (const unsigned char *) this->ds_input.end();
    const YYCTYPE *YYMARKER = YYCURSOR;
    class _yycond {
    public:
        int operator()() const {
            return this->val;
        }

        void operator=(int v) {
            this->val = v;
        }

        int val{yycinit};
    } c;

    if (this->ds_bol) {
        c = yycbol;
    }
    this->ds_bol = false;
    if (this->ds_units) {
        c = yycunits;
    }
    this->ds_units = false;

    YYCURSOR.lim = YYLIMIT;

    cap_all.c_begin = this->ds_next_offset;
    cap_all.c_end = this->ds_next_offset;
    cap_inner.c_begin = this->ds_next_offset;
    cap_inner.c_end = this->ds_next_offset;

    /*!re2c
       re2c:yyfill:enable = 0;
       re2c:sentinel = 0;
       re2c:define:YYCTYPE        = uint8_t;
       re2c:define:YYGETCONDITION = "c";
       re2c:define:YYSETCONDITION = "c = @@;";
       re2c:tags = 1;

       SPACE = [ \t\r];
       ALPHA = [a-zA-Z];
       ESC = "\x1b";
       NUM = [0-9];
       ALPHANUM = [a-zA-Z0-9_];
       EOF = "";
       SYN = "\x16";
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
       UNITS = (([mup]?("s"|"S"))|(([kKmMgG]"i"?)?[bB])|("m"|"min"));

       <init, bol> EOF { return nonstd::nullopt; }
       <init, bol> [\x00] { return nonstd::nullopt; }
       <*> * { return nonstd::nullopt; }
       <init, bol> SYN+ {
           RET(DT_ZERO_WIDTH_SPACE);
       }

       <init, bol> ("f"|"u"|"r")?'"'('\\'[^\x00]|[^\x00\x16\x1b\n"\\]|'""')*'"' {
           CAPTURE(DT_QUOTED_STRING);
           switch (this->ds_input[cap_inner.c_begin]) {
           case 'f':
           case 'u':
           case 'r':
               cap_inner.c_begin += 1;
               break;
           }
           cap_inner.c_begin += 1;
           cap_inner.c_end -= 1;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }
       <init, bol> ("f"|"u"|"r")?'"""' {
           CAPTURE(DT_QUOTED_STRING);
           switch (this->ds_input[cap_inner.c_begin]) {
           case 'f':
           case 'u':
           case 'r':
               cap_inner.c_begin += 1;
               break;
           }
           cap_inner.c_begin += 3;
           goto yyc_dbldocstring;
       }

       <dbldocstring> ([\x00]|'"""') {
           CAPTURE(DT_QUOTED_STRING);
           cap_inner.c_end -= 3;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <dbldocstring> * {
           goto yyc_dbldocstring;
       }

       <init, bol> ("f"|"u"|"r")?"'''" {
           CAPTURE(DT_QUOTED_STRING);
           switch (this->ds_input[cap_inner.c_begin]) {
           case 'f':
           case 'u':
           case 'r':
               cap_inner.c_begin += 1;
               break;
           }
           cap_inner.c_begin += 3;
           goto yyc_sdocstring;
       }

       <sdocstring> ([\x00]|"'''") {
           CAPTURE(DT_QUOTED_STRING);
           cap_inner.c_end -= 3;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <sdocstring> * {
           goto yyc_sdocstring;
       }

       <init, bol> "/*" ([^\x00*]|"*"+[^\x00/])* "*"+ "/" {
           CAPTURE(DT_COMMENT);
           if (tf == text_format_t::TF_DIFF) {
               auto sf = this->to_string_fragment(cap_all);
               auto split_res = sf.split_when(string_fragment::tag1{'\n'});
               cap_all.c_end = split_res.first.sf_end;
               cap_inner.c_end = split_res.first.sf_end;
               this->ds_next_offset = cap_all.c_end;
           }
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }
       <init, bol> "<!--" ([^\x00*]|"-"+[^\x00>])* "-"{2,} ">" {
           CAPTURE(DT_COMMENT);
           if (tf == text_format_t::TF_DIFF) {
               auto sf = this->to_string_fragment(cap_all);
               auto split_res = sf.split_when(string_fragment::tag1{'\n'});
               cap_all.c_end = split_res.first.sf_end;
               cap_inner.c_end = split_res.first.sf_end;
               this->ds_next_offset = cap_all.c_end;
           }
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }
       <init, bol> "#[" "="* "[" ([^\x00\]]|"]" [^\x00=\]])* "]" "="* "]" {
           CAPTURE(DT_COMMENT);
           if (tf == text_format_t::TF_DIFF) {
               auto sf = this->to_string_fragment(cap_all);
               auto split_res = sf.split_when(string_fragment::tag1{'\n'});
               cap_all.c_end = split_res.first.sf_end;
               cap_inner.c_end = split_res.first.sf_end;
               this->ds_next_offset = cap_all.c_end;
           }
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <init, bol> ("f"|"u"|"r")?"'"('\\'[^\x00]|"''"|[^\x00\x16\x1b\n'\\])*"'"/[^sS] {
           CAPTURE(DT_QUOTED_STRING);
           if (tf == text_format_t::TF_RUST) {
               auto sf = this->to_string_fragment(cap_all);
               auto split_res = sf.split_when([](char ch) { return ch != '\'' && !isalnum(ch); });
               cap_all.c_end = split_res.first.sf_end - this->ds_input.sf_begin;
               cap_inner.c_end = split_res.first.sf_end - this->ds_input.sf_begin;
               this->ds_next_offset = cap_all.c_end;
               return tokenize_result{DT_SYMBOL, cap_all, cap_inner, this->ds_input.data()};
           }
           switch (this->ds_input[cap_inner.c_begin]) {
           case 'f':
           case 'u':
           case 'r':
               cap_inner.c_begin += 1;
               break;
           }
           cap_inner.c_begin += 1;
           cap_inner.c_end -= 1;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }
       <init, bol> [a-zA-Z0-9]+":/""/"?[^\x00\x16\x1b\r\n\t '"[\](){}]+[/a-zA-Z0-9\-=&?%] { RET(DT_URL); }
       <init, bol> ("/"|"./"|"../"|[A-Z]":\\"|"\\\\")("Program Files"(" (x86)")?)?[a-zA-Z0-9_\.\-\~/\\!@#$%^&*()]* { RET(DT_PATH); }
       <init, bol> (SPACE|NUM)NUM":"NUM{2}/[^:] { RET(DT_TIME); }
       <init, bol> (SPACE|NUM)NUM?":"NUM{2}":"NUM{2}("."NUM{3,6})?/[^:] { RET(DT_TIME); }
       <init, bol> [0-9a-fA-F][0-9a-fA-F]((":"|"-")[0-9a-fA-F][0-9a-fA-F])+ {
           if ((YYCURSOR.val - (this->ds_input.udata() + this->ds_next_offset)) == 17) {
               RET(DT_MAC_ADDRESS);
           } else {
               RET(DT_HEX_DUMP);
           }
       }
       <init, bol> (NUM{4}"/"NUM{1,2}"/"NUM{1,2}|NUM{4}"-"NUM{1,2}"-"NUM{1,2}|NUM{2}"/"ALPHA{3}"/"NUM{4})("T"|" ")NUM{2}":"NUM{2}(":"NUM{2}("."NUM{3,6})?)? {
           RET(DT_DATE_TIME);
       }
       <init, bol> ALPHA{3}("  "NUM|" "NUM{2})" "NUM{2}":"NUM{2}(":"NUM{2}("."NUM{3,6})?)? {
           RET(DT_DATE_TIME);
       }
       <init, bol> (NUM{4}"/"NUM{1,2}"/"NUM{1,2}|NUM{4}"-"NUM{1,2}"-"NUM{1,2}|NUM{2}"/"ALPHA{3}"/"NUM{4}) {
           RET(DT_DATE);
       }
       <init, bol> IPV6ADDR/(": "|[^:a-zA-Z0-9]) { RET(DT_IPV6_ADDRESS); }

       <init, bol> "<!"[a-zA-Z0-9_:\-]+SPACE*([a-zA-Z0-9_:\-]+(SPACE*'='SPACE*('"'(('\\'[^\x00]|[^\x00"\\])+)'"'|"'"(('\\'[^\x00]|[^\x00'\\])+)"'"|[^\x00>]+))?|SPACE*('"'(('\\'[^\x00]|[^\x00"\\])+)'"'|"'"(('\\'[^\x00]|[^\x00'\\])+)"'"))*SPACE*">" {
           RET(DT_XML_DECL_TAG);
       }

       <init, bol> "<""?"?[a-zA-Z0-9_:\-]+SPACE*([a-zA-Z0-9_:\-]+(SPACE*'='SPACE*('"'(('\\'[^\x00]|[^\x00"\\])+)'"'|"'"(('\\'[^\x00]|[^\x00'\\])+)"'"|[^\x00>]+))?)*SPACE*("/"|"?")">" {
           RET(DT_XML_EMPTY_TAG);
       }

       <init, bol> "<"[a-zA-Z0-9_:\-]+SPACE*([a-zA-Z0-9_:\-]+(SPACE*"="SPACE*('"'(('\\'[^\x00]|[^\x00"\\])+)'"'|"'"(('\\'[^\x00]|[^\x00'\\])+)"'"|[^\x00>]+))?)*SPACE*">" {
           RET(DT_XML_OPEN_TAG);
       }

       <init, bol> "</"[a-zA-Z0-9_:\-]+SPACE*">" {
           RET(DT_XML_CLOSE_TAG);
       }

       <bol> [A-Z][A-Z _\-0-9]+"\n" {
           CAPTURE(DT_H1);
           cap_inner.c_end -= 1;
           this->ds_bol = true;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <bol> "["[^\x00\n]+"]\n" {
           CAPTURE(DT_H1);
           cap_inner.c_end -= 1;
           this->ds_bol = true;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <bol> "diff --git "[^\x00\n]+"\n" {
           CAPTURE(DT_H1);
           cap_inner.c_end = cap_inner.c_begin;
           this->ds_bol = true;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <bol> "--- "[^\x00\n]+"\n+++ "[^\x00\n]+"\n" {
           CAPTURE(DT_DIFF_FILE_HEADER);
           cap_inner.c_end -= 1;
           this->ds_bol = true;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <init, bol> "@@ -"[0-9]+","[0-9]+" +"[0-9]+","[0-9]+" @@ " @hunk_heading ([^\x00\n]+)"\n" {
           CAPTURE(DT_DIFF_HUNK_HEADING);
           cap_inner.c_begin = hunk_heading.val - this->ds_input.udata();
           cap_inner.c_end -= 1;
           this->ds_bol = true;
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <init, bol> ESC"["[0-9=;?]*[a-zA-Z] {
           RET(DT_CSI);
       }

       <init, bol> ":" { RET(DT_COLON); }
       <init, bol> "=" { RET(DT_EQUALS); }
       <init, bol> "," { RET(DT_COMMA); }
       <init, bol> ";" { RET(DT_SEMI); }
       <init, bol> "--"/[^\-] { RET(DT_EMDASH); }
       <init, bol> "()" | "{}" | "[]" { RET(DT_EMPTY_CONTAINER); }
       <init, bol> "{" { RET(DT_LCURLY); }
       <init, bol> "}" { RET(DT_RCURLY); }
       <init, bol> "[" { RET(DT_LSQUARE); }
       <init, bol> "]" { RET(DT_RSQUARE); }
       <init, bol> "(" { RET(DT_LPAREN); }
       <init, bol> ")" { RET(DT_RPAREN); }
       <init, bol> "<" { RET(DT_LANGLE); }
       <init, bol> ">" { RET(DT_RANGLE); }

       <init, bol> IPV4ADDR/[^0-9] {
           RET(DT_IPV4_ADDRESS);
       }

       <init, bol> [0-9a-fA-F]{8}("-"[0-9a-fA-F]{4}){3}"-"[0-9a-fA-F]{12} { RET(DT_UUID); }

       <init, bol> (NUM{4}" "NUM{4}" "NUM{4}" "NUM{4}|NUM{16})/[^0-9] {
           CAPTURE(DT_CREDIT_CARD_NUMBER);
           if (!this->is_credit_card(this->to_string_fragment(cap_all))) {
               if (cap_all.length() > 16) {
                   cap_all.c_end = cap_all.c_begin + 4;
                   cap_inner.c_end = cap_inner.c_begin + 4;
               }
               this->ds_next_offset = cap_all.c_end;
               token_out = DT_HEX_DUMP;
           }
           return tokenize_result{token_out, cap_all, cap_inner, this->ds_input.data()};
       }

       <init, bol> ("-"|"+")?[0-9]"."[0-9]+([eE][\-\+][0-9]+)?UNITS? {
           CAPTURE(DT_NUMBER);
           auto sf = this->to_string_fragment(cap_all);
           if (isalpha(sf.back())) {
               while (isalpha(sf.back())) {
                   sf.pop_back();
               }
               cap_all.c_end = sf.sf_end - this->ds_input.sf_begin;
               cap_inner.c_end = sf.sf_end - this->ds_input.sf_begin;
               this->ds_next_offset = cap_all.c_end;
               this->ds_units = true;
           }
           return tokenize_result{DT_NUMBER, cap_all, cap_inner, this->ds_input.data()};
       }

       <init, bol> [0-9]+("."[0-9]+[a-zA-Z0-9_]*){2,}("-"[a-zA-Z0-9_]+)?|[0-9]+("."[0-9]+[a-zA-Z0-9_]*)+"-"[a-zA-Z0-9_]+ {
           RET(DT_VERSION_NUMBER);
       }

       <units> UNITS {
           RET(DT_UNIT);
       }

       <init, bol> "-"?"0"[0-7]+ { RET(DT_OCTAL_NUMBER); }
       <init, bol> "-"?[0-9]+("."[0-9]+)?[ ]*"%" { RET(DT_PERCENTAGE); }
       <init, bol> ("0"|("-"|"+")?[1-9][0-9]*("."[0-9]+)?([eE][\-+][0-9]+)?)UNITS? {
           CAPTURE(DT_NUMBER);
           auto sf = this->to_string_fragment(cap_all);
           if (isalpha(sf.back())) {
               while (isalpha(sf.back())) {
                   sf.pop_back();
               }
               cap_all.c_end = sf.sf_end - this->ds_input.sf_begin;
               cap_inner.c_end = sf.sf_end - this->ds_input.sf_begin;
               this->ds_next_offset = cap_all.c_end;
               this->ds_units = true;
           }
           return tokenize_result{DT_NUMBER, cap_all, cap_inner, this->ds_input.data()};
       }
       <init, bol> "-"?("0x"|[0-9])[0-9a-fA-F]+ { RET(DT_HEX_NUMBER); }

       <init, bol> [a-zA-Z0-9\._%+-]+"@"[a-zA-Z0-9\.-]+"."[a-zA-Z]+ { RET(DT_EMAIL); }

       <init, bol> "true"|"True"|"TRUE"|"false"|"False"|"FALSE"|"None"|"null"|"NULL"/([\r\n\t \(\)!\*:;'\"\?,]|[\.\!,\?]SPACE|EOF) { RET(DT_CONSTANT); }

       <init, bol> ("re-")?[a-zA-Z][a-z']+/([\r\n\t \(\)!\*:;'\"\?,]|[\.\!,\?]SPACE|EOF) { RET(DT_WORD); }

       <init, bol> [aAI] { RET(DT_WORD); }

       <init, bol> ("--"|"++")[a-zA-Z0-9]+("-"[a-zA-Z0-9]+)* {
           RET(DT_SYMBOL);
       }

       <init, bol> ("-"|"+")[a-zA-Z0-9]+/[\x00 \t\r\n] {
           RET(DT_SYMBOL);
       }

       <init, bol> [a-zA-Z_][a-zA-Z0-9_]*(("::"|".")[a-zA-Z_0-9\-]+)* {
           RET(DT_SYMBOL);
       }

       <init, bol> [a-zA-Z0-9_]+(("::"|"."|"-"|"@"|"/")[a-zA-Z0-9_]+)* {
           RET(DT_ID);
       }

       <init, bol> ("\r"?"\n"|"\\n") {
           this->ds_bol = true;
           RET(DT_LINE);
       }
       <init, bol> SPACE+ { RET(DT_WHITE); }
       <init, bol> "." { RET(DT_DOT); }
       <init, bol> "\\". { RET(DT_ESCAPED_CHAR); }
       <init, bol> . { RET(DT_GARBAGE); }

     */

    return nonstd::nullopt;
}
