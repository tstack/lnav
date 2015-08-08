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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "pcrepp.hh"

int main(int argc, char *argv[])
{
    pcre_context_static<30> context;
    int retval = EXIT_SUCCESS;
    
    {
        pcrepp nomatch("nothing-to-match");
        pcre_input pi("dummy");

        assert(!nomatch.match(context, pi));
    }

    {
        pcrepp match1("(\\w*)=(\\d+)");
        pcre_input pi("a=1  b=2");
        pcre_context::capture_t *cap;

        assert(match1.match(context, pi));

        cap = context.all();
        assert(cap->c_begin == 0);
        assert(cap->c_end == 3);

        assert((context.end() - context.begin()) == 2);
        assert(pi.get_substr(context.begin()) == "a");
        assert(pi.get_substr(context.begin() + 1) == "1");
        assert(pi.get_substr(context[1]) == "1");

        assert(match1.match(context, pi));
        assert((context.end() - context.begin()) == 2);
        assert(pi.get_substr(context.begin()) == "b");
        assert(pi.get_substr(context.begin() + 1) == "2");
    }

    {
        pcrepp match2("");
    }

    {
        pcrepp match3("(?<var1>\\d+)(?<var2>\\w+)");
        pcre_named_capture::iterator iter;
        const char *expected_names[] = {
            "var1",
            "var2",
        };
        int index = 0;

        for (iter = match3.named_begin();
             iter != match3.named_end();
             ++iter, index++) {
            assert(strcmp(iter->pnc_name, expected_names[index]) == 0);
        }

        assert(match3.name_index("var2") == 1);

        pcre_input pi("123foo");

        match3.match(context, pi);
        assert(pi.get_substr(context["var1"]) == "123");
    }

    {
        pcre_context::capture cap(1, 4);
        pcre_input pi("\0foo", 0, 4);

        assert("foo" == pi.get_substr(&cap));
    }

    const char *empty_cap_regexes[] = {
            "foo (?:bar)",
            "foo [(]",
            "foo \\Q(bar)\\E",
            "(?i)",

            NULL
    };

    for (int lpc = 0; empty_cap_regexes[lpc]; lpc++) {
        pcrepp re(empty_cap_regexes[lpc]);

        assert(re.captures().empty());
    }

    {
        pcrepp re("foo (bar (?:baz)?)");

        assert(re.captures().size() == 1);
        assert(re.captures()[0].c_begin == 4);
        assert(re.captures()[0].c_end == 18);
        assert(re.captures()[0].length() == 14);
    }

    {
        pcrepp re("(a)(b)(c)");

        assert(re.captures().size() == 3);
        assert(re.captures()[0].c_begin == 0);
        assert(re.captures()[0].c_end == 3);
        assert(re.captures()[1].c_begin == 3);
        assert(re.captures()[1].c_end == 6);
        assert(re.captures()[2].c_begin == 6);
        assert(re.captures()[2].c_end == 9);
    }

    {
        pcrepp re("\\(a\\)(b)");

        assert(re.captures().size() == 1);
        assert(re.captures()[0].c_begin == 5);
        assert(re.captures()[0].c_end == 8);
    }

    {
        pcrepp re("(?<named>b)");

        assert(re.captures().size() == 1);
        assert(re.captures()[0].c_begin == 0);
        assert(re.captures()[0].c_end == 11);
    }

    {
        pcrepp re("(?P<named>b)");

        assert(re.captures().size() == 1);
        assert(re.captures()[0].c_begin == 0);
        assert(re.captures()[0].c_end == 12);
    }

    {
        pcrepp re("(?'named'b)");

        assert(re.captures().size() == 1);
        assert(re.captures()[0].c_begin == 0);
        assert(re.captures()[0].c_end == 11);
    }

    return retval;
}
