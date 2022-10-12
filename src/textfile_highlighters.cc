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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>

#include "textfile_highlighters.hh"

#include "config.h"

template<typename T, std::size_t N>
static std::shared_ptr<lnav::pcre2pp::code>
xpcre_compile(const T (&pattern)[N], int options = 0)
{
    return lnav::pcre2pp::code::from_const(pattern, options).to_shared();
}

void
setup_highlights(highlight_map_t& hm)
{
    hm[{highlight_source_t::INTERNAL, "python"}]
        = highlighter(xpcre_compile("(?:"
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
              .with_nestable(false)
              .with_text_format(text_format_t::TF_PYTHON)
              .with_role(role_t::VCR_KEYWORD);

    hm[{highlight_source_t::INTERNAL, "rust"}]
        = highlighter(xpcre_compile("(?:"
                                    "\\bas\\b|"
                                    "\\buse\\b|"
                                    "\\bextern crate\\b|"
                                    "\\bbreak\\b|"
                                    "\\bconst\\b|"
                                    "\\bcontinue\\b|"
                                    "\\bcrate\\b|"
                                    "\\belse\\b|"
                                    "\\bif\\b|"
                                    "\\bif let\\b|"
                                    "\\benum\\b|"
                                    "\\bextern\\b|"
                                    "\\bfalse\\b|"
                                    "\\bfn\\b|"
                                    "\\bfor\\b|"
                                    "\\bif\\b|"
                                    "\\bimpl\\b|"
                                    "\\bin\\b|"
                                    "\\bfor\\b|"
                                    "\\blet\\b|"
                                    "\\bloop\\b|"
                                    "\\bmatch\\b|"
                                    "\\bmod\\b|"
                                    "\\bmove\\b|"
                                    "\\bmut\\b|"
                                    "\\bpub\\b|"
                                    "\\bimpl\\b|"
                                    "\\bref\\b|"
                                    "\\breturn\\b|"
                                    "\\bSelf\\b|"
                                    "\\bself\\b|"
                                    "\\bstatic\\b|"
                                    "\\bstruct\\b|"
                                    "\\bsuper\\b|"
                                    "\\btrait\\b|"
                                    "\\btrue\\b|"
                                    "\\btype\\b|"
                                    "\\bunsafe\\b|"
                                    "\\buse\\b|"
                                    "\\bwhere\\b|"
                                    "\\bwhile\\b|"
                                    "\\babstract\\b|"
                                    "\\balignof\\b|"
                                    "\\bbecome\\b|"
                                    "\\bbox\\b|"
                                    "\\bdo\\b|"
                                    "\\bfinal\\b|"
                                    "\\bmacro\\b|"
                                    "\\boffsetof\\b|"
                                    "\\boverride\\b|"
                                    "\\bpriv\\b|"
                                    "\\bproc\\b|"
                                    "\\bpure\\b|"
                                    "\\bsizeof\\b|"
                                    "\\btypeof\\b|"
                                    "\\bunsized\\b|"
                                    "\\bvirtual\\b|"
                                    "\\byield\\b"
                                    ")"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_RUST)
              .with_role(role_t::VCR_KEYWORD);

    hm[{highlight_source_t::INTERNAL, "clike"}]
        = highlighter(xpcre_compile("(?:"
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
                                    "\\bconstexpr\\b|"
                                    "\\bconst_cast\\b|"
                                    "\\bcontinue\\b|"
                                    "\\bdecltype\\b|"
                                    "\\bdefault\\b|"
                                    "\\bdelete\\b|"
                                    "\\bdo\\b|"
                                    "\\bdouble\\b|"
                                    "\\bdynamic_cast\\b|"
                                    "\\belse\\b|"
                                    "\\benum\\b|"
                                    "\\bexplicit\\b|"
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
                                    "\\bnoexcept\\b|"
                                    "\\bnullptr\\b|"
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
                                    "\\bthread_local\\b|"
                                    "\\bthrow\\b|"
                                    "\\bthrows\\b|"
                                    "\\btransient\\b|"
                                    "\\btry\\b|"
                                    "\\btrue\\b|"
                                    "\\btypedef\\b|"
                                    "\\btypeid\\b|"
                                    "\\btypename\\b|"
                                    "\\bunion\\b|"
                                    "\\bunsigned\\b|"
                                    "\\busing\\b|"
                                    "\\bvirtual\\b|"
                                    "\\bvoid\\b|"
                                    "\\bvolatile\\b|"
                                    "\\bwchar_t\\b|"
                                    "\\bwhile\\b"
                                    ")"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_C_LIKE)
              .with_text_format(text_format_t::TF_JAVA)
              .with_role(role_t::VCR_KEYWORD);

    hm[{highlight_source_t::INTERNAL, "sql.0.comment"}]
        = highlighter(xpcre_compile("(?:(?<=[\\s;])|^)--.*"))
              .with_text_format(text_format_t::TF_SQL)
              .with_role(role_t::VCR_COMMENT);
    hm[{highlight_source_t::INTERNAL, "sql.9.keyword"}]
        = highlighter(xpcre_compile("(?:"
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
                                    ")",
                                    PCRE2_CASELESS))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_SQL)
              .with_role(role_t::VCR_KEYWORD);

    hm[{highlight_source_t::INTERNAL, "srcfile"}]
        = highlighter(xpcre_compile(
                          "[\\w\\-_]+\\."
                          "(?:java|a|o|so|c|cc|cpp|cxx|h|hh|hpp|hxx|py|pyc|rb):"
                          "\\d+"))
              .with_role(role_t::VCR_FILE);
    hm[{highlight_source_t::INTERNAL, "1.stringd"}]
        = highlighter(xpcre_compile(R"("(?:\\.|[^"])*")"))
              .with_nestable(false)
              .with_role(role_t::VCR_STRING);
    hm[{highlight_source_t::INTERNAL, "1.strings"}]
        = highlighter(xpcre_compile(R"((?<![A-WY-Za-qstv-z])'(?:\\.|[^'])*')"))
              .with_nestable(false)
              .with_role(role_t::VCR_STRING);
    hm[{highlight_source_t::INTERNAL, "1.stringb"}]
        = highlighter(xpcre_compile("`(?:\\\\.|[^`])*`"))
              .with_nestable(false)
              .with_role(role_t::VCR_STRING);
    hm[{highlight_source_t::INTERNAL, "diffp"}]
        = highlighter(xpcre_compile("^\\+.*")).with_role(role_t::VCR_DIFF_ADD);
    hm[{highlight_source_t::INTERNAL, "diffm"}]
        = highlighter(xpcre_compile("^(?:--- .*|-$|-[^-].*)"))
              .with_role(role_t::VCR_DIFF_DELETE);
    hm[{highlight_source_t::INTERNAL, "diffs"}]
        = highlighter(xpcre_compile("^\\@@ .*"))
              .with_role(role_t::VCR_DIFF_SECTION);
    hm[{highlight_source_t::INTERNAL, "0.comment"}]
        = highlighter(
              xpcre_compile(
                  R"((?<=[\s;])//.*|/\*.*\*/|\(\*.*\*\)|^#\s*(?!include|if|ifndef|elif|else|endif|error|pragma|define|undef).*|\s+#.*|dnl.*)"))
              .with_nestable(false)
              .with_role(role_t::VCR_COMMENT);
    hm[{highlight_source_t::INTERNAL, "javadoc"}]
        = highlighter(
              xpcre_compile("@(?:author|deprecated|exception|file|param|return|"
                            "see|since|throws|todo|version)"))
              .with_role(role_t::VCR_DOC_DIRECTIVE);
    hm[{highlight_source_t::INTERNAL, "var"}]
        = highlighter(
              xpcre_compile("(?:"
                            "(?:var\\s+)?([\\-\\w]+)\\s*[!=+\\-*/|&^]?=|"
                            "(?<!\\$)\\$(\\w+)|"
                            "(?<!\\$)\\$\\((\\w+)\\)|"
                            "(?<!\\$)\\$\\{(\\w+)\\}"
                            ")"))
              .with_nestable(false)
              .with_role(role_t::VCR_VARIABLE);
    hm[{highlight_source_t::INTERNAL, "rust.sym"}]
        = highlighter(xpcre_compile("\\b[A-Z_][A-Z0-9_]+\\b"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_RUST)
              .with_role(role_t::VCR_SYMBOL);
    hm[{highlight_source_t::INTERNAL, "rust.num"}]
        = highlighter(xpcre_compile(R"(\b-?(?:\d+|0x[a-zA-Z0-9]+)\b)"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_RUST)
              .with_role(role_t::VCR_NUMBER);
    hm[{highlight_source_t::INTERNAL, "sym"}]
        = highlighter(xpcre_compile("\\b[A-Z_][A-Z0-9_]+\\b"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_C_LIKE)
              .with_text_format(text_format_t::TF_JAVA)
              .with_role(role_t::VCR_SYMBOL);
    hm[{highlight_source_t::INTERNAL, "cpp"}]
        = highlighter(
              xpcre_compile(
                  R"(^#\s*(?:include|ifdef|ifndef|if|else|elif|error|endif|define|undef|pragma))"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_C_LIKE)
              .with_text_format(text_format_t::TF_JAVA)
              .with_role(role_t::VCR_KEYWORD);
    hm[{highlight_source_t::INTERNAL, "num"}]
        = highlighter(xpcre_compile(R"(\b-?(?:\d+|0x[a-zA-Z0-9]+)\b)"))
              .with_nestable(false)
              .with_text_format(text_format_t::TF_C_LIKE)
              .with_text_format(text_format_t::TF_JAVA)
              .with_role(role_t::VCR_NUMBER);
}
