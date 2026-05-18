/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <RawCapture.hxx>
#include <OutcomeSnapshot.hxx>
#include <RecentRaw.hxx>
#include <SemanticEmitter.hxx>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <vcl/svapp.hxx>
#include <vcl/vclevent.hxx>
#include <vcl/event.hxx>
#include <vcl/window.hxx>
#include <vcl/wintypes.hxx>
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

// Snapshot of the most recent raw event. Read by the semantic emitter
// at recordDispatch time to attribute the dispatch to a UI trigger.
// Both writer and reader run on the main thread under SolarMutex, so
// no lock is needed.
RecentRawSnapshot g_lastRaw;

// JSON string escape. Input is treated as UTF-8 (the LO convention via
// OUStringToOString). Only control bytes (< 0x20) and JSON's two
// mandatory escapes (quote, backslash) get \uXXXX / \\X treatment.
// All other bytes — including the multi-byte UTF-8 sequences for
// non-ASCII characters — pass through verbatim, since JSON allows
// UTF-8 string content directly. Escaping each byte of a multi-byte
// sequence separately would produce nonsense ("â"
// instead of a single em dash) on the consumer side.
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
                if (c < 0x20)
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

LastRawType lastRawTypeForEventId(VclEventId id)
{
    switch (id)
    {
        case VclEventId::WindowKeyInput:        return LastRawType::KeyDown;
        case VclEventId::WindowKeyUp:           return LastRawType::KeyUp;
        case VclEventId::WindowMouseButtonDown: return LastRawType::MouseDown;
        case VclEventId::WindowMouseButtonUp:   return LastRawType::MouseUp;
        case VclEventId::WindowMouseMove:       return LastRawType::MouseMove;
        case VclEventId::WindowGetFocus:        return LastRawType::FocusIn;
        case VclEventId::WindowLoseFocus:       return LastRawType::FocusOut;
        case VclEventId::WindowCommand:         return LastRawType::Command;
        case VclEventId::WindowGestureEvent:    return LastRawType::Gesture;
        default:                                return LastRawType::None;
    }
}

// Walk up the parent chain looking for a classifiable container. The
// click target is often a deep child (a Button inside a ToolBox, or
// the menu-item rendering window inside a MenuBarWindow); the
// enclosing widget is what tells us "this came from a toolbar".
TargetWidget classifyWidget(vcl::Window* w)
{
    for (vcl::Window* cur = w; cur != nullptr; cur = cur->GetParent())
    {
        switch (cur->GetType())
        {
            case WindowType::TOOLBOX:
                return TargetWidget::Toolbar;
            case WindowType::MENUBARWINDOW:
                return TargetWidget::MenuBar;
            case WindowType::FLOATINGWINDOW:
                return TargetWidget::FloatingMenu;
            default:
                break;
        }
    }
    return w ? TargetWidget::Document : TargetWidget::Unknown;
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
    const vcl::KeyCode& kc = k->GetKeyCode();
    // GetCharCode returns sal_uInt16 == char16_t; stream insertion for
    // char16_t is deleted in C++17, so widen to unsigned for the JSON
    // numeric field.
    os << R"("fields":{)"
       << R"("keyCode":)" << static_cast<unsigned>(kc.GetCode()) << ','
       << R"("char":)" << static_cast<unsigned>(k->GetCharCode()) << ','
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

    // First-event retry: rllogger::initialize() ran before the UNO
    // service manager was wired, so semantic::install() may have
    // failed to subscribe to theGlobalEventBroadcaster. Now that VCL
    // is dispatching events, the UNO context is ready. The retry is
    // a cheap atomic load on every subsequent event.
    semantic::retrySubscription();
    // Same lazy start for the outcome snapshot AutoTimer; we can't
    // create / Start() it from rllogger::initialize() because the
    // scheduler isn't running yet.
    outcome::retryStart();

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

    // Update the recent-raw snapshot for the semantic emitter's
    // trigger heuristic. Only key / mouse-button events count —
    // mouse.move is noise; command / focus / gesture events fire
    // between a keystroke and its SfxDispatcher dispatch (IME, wheel
    // autoscroll, focus shuffles) and would mislabel keyboard
    // shortcuts as `menu`.
    switch (id)
    {
        case VclEventId::WindowKeyInput:
        case VclEventId::WindowKeyUp:
        case VclEventId::WindowMouseButtonDown:
        case VclEventId::WindowMouseButtonUp:
            g_lastRaw.type = lastRawTypeForEventId(id);
            g_lastRaw.widget = classifyWidget(pWindow);
            g_lastRaw.timestampMs = wallTimeMs();
            g_lastRaw.hasModifier = (mods & (KEY_SHIFT | KEY_MOD1 | KEY_MOD2 | KEY_MOD3)) != 0;
            break;
        default:
            break;
    }
}

} // namespace

RecentRawSnapshot getLastRaw()
{
    return g_lastRaw;
}

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
