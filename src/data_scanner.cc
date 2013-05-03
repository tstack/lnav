/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include "pcrepp.hh"
#include "data_scanner.hh"

using namespace std;

static struct {
    const char *name;
    pcrepp pcre;
} MATCHERS[DT_TERMINAL_MAX] = {
    { "url", pcrepp("([\\w]+://[^\\s'\"\\[\\](){}]+)"), },
    { "path", pcrepp("(?<![\\w\\d-_])((?:/|\\./|\\.\\./)[\\w\\d\\.-_\\~/]+)"), },
    { "time", pcrepp("\\b(\\d?\\d:\\d\\d(:\\d\\d)?(:\\d\\d)?([,.]\\d{3})?)\\b"), }, // XXX be more specific
    { "mac", pcrepp("([0-9a-fA-F][0-9a-fA-F](?::[0-9a-fA-F][0-9a-fA-F]){5,5})"), },
    { "quot", pcrepp("u?\"([^\"]+)\"|u'([^']+(?:'\\w[^']*)*)'"), },
    // { "qual", pcrepp("([^\\s:=]+:[^\\s:=,]+(?!,)(?::[^\\s:=,]+)*)"), },
    
    { "sep", pcrepp("(:|=)"), },
    { "comm", pcrepp("(,|/)"), },

    { "ipv4", pcrepp("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"), },

    { "vers", pcrepp("([0-9]+(?:\\.[0-9]+){2,}\\b)"), },
    { "oct", pcrepp("(-?0[0-7]+\\b)"), },
    { "pcnt", pcrepp("(-?[0-9]+(\\.[0-9]+)?[ ]*%\\b)"), },
    { "num", pcrepp("(-?[0-9]+(\\.[0-9]+)?([eE][-+][0-9]+)?\\b)"), },
    { "hex", pcrepp("(-?(?:0x|[0-9])[0-9a-fA-F]+\\b)"), },
    
    { "word", pcrepp("([^\"';\\s:=,/(){}\\[\\]]+)"), },
    { "line", pcrepp("(\r?\n|\r|;)"), },
    { "whit", pcrepp("([ \r\t]+)"), },
    { "dot", pcrepp("(\\.)"), },
     
    { "gbg", pcrepp("(.)"), },
};

const char *DNT_NAMES[] = {
    "key",
    "pair",
    "val",
    "row",
    "unit",
    "meas",
    "var",
    "rang",
    "date",
};

const char *data_scanner::token2name(data_token_t token)
{
    if (token < 0)
	return "inv";
    else if (token < DT_TERMINAL_MAX)
	return MATCHERS[token].name;
    else if (token == DT_ANY)
	return "any";
    else
	return DNT_NAMES[token - DNT_KEY];
}

bool data_scanner::tokenize(pcre_context &pc, data_token_t &token_out)
{
    int lpc;

    token_out = data_token_t(-1);
    
    if (this->ds_pcre_input.pi_next_offset > this->ds_pcre_input.pi_length) {
	return false;
    }
    else if (this->ds_pcre_input.pi_next_offset ==
	     this->ds_pcre_input.pi_length) {
	this->ds_pcre_input.pi_next_offset += 1;
	token_out = DT_LINE;
	
	return true;
    }

    for (lpc = 0; lpc < DT_TERMINAL_MAX; lpc++) {
	if (MATCHERS[lpc].pcre.match(pc, this->ds_pcre_input, PCRE_ANCHORED)) {
	    token_out = data_token_t(lpc);
	    break;
	}
    }

    assert((0 <= token_out && token_out < DT_TERMINAL_MAX));

    return true;
}
