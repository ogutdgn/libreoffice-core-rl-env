/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CommandMap.hxx>

#include <unordered_map>

namespace rllogger::semantic {

namespace {

// V1 Writer-focused command map. Mirror of the table in
// docs/architecture/PHASE3_LOGGER_DESIGN.md §2.2. Keys are .uno:
// URLs as they appear in dispatch records; values are RL-agent-
// friendly stable identifiers. String-view keys point at static
// string literals, so the map can be built once and held forever.
const std::unordered_map<std::string_view, std::string_view>& getMap()
{
    static const std::unordered_map<std::string_view, std::string_view> kMap = {
        // File
        { ".uno:Save",            "file_save" },
        { ".uno:SaveAs",          "file_save_as" },
        { ".uno:CloseDoc",        "file_close" },
        { ".uno:Open",            "file_open" },
        { ".uno:Print",           "file_print" },
        { ".uno:Quit",            "app_quit" },
        // Edit
        { ".uno:Cut",             "edit_cut" },
        { ".uno:Copy",            "edit_copy" },
        { ".uno:Paste",           "edit_paste" },
        { ".uno:PasteSpecial",    "edit_paste_special" },
        { ".uno:Undo",            "edit_undo" },
        { ".uno:Redo",            "edit_redo" },
        { ".uno:Delete",          "edit_delete" },
        // Find/replace
        { ".uno:SearchDialog",    "edit_find" },
        { ".uno:ReplaceDialog",   "edit_find_replace" },
        // Text format
        { ".uno:Bold",            "format_bold" },
        { ".uno:Italic",          "format_italic" },
        { ".uno:Underline",       "format_underline" },
        { ".uno:Strikeout",       "format_strikeout" },
        { ".uno:SuperScript",     "format_superscript" },
        { ".uno:SubScript",       "format_subscript" },
        // Paragraph
        { ".uno:LeftPara",        "paragraph_align_left" },
        { ".uno:CenterPara",      "paragraph_align_center" },
        { ".uno:RightPara",       "paragraph_align_right" },
        { ".uno:JustifyPara",     "paragraph_align_justify" },
        { ".uno:DefaultBullet",   "paragraph_bullet_toggle" },
        { ".uno:DefaultNumbering","paragraph_number_toggle" },
        // Font
        { ".uno:CharFontName",    "format_font_change" },
        { ".uno:FontHeight",      "format_size_change" },
        { ".uno:Color",           "format_color" },
        { ".uno:BackColor",       "format_highlight" },
        // Insert
        { ".uno:InsertGraphic",   "insert_image" },
        { ".uno:InsertObject",    "insert_object" },
        { ".uno:InsertTable",     "insert_table" },
        { ".uno:InsertPagebreak", "insert_pagebreak" },
        { ".uno:InsertSymbol",    "insert_symbol" },
        // Selection
        { ".uno:SelectAll",       "select_all" },
        // View / zoom
        { ".uno:Zoom",            "view_zoom" },
        { ".uno:ZoomPlus",        "view_zoom_in" },
        { ".uno:ZoomMinus",       "view_zoom_out" },
    };
    return kMap;
}

} // namespace

std::string_view mapCommand(std::string_view unoUrl)
{
    const auto& m = getMap();
    const auto it = m.find(unoUrl);
    return it != m.end() ? it->second : std::string_view{};
}

} // namespace rllogger::semantic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
