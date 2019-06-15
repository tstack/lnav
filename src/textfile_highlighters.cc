/**
 * Copyright (c) 2018, Timothy Stack
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

#include <string>

#include "textfile_highlighters.hh"

using namespace std;

static pcre *xpcre_compile(const char *pattern, int options = 0)
{
    const char *errptr;
    pcre *      retval;
    int         eoff;

    if ((retval = pcre_compile(pattern,
                               options,
                               &errptr,
                               &eoff,
                               NULL)) == NULL) {
        fprintf(stderr, "internal error: failed to compile -- %s\n", pattern);
        fprintf(stderr, "internal error: %s\n", errptr);

        exit(1);
    }

    return retval;
}

static highlighter static_highlighter(const string &regex) {
    return highlighter(xpcre_compile(regex.c_str()))
        .with_attrs(view_colors::singleton().attrs_for_ident(regex));
}

void setup_highlights(textview_curses::highlight_map_t &hm)
{
    hm["$python"] = highlighter(xpcre_compile(
        "(?:"
        "\\bFalse\\b|"
        "\\bNone\\b|"
        "\\bTrue\\b|"
        "\\band\\b|"
        "\\bas\\b|"
        "\\bassert\\b|"
        "\\bbreak\\b|"
        "\\bclass\\b|"
        "\\bcontinue\\b|"
        "\\bdef\\b|"
        "\\bdel\\b|"
        "\\belif\\b|"
        "\\belse\\b|"
        "\\bexcept\\b|"
        "\\bfinally\\b|"
        "\\bfor\\b|"
        "\\bfrom\\b|"
        "\\bglobal\\b|"
        "\\bif\\b|"
        "\\bimport\\b|"
        "\\bin\\b|"
        "\\bis\\b|"
        "\\blambda\\b|"
        "\\bnonlocal\\b|"
        "\\bnot\\b|"
        "\\bor\\b|"
        "\\bpass\\b|"
        "\\bprint\\b|"
        "\\braise\\b|"
        "\\breturn\\b|"
        "\\btry\\b|"
        "\\bwhile\\b|"
        "\\bwith\\b|"
        "\\byield\\b"
        ")"))
        .with_text_format(text_format_t::TF_PYTHON)
        .with_role(view_colors::VCR_KEYWORD);

    hm["$clike"] = highlighter(xpcre_compile(
        "(?:"
        "\\babstract\\b|"
        "\\bassert\\b|"
        "\\basm\\b|"
        "\\bauto\\b|"
        "\\bbool\\b|"
        "\\bbooleanif\\b|"
        "\\bbreak\\b|"
        "\\bbyte\\b|"
        "\\bcase\\b|"
        "\\bcatch\\b|"
        "\\bchar\\b|"
        "\\bclass\\b|"
        "\\bconst\\b|"
        "\\bconst_cast\\b|"
        "\\bcontinue\\b|"
        "\\bdefault\\b|"
        "\\bdelete\\b|"
        "\\bdo\\b|"
        "\\bdouble\\b|"
        "\\bdynamic_cast\\b|"
        "\\belse\\b|"
        "\\benum\\b|"
        "\\bextends\\b|"
        "\\bextern\\b|"
        "\\bfalse\\b|"
        "\\bfinal\\b|"
        "\\bfinally\\b|"
        "\\bfloat\\b|"
        "\\bfor\\b|"
        "\\bfriend\\b|"
        "\\bgoto\\b|"
        "\\bif\\b|"
        "\\bimplements\\b|"
        "\\bimport\\b|"
        "\\binline\\b|"
        "\\binstanceof\\b|"
        "\\bint\\b|"
        "\\binterface\\b|"
        "\\blong\\b|"
        "\\bmutable\\b|"
        "\\bnamespace\\b|"
        "\\bnative\\b|"
        "\\bnew\\b|"
        "\\boperator\\b|"
        "\\bpackage\\b|"
        "\\bprivate\\b|"
        "\\bprotected\\b|"
        "\\bpublic\\b|"
        "\\breinterpret_cast\\b|"
        "\\bregister\\b|"
        "\\breturn\\b|"
        "\\bshort\\b|"
        "\\bsigned\\b|"
        "\\bsizeof\\b|"
        "\\bstatic\\b|"
        "\\bstatic_cast\\b|"
        "\\bstrictfp\\b|"
        "\\bstruct\\b|"
        "\\bsuper\\b|"
        "\\bswitch\\b|"
        "\\bsynchronized\\b|"
        "\\btemplate\\b|"
        "\\bthis\\b|"
        "\\bthrow\\b|"
        "\\bthrows\\b|"
        "\\btransient\\b|"
        "\\btry\\b|"
        "\\btrue\\b|"
        "\\btypedef\\b|"
        "\\btypeid\\b|"
        "\\bunion\\b|"
        "\\bunsigned\\b|"
        "\\busing\\b|"
        "\\bvirtual\\b|"
        "\\bvoid\\b|"
        "\\bvolatile\\b|"
        "\\bwchar_t\\b|"
        "\\bwhile\\b"
        ")"))
        .with_text_format(text_format_t::TF_C_LIKE)
        .with_role(view_colors::VCR_KEYWORD);

    hm["$sql"] = highlighter(xpcre_compile(
        "(?:"
        "\\bABORT\\b|"
        "\\bACTION\\b|"
        "\\bADD\\b|"
        "\\bAFTER\\b|"
        "\\bALL\\b|"
        "\\bALTER\\b|"
        "\\bANALYZE\\b|"
        "\\bAND\\b|"
        "\\bAS\\b|"
        "\\bASC\\b|"
        "\\bATTACH\\b|"
        "\\bAUTOINCREMENT\\b|"
        "\\bBEFORE\\b|"
        "\\bBEGIN\\b|"
        "\\bBETWEEN\\b|"
        "\\bBOOLEAN\\b|"
        "\\bBY\\b|"
        "\\bCASCADE\\b|"
        "\\bCASE\\b|"
        "\\bCAST\\b|"
        "\\bCHECK\\b|"
        "\\bCOLLATE\\b|"
        "\\bCOLUMN\\b|"
        "\\bCOMMIT\\b|"
        "\\bCONFLICT\\b|"
        "\\bCONSTRAINT\\b|"
        "\\bCREATE\\b|"
        "\\bCROSS\\b|"
        "\\bCURRENT_DATE\\b|"
        "\\bCURRENT_TIME\\b|"
        "\\bCURRENT_TIMESTAMP\\b|"
        "\\bDATABASE\\b|"
        "\\bDATETIME\\b|"
        "\\bDEFAULT\\b|"
        "\\bDEFERRABLE\\b|"
        "\\bDEFERRED\\b|"
        "\\bDELETE\\b|"
        "\\bDESC\\b|"
        "\\bDETACH\\b|"
        "\\bDISTINCT\\b|"
        "\\bDROP\\b|"
        "\\bEACH\\b|"
        "\\bELSE\\b|"
        "\\bEND\\b|"
        "\\bESCAPE\\b|"
        "\\bEXCEPT\\b|"
        "\\bEXCLUSIVE\\b|"
        "\\bEXISTS\\b|"
        "\\bEXPLAIN\\b|"
        "\\bFAIL\\b|"
        "\\bFLOAT\\b|"
        "\\bFOR\\b|"
        "\\bFOREIGN\\b|"
        "\\bFROM\\b|"
        "\\bFULL\\b|"
        "\\bGLOB\\b|"
        "\\bGROUP\\b|"
        "\\bHAVING\\b|"
        "\\bHIDDEN\\b|"
        "\\bIF\\b|"
        "\\bIGNORE\\b|"
        "\\bIMMEDIATE\\b|"
        "\\bIN\\b|"
        "\\bINDEX\\b|"
        "\\bINDEXED\\b|"
        "\\bINITIALLY\\b|"
        "\\bINNER\\b|"
        "\\bINSERT\\b|"
        "\\bINSTEAD\\b|"
        "\\bINTEGER\\b|"
        "\\bINTERSECT\\b|"
        "\\bINTO\\b|"
        "\\bIS\\b|"
        "\\bISNULL\\b|"
        "\\bJOIN\\b|"
        "\\bKEY\\b|"
        "\\bLEFT\\b|"
        "\\bLIKE\\b|"
        "\\bLIMIT\\b|"
        "\\bMATCH\\b|"
        "\\bNATURAL\\b|"
        "\\bNO\\b|"
        "\\bNOT\\b|"
        "\\bNOTNULL\\b|"
        "\\bNULL\\b|"
        "\\bOF\\b|"
        "\\bOFFSET\\b|"
        "\\bON\\b|"
        "\\bOR\\b|"
        "\\bORDER\\b|"
        "\\bOUTER\\b|"
        "\\bPLAN\\b|"
        "\\bPRAGMA\\b|"
        "\\bPRIMARY\\b|"
        "\\bQUERY\\b|"
        "\\bRAISE\\b|"
        "\\bRECURSIVE\\b|"
        "\\bREFERENCES\\b|"
        "\\bREGEXP\\b|"
        "\\bREINDEX\\b|"
        "\\bRELEASE\\b|"
        "\\bRENAME\\b|"
        "\\bREPLACE\\b|"
        "\\bRESTRICT\\b|"
        "\\bRIGHT\\b|"
        "\\bROLLBACK\\b|"
        "\\bROW\\b|"
        "\\bSAVEPOINT\\b|"
        "\\bSELECT\\b|"
        "\\bSET\\b|"
        "\\bTABLE\\b|"
        "\\bTEMP\\b|"
        "\\bTEMPORARY\\b|"
        "\\bTEXT\\b|"
        "\\bTHEN\\b|"
        "\\bTO\\b|"
        "\\bTRANSACTION\\b|"
        "\\bTRIGGER\\b|"
        "\\bUNION\\b|"
        "\\bUNIQUE\\b|"
        "\\bUPDATE\\b|"
        "\\bUSING\\b|"
        "\\bVACUUM\\b|"
        "\\bVALUES\\b|"
        "\\bVIEW\\b|"
        "\\bVIRTUAL\\b|"
        "\\bWHEN\\b|"
        "\\bWHERE\\b|"
        "\\bWITH\\b|"
        "\\bWITHOUT\\b"
        ")", PCRE_CASELESS))
        .with_text_format(text_format_t::TF_SQL)
        .with_role(view_colors::VCR_KEYWORD);

    hm["$srcfile"] = highlighter(xpcre_compile(
        "[\\w\\-_]+\\."
        "(?:java|a|o|so|c|cc|cpp|cxx|h|hh|hpp|hxx|py|pyc|rb):"
        "\\d+"))
        .with_role(view_colors::VCR_FILE);
    hm["$xml"] = static_highlighter("<(/?[^ >=]+)[^>]*>");
    hm["$stringd"] = highlighter(xpcre_compile(
        "\"(?:\\\\.|[^\"])*\""))
        .with_role(view_colors::VCR_STRING);
    hm["$strings"] = highlighter(xpcre_compile(
        "(?<![A-WY-Za-qstv-z])\'(?:\\\\.|[^'])*\'"))
        .with_role(view_colors::VCR_STRING);
    hm["$stringb"] = highlighter(xpcre_compile(
        "`(?:\\\\.|[^`])*`"))
        .with_role(view_colors::VCR_STRING);
    hm["$diffp"] = highlighter(xpcre_compile(
        "^\\+.*"))
        .with_role(view_colors::VCR_DIFF_ADD);
    hm["$diffm"] = highlighter(xpcre_compile(
        "^(?:--- .*|-$|-[^-].*)"))
        .with_role(view_colors::VCR_DIFF_DELETE);
    hm["$diffs"] = highlighter(xpcre_compile(
        "^\\@@ .*"))
        .with_role(view_colors::VCR_DIFF_SECTION);
    hm["$ip"] = static_highlighter("\\d+\\.\\d+\\.\\d+\\.\\d+");
    hm["$comment"] = highlighter(xpcre_compile(
        "(?<=[\\s;])//.*|/\\*.*\\*/|\\(\\*.*\\*\\)|^#.*|\\s+#.*|dnl.*"))
        .with_role(view_colors::VCR_COMMENT);
    hm["$sqlcomment"] = highlighter(xpcre_compile(
        "(?<=[\\s;])--.*"))
        .with_text_format(text_format_t::TF_SQL)
        .with_role(view_colors::VCR_COMMENT);
    hm["$javadoc"] = static_highlighter(
        "@(?:author|deprecated|exception|file|param|return|see|since|throws|todo|version)");
    hm["$var"] = highlighter(xpcre_compile(
        "(?:"
        "(?:var\\s+)?([\\-\\w]+)\\s*[!=+\\-*/|&^]?=|"
        "(?<!\\$)\\$(\\w+)|"
        "(?<!\\$)\\$\\((\\w+)\\)|"
        "(?<!\\$)\\$\\{(\\w+)\\}"
        ")"))
        .with_role(view_colors::VCR_VARIABLE);
    hm["$sym"] = highlighter(xpcre_compile(
        "\\b[A-Z_][A-Z0-9_]+\\b"))
        .with_text_format(text_format_t::TF_C_LIKE)
        .with_role(view_colors::VCR_SYMBOL);
    hm["$num"] = highlighter(xpcre_compile(
        "\\b-?(?:\\d+|0x[a-zA-Z0-9]+)\\b"))
        .with_text_format(text_format_t::TF_C_LIKE)
        .with_role(view_colors::VCR_NUMBER);
}
