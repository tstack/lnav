
#include "config.h"

#include "highlighter.hh"

highlighter::highlighter(const highlighter &other)
{
    this->h_pattern = other.h_pattern;
    this->h_fg = other.h_fg;
    this->h_bg = other.h_bg;
    this->h_role = other.h_role;
    this->h_code = other.h_code;
    pcre_refcount(this->h_code, 1);
    this->study();
    this->h_attrs = other.h_attrs;
    this->h_text_format = other.h_text_format;
    this->h_format_name = other.h_format_name;
    this->h_nestable = other.h_nestable;
    this->h_semantic = other.h_semantic;
}

highlighter &highlighter::operator=(const highlighter &other)
{
    if (this->h_code != nullptr && pcre_refcount(this->h_code, -1) == 0) {
        free(this->h_code);
        this->h_code = nullptr;
    }
    free(this->h_code_extra);

    this->h_pattern = other.h_pattern;
    this->h_fg = other.h_fg;
    this->h_bg = other.h_bg;
    this->h_role = other.h_role;
    this->h_code = other.h_code;
    pcre_refcount(this->h_code, 1);
    this->study();
    this->h_format_name = other.h_format_name;
    this->h_attrs = other.h_attrs;
    this->h_text_format = other.h_text_format;
    this->h_nestable = other.h_nestable;
    this->h_semantic = other.h_semantic;

    return *this;
}

void highlighter::study()
{
    const char *errptr;

    this->h_code_extra = pcre_study(this->h_code, 0, &errptr);
    if (!this->h_code_extra && errptr) {
        log_error("pcre_study error: %s", errptr);
    }
    if (this->h_code_extra != nullptr) {
        pcre_extra *extra = this->h_code_extra;

        extra->flags |= (PCRE_EXTRA_MATCH_LIMIT |
                         PCRE_EXTRA_MATCH_LIMIT_RECURSION);
        extra->match_limit           = 10000;
        extra->match_limit_recursion = 500;
    }
}

void highlighter::annotate(attr_line_t &al, int start) const
{
    auto &vc = view_colors::singleton();
    const auto &str = al.get_string();
    auto &sa = al.get_attrs();
    // The line we pass to pcre_exec will be treated as the start when the
    // carat (^) operator is used.
    const char *line_start = &(str.c_str()[start]);
    size_t re_end;

    if ((str.length() - start) > 8192)
        re_end = 8192;
    else
        re_end = str.length() - start;
    for (int off = 0; off < (int)str.size() - start; ) {
        int rc, matches[60];
        rc = pcre_exec(this->h_code,
                       this->h_code_extra,
                       line_start,
                       re_end,
                       off,
                       0,
                       matches,
                       60);
        if (rc > 0) {
            struct line_range lr;

            if (rc == 2) {
                lr.lr_start = start + matches[2];
                lr.lr_end   = start + matches[3];
            }
            else {
                lr.lr_start = start + matches[0];
                lr.lr_end   = start + matches[1];
            }

            if (lr.lr_end > lr.lr_start &&
                (this->h_nestable ||
                 find_string_attr_containing(sa, &view_curses::VC_STYLE, lr) ==
                 sa.end())) {
                int attrs = 0;

                if (this->h_attrs != -1) {
                    attrs = this->h_attrs;
                }
                if (this->h_semantic) {
                    attrs |= vc.attrs_for_ident(&str[lr.lr_start],
                                                lr.length());
                }
                if (!this->h_fg.empty()) {
                    sa.emplace_back(lr,
                                    &view_curses::VC_FOREGROUND,
                                    vc.match_color(this->h_fg));
                }
                if (!this->h_bg.empty()) {
                    sa.emplace_back(lr,
                                    &view_curses::VC_BACKGROUND,
                                    vc.match_color(this->h_bg));
                }
                if (this->h_role != view_colors::VCR_NONE) {
                    attrs |= vc.attrs_for_role(this->h_role);
                }
                sa.emplace_back(lr, &view_curses::VC_STYLE, attrs);

                off = matches[1];
            }
            else {
                off += 1;
            }
        }
        else {
            off = str.size();
        }
    }
}
