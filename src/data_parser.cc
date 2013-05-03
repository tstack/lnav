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

#include "data_parser.hh"

using namespace std;

static data_token_t PATTERN_KEY[] = {
    DT_STRING,
    DT_NUMBER,
    DT_HEX_NUMBER,
    // DT_QUALIFIED_NAME,
};

static data_token_t UPTO_SEPARATOR[] = {
    DT_SEPARATOR,
    DT_LINE,
};

static data_token_t UPTO_NT[] = {
    DNT_PAIR,
    DNT_ROW,
    DT_SEPARATOR,
};

static data_token_t PATTERN_PAIR[] = {
    DNT_ROW,
    DT_SEPARATOR,
    // DNT_KEY,
};

static data_token_t PATTERN_ROW[] = {
    DT_ANY,
    DT_COMMA,
    DNT_ROW,
};

static data_token_t PATTERN_DATE_TIME[] = {
    DT_TIME,
    DT_NUMBER,
    DT_STRING,
};

static data_token_t PATTERN_QUAL[] = {
    DNT_KEY,
    DT_SEPARATOR,
    DNT_KEY,
};

bool data_parser::reducePattern(std::list<element> &reduction,
				const data_token_t *pattern_start,
				const data_token_t *pattern_end,
				bool repeating)
{
    size_t pattern_size = (pattern_end - pattern_start);
    bool found, retval = false;
    
    reduction.clear();
    
    do {
	found = false;
	if (pattern_size <= this->dp_stack.size() &&
	    std::equal(pattern_start, pattern_end,
		       this->dp_stack.begin(),
		       element_cmp())) {
	    std::list<element>::iterator match_end = this->dp_stack.begin();
	    
	    advance(match_end, pattern_size);
	    reduction.splice(reduction.end(),
			     this->dp_stack,
			     this->dp_stack.begin(),
			     match_end);
	    
	    retval = found = true;
	}
    } while (found && repeating);
    
    reduction.reverse();
    
    return retval;
}

void data_parser::reduceQual(const struct element &lookahead)
{
    std::list<element> reduction;
    
    if (this->reducePattern(reduction,
			    PATTERN_QUAL,
			    PATTERN_QUAL +
			    sizeof(PATTERN_QUAL) / sizeof(data_token_t))) {
	// printf("qual hit\n");
	this->dp_qual.splice(this->dp_qual.end(), reduction);
    }
}

void data_parser::reduceRow(void)
{
    std::list<element> reduction;

    if (this->reducePattern(reduction,
			    PATTERN_ROW,
			    PATTERN_ROW +
			    sizeof(PATTERN_ROW) / sizeof(data_token_t))) {
	std::list<element>::iterator match_end;

	if (reduction.back().e_sub_elements != NULL)
	    reduction.front().assign_elements(*reduction.back().e_sub_elements);
	else
	    reduction.front().e_sub_elements->push_back(reduction.back());
	reduction.front().update_capture();
	match_end = reduction.begin();
	++match_end;
	this->dp_stack.splice(this->dp_stack.begin(),
			      reduction,
			      reduction.begin(),
			      match_end);
    }
}

