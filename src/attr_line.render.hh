/**
 * Copyright (c) 2026, Timothy Stack
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

#ifndef lnav_attr_line_render_hh
#define lnav_attr_line_render_hh

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/color_spaces.hh"
#include "base/log_level_enum.hh"

/**
 * The part of rendering an attr_line_t that every output format shares:
 * splitting the line into runs of text that have the same attributes.
 */
namespace lnav::render {

/**
 * One attribute that styles a run.  A run keeps these in the order the
 * attributes are applied on the screen, so a later part takes precedence
 * over an earlier one.
 */
struct style_part {
    enum class kind_t {
        /** sp_role, with all of its attributes. */
        role,
        /** sp_role, with only its foreground color. */
        role_fg,
        /** sp_level. */
        level,
        /** sp_attrs, as explicitly given. */
        attrs,
        /** sp_attrs.ta_fg_color, which replaces the current color. */
        fg,
        /** sp_attrs.ta_bg_color, which replaces the current color. */
        bg,
    };

    kind_t sp_kind;
    role_t sp_role{role_t::VCR_NONE};
    log_level_t sp_level{LEVEL_UNKNOWN};
    /**
     * For a role, the colors that the theme leaves to the text, already
     * resolved against it.  For the others, the attributes themselves, with
     * their colors resolved the same way.
     */
    text_attrs sp_attrs;
};

struct styled_run {
    std::vector<style_part> sr_parts;
    /** The link target, pointing into the attr_line_t, or nullptr. */
    const std::string* sr_href{nullptr};
    /** The anchor name, pointing into the attr_line_t, or nullptr. */
    const std::string* sr_id{nullptr};
    /**
     * Notes from the attributes that are not drawn, like the file a line
     * came from, but only the ones that do not cover the whole line.  Those
     * are returned by line_notes() instead.
     */
    std::vector<std::string> sr_notes;
    /**
     * The text to show, with glyphs in place of the characters they replace
     * and control characters made visible.  It is not escaped for any output
     * format.
     */
    std::string sr_text;
};

/**
 * @return True if both strings are absent or both are present and equal.
 */
inline bool
same_str(const std::string* lhs, const std::string* rhs)
{
    if (lhs == nullptr || rhs == nullptr) {
        return lhs == rhs;
    }
    return *lhs == *rhs;
}

/**
 * @return The attributes that the part contributes to a run, with the colors
 *   as the theme gave them.  A role that only contributes its foreground
 *   color has nothing else set.
 */
text_attrs attrs_for_part(const style_part& part);

/**
 * Combine attributes the way they are combined for a cell on the screen:
 * the new colors win where they are set and reverse video toggles.
 *
 * @param attrs The attributes being applied.
 * @param resolved The attributes applied so far.
 */
text_attrs apply_attrs(const text_attrs& attrs, const text_attrs& resolved);

/**
 * Split a line into runs of text and pass each one to a callback.
 * Boundaries are always at the start of a character.  Adjacent runs are not
 * merged, even if they look the same.  Each byte that is not valid UTF-8 is
 * a run of its own that shows a replacement character with the non-ASCII
 * role, as on the screen.
 *
 * @param al The line to split up.
 * @param cb Called with each run in order.  The run is reused for the next
 *   one, so it is only valid during the call.  Its string pointers point
 *   into `al`.
 */
void for_each_run(const attr_line_t& al,
                  const std::function<void(const styled_run&)>& cb);

/**
 * @return Notes from the attributes that are not drawn and cover the whole
 *   line: the file it came from, its log format, and why it is invalid or
 *   an error.  They are in that order and without duplicates.
 */
std::vector<std::string> line_notes(const attr_line_t& al);

/**
 * @return The text of the line as it is shown, with glyphs in place of the
 *   characters they replace and control characters made visible.
 */
std::string to_text(const attr_line_t& al);

/**
 * @return The RGB value of a color, looking palette indexes up in the xterm
 *   palette that theme color names come from.  Nothing for a color that is
 *   empty, transparent or not resolved yet.
 */
std::optional<rgb_color> to_rgb(const styling::color_unit& cu);

}  // namespace lnav::render

#endif
