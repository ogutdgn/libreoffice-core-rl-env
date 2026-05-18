/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <RawCapture.hxx>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <vcl/svapp.hxx>
#include <vcl/vclevent.hxx>
#include <vcl/event.hxx>
#include <vcl/window.hxx>
#include <vcl/commandevent.hxx>
#include <tools/link.hxx>
#include <rtl/ustring.hxx>

namespace rllogger::raw {

namespace {

bool g_installed = false;
std::ofstream g_stream;
std::atomic<uint64_t> g_seq{0};
std::chrono::steady_clock::time_point g_sessionStart;
Link<VclSimpleEvent&, void> g_listenerLink;

// Minimal JSON string escape — handles backslash, quote, control chars.
// LO interactions occasionally produce non-ASCII (text input), so we
// also escape > 0x7F to \uXXXX form to keep the output strictly ASCII.
std::string escapeJson(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c >= 0x80)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string escapeOUString(const OUString& s)
{
    OString u8 = OUStringToOString(s, RTL_TEXTENCODING_UTF8);
    return escapeJson(std::string_view(u8.getStr(), u8.getLength()));
}

const char* nameForEventId(VclEventId id)
{
    switch (id)
    {
        case VclEventId::WindowKeyInput:        return "key.down";
        case VclEventId::WindowKeyUp:           return "key.up";
        case VclEventId::WindowMouseButtonDown: return "mouse.down";
        case VclEventId::WindowMouseButtonUp:   return "mouse.up";
        case VclEventId::WindowMouseMove:       return "mouse.move";
        case VclEventId::WindowGetFocus:        return "focus.in";
        case VclEventId::WindowLoseFocus:       return "focus.out";
        case VclEventId::WindowCommand:         return "command";
        case VclEventId::WindowGestureEvent:    return "gesture";
        default:                                return nullptr;
    }
}

// Returns the time since session start, in milliseconds.
uint64_t sessionTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - g_sessionStart).count();
}

uint64_t wallTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void writeKeyFields(std::ostringstream& os, const KeyEvent* k)
{
    if (!k) { os << R"("fields":{})"; return; }
    const KeyCode& kc = k->GetKeyCode();
    os << R"("fields":{)"
       << R"("keyCode":)" << kc.GetCode() << ','
       << R"("char":)" << k->GetCharCode() << ','
       << R"("repeat":)" << (k->GetRepeat() ? "true" : "false")
       << '}';
}

void writeMouseFields(std::ostringstream& os, const MouseEvent* m)
{
    if (!m) { os << R"("fields":{})"; return; }
    const Point& p = m->GetPosPixel();
    os << R"("fields":{)"
       << R"("x":)" << p.X() << ','
       << R"("y":)" << p.Y() << ','
       << R"("button":")"
       << (m->IsLeft() ? "left" : m->IsRight() ? "right" : m->IsMiddle() ? "middle" : "none")
       << R"(",)"
       << R"("clicks":)" << m->GetClicks()
       << '}';
}

void writeModifiers(std::ostringstream& os, sal_uInt16 mods)
{
    os << R"("modifiers":{)"
       << R"("shift":)" << ((mods & KEY_SHIFT) ? "true" : "false") << ','
       << R"("ctrl":)" << ((mods & KEY_MOD1)  ? "true" : "false") << ','
       << R"("alt":)" << ((mods & KEY_MOD2)   ? "true" : "false") << ','
       << R"("meta":)" << ((mods & KEY_MOD3)  ? "true" : "false")
       << '}';
}

void writeTarget(std::ostringstream& os, vcl::Window* w)
{
    os << R"("target":)";
    if (!w) { os << "null"; return; }
    OUString name;
    try { name = w->GetText(); } catch (...) {}
    os << R"({"text":")" << escapeOUString(name) << R"("})";
}

// Free function used as the VCL event listener entry. Called on the
// main thread under SolarMutex by VCL's event dispatch.
void rawEventHandler(void* /*pThis*/, VclSimpleEvent& rEvent)
{
    if (!g_stream.is_open()) return;

    const VclEventId id = rEvent.GetId();
    const char* eventName = nameForEventId(id);
    if (!eventName) return; // filter out everything else

    auto* w = dynamic_cast<VclWindowEvent*>(&rEvent);
    void* pData = w ? w->GetData() : nullptr;
    vcl::Window* pWindow = w ? w->GetWindow() : nullptr;

    std::ostringstream os;
    os << '{'
       << R"("eventId":"raw-)" << g_seq.fetch_add(1) << R"(",)"
       << R"("type":")" << eventName << R"(",)"
       << R"("timestamp":)" << wallTimeMs() << ','
       << R"("sessionTime":)" << sessionTimeMs() << ',';

    sal_uInt16 mods = 0;

    switch (id)
    {
        case VclEventId::WindowKeyInput:
        case VclEventId::WindowKeyUp:
        {
            const auto* k = static_cast<const KeyEvent*>(pData);
            writeKeyFields(os, k);
            os << ',';
            if (k) mods = k->GetKeyCode().GetModifier();
            break;
        }
        case VclEventId::WindowMouseButtonDown:
        case VclEventId::WindowMouseButtonUp:
        case VclEventId::WindowMouseMove:
        {
            const auto* m = static_cast<const MouseEvent*>(pData);
            writeMouseFields(os, m);
            os << ',';
            if (m) mods = m->GetModifier();
            break;
        }
        default:
            os << R"("fields":{},)";
            break;
    }

    writeModifiers(os, mods);
    os << ',';
    writeTarget(os, pWindow);
    os << "}\n";

    const std::string line = os.str();
    g_stream << line;
    g_stream.flush();
}

} // namespace

void install(const std::filesystem::path& sessionDir)
{
    if (g_installed) return;

    g_stream.open(sessionDir / "raw.jsonl", std::ios::app);
    if (!g_stream.is_open())
    {
        std::fprintf(stderr,
                     "rllogger.raw: cannot open %s for append\n",
                     (sessionDir / "raw.jsonl").string().c_str());
        return;
    }

    g_sessionStart = std::chrono::steady_clock::now();
    g_listenerLink = LINK_NONMEMBER(nullptr, rawEventHandler);
    Application::AddEventListener(g_listenerLink);
    g_installed = true;
}

} // namespace rllogger::raw

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