void data_parser::reducePair(void)
{
    std::list<element> reduction;

    if (this->reducePattern(reduction,
			    PATTERN_DATE_TIME,
			    PATTERN_DATE_TIME +
			    sizeof(PATTERN_DATE_TIME) / sizeof(data_token_t))) {
	this->dp_stack.push_front(element(reduction, DNT_DATE_TIME));
	this->dp_stack.front().assign_elements(reduction);
    }
    
    this->reduceRow();
    if (this->reduceUpTo(reduction,
			 UPTO_SEPARATOR,
			 UPTO_SEPARATOR +
			 sizeof(UPTO_SEPARATOR) / sizeof(data_token_t)) &&
	!reduction.empty()) {
	if (reduction.front().e_token == DNT_ROW) {
	    reduction.reverse();
	    this->dp_stack.splice(this->dp_stack.begin(), reduction);
	}
	else {
	    this->dp_stack.push_front(element(reduction, DNT_ROW));
	    this->dp_stack.front().assign_elements(reduction);
	}
    }

    if (this->reducePattern(reduction,
			    PATTERN_PAIR,
			    PATTERN_PAIR +
			    sizeof(PATTERN_PAIR) / sizeof(data_token_t))) {
	if (this->dp_qual.empty()) {
	    this->dp_stack.splice(this->dp_stack.begin(), reduction);
	}
	else {
	    reduction.push_front(this->dp_qual.back());
	    this->dp_qual.pop_back();
	    this->dp_stack.push_front(element(reduction, DNT_PAIR));
	    this->dp_stack.front().assign_elements(reduction);
	}
    }
    // this->print(stdout);
}

#define DEB 0

void data_parser::reduce(const element &lookahead)
{
    std::list<element> reduction;
    bool push_lookahead = true;
    
    switch (lookahead.e_token) {
    case DT_INVALID:
    case DT_WHITE:
	this->reducePair();
	push_lookahead = false;
	break;

    case DT_GARBAGE:
	push_lookahead = false;
	break;

    case DT_LINE:
	this->reduceRow();
	if (!this->reduceUpTo(reduction,
			      UPTO_NT,
			      UPTO_NT +
			      sizeof(UPTO_NT) / sizeof(data_token_t))) {
	    reduction.splice(reduction.begin(), this->dp_stack);
	    reduction.reverse();
	}
	if (!reduction.empty()) {
	    if (this->dp_stack.front().e_token == DNT_ROW) {
		this->dp_stack.front().assign_elements(reduction);
	    }
	    else if (this->dp_stack.front().e_token == DNT_PAIR) {
		this->dp_stack.front().e_sub_elements->back().assign_elements(reduction);
	    }
	    else {
		this->dp_stack.push_front(element(reduction, DNT_ROW));
		this->dp_stack.front().assign_elements(reduction);
	    }
	}

	this->reducePair();
	push_lookahead = false;
	break;

    case DT_COMMA:
	this->reduceRow();
	
	if (!this->dp_stack.empty() &&
	    this->dp_stack.front().e_token != DNT_ROW) {
	    if (this->dp_stack.front().e_token == DT_SEPARATOR) {
		push_lookahead = false;
	    }
	    else if (this->dp_stack.front().e_token == DNT_PAIR) {
		std::list<element>::iterator pair_iter = this->dp_stack.begin();

		this->dp_qual.push_front(this->dp_stack.front().e_sub_elements->front());
		this->dp_stack.front().e_sub_elements->pop_front();
		this->dp_stack.front().e_sub_elements->reverse();
		this->dp_stack.splice(this->dp_stack.begin(),
				      *this->dp_stack.front().e_sub_elements);
		this->dp_stack.erase(pair_iter);
	    }
	    else {
		std::list<element>::iterator next_elem = this->dp_stack.begin();

		advance(next_elem, 1);
		reduction.splice(reduction.end(),
				 this->dp_stack,
				 this->dp_stack.begin(),
				 next_elem);
		this->dp_stack.push_front(element(reduction, DNT_ROW));
		this->dp_stack.front().assign_elements(reduction);
	    }
	}
	break;
	
    case DT_SEPARATOR:
	if (this->reduceAnyOf(reduction,
			      PATTERN_KEY,
			      PATTERN_KEY +
			      sizeof(PATTERN_KEY) / sizeof(data_token_t))) {
	    this->reducePair();
	    if (this->dp_stack.front().e_token == DT_SEPARATOR)
		this->dp_stack.pop_front();
	    this->dp_qual.push_back(element(reduction, DNT_KEY));
	    // this->reduceQual(lookahead);
	}
	break;

    default:
	break;
    }

    if (push_lookahead) {
	this->dp_stack.push_front(lookahead);
    }

    // this->print(stdout);
}
