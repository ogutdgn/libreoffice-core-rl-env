/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <OutcomeSnapshot.hxx>

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/FontUnderline.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/beans/UnknownPropertyException.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XDesktop2.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/text/XPageCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/uno/Exception.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/util/XModifiable.hpp>

#include <comphelper/processfactory.hxx>
#include <rtl/ustring.hxx>
#include <tools/link.hxx>
#include <vcl/timer.hxx>

using namespace ::com::sun::star;

namespace rllogger::outcome {

namespace {

// --- Module state -----------------------------------------------------

bool g_installed = false;
std::filesystem::path g_outcomePath;
std::atomic<bool> g_timerStarted{false};
std::unique_ptr<AutoTimer> g_timer;
std::mutex g_writeMutex;

// --- JSON helpers (kept local; small enough to duplicate) -------------

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

uint64_t wallTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// --- Text statistics --------------------------------------------------
//
// Cheap, language-agnostic word counter — treats any run of non-
// whitespace UTF-16 code units as one word. Matches Writer's word
// count closely enough for RL feedback; exact counts aren't required.

uint32_t countWords(const OUString& s)
{
    uint32_t count = 0;
    bool inWord = false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        const bool isWs = (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r'
                           || c == u' ' || c == u' ' || c == u' ');
        if (!isWs && !inWord)
        {
            ++count;
            inWord = true;
        }
        else if (isWs)
        {
            inWord = false;
        }
    }
    return count;
}

uint32_t countParagraphs(const uno::Reference<text::XText>& xText)
{
    uno::Reference<container::XEnumerationAccess> xEnumA(xText, uno::UNO_QUERY);
    if (!xEnumA.is()) return 0;
    uno::Reference<container::XEnumeration> xEnum = xEnumA->createEnumeration();
    if (!xEnum.is()) return 0;
    uint32_t count = 0;
    while (xEnum->hasMoreElements())
    {
        try { xEnum->nextElement(); } catch (...) { break; }
        ++count;
    }
    return count;
}

// --- Property helpers -------------------------------------------------
//
// Each property fetch is wrapped in try/catch — a cursor pointing at
// document boundaries (no character span) can throw
// UnknownPropertyException on some property names, and a non-Writer
// cursor wouldn't expose the Char* family at all. Failure leaves the
// out-parameter at its default so the JSON snapshot still composes.

template <typename T>
bool tryGetProp(const uno::Reference<beans::XPropertySet>& xProps,
                const char* propName, T& out)
{
    if (!xProps.is()) return false;
    try
    {
        uno::Any v = xProps->getPropertyValue(OUString::createFromAscii(propName));
        return (v >>= out);
    }
    catch (const uno::Exception&)
    {
        return false;
    }
}

bool propIsBold(const uno::Reference<beans::XPropertySet>& xProps)
{
    float weight = awt::FontWeight::NORMAL;
    if (!tryGetProp(xProps, "CharWeight", weight)) return false;
    return weight >= awt::FontWeight::SEMIBOLD;
}

bool propIsItalic(const uno::Reference<beans::XPropertySet>& xProps)
{
    sal_Int16 slant = static_cast<sal_Int16>(awt::FontSlant_NONE);
    awt::FontSlant fs;
    if (tryGetProp(xProps, "CharPosture", fs))
        return fs != awt::FontSlant_NONE;
    // Some properties expose the enum as int16; try that path too.
    if (tryGetProp(xProps, "CharPosture", slant))
        return slant != static_cast<sal_Int16>(awt::FontSlant_NONE);
    return false;
}

bool propIsUnderline(const uno::Reference<beans::XPropertySet>& xProps)
{
    sal_Int16 ul = awt::FontUnderline::NONE;
    if (!tryGetProp(xProps, "CharUnderline", ul)) return false;
    return ul != awt::FontUnderline::NONE;
}

// --- Snapshot builder -------------------------------------------------

void writeSnapshot(const std::string& json)
{
    std::lock_guard<std::mutex> lock(g_writeMutex);
    std::ofstream ofs(g_outcomePath, std::ios::trunc);
    if (!ofs.is_open()) return;
    ofs << json << '\n';
}

void buildAndWrite()
{
    if (!g_installed) return;

    try
    {
        const uno::Reference<uno::XComponentContext> xCtx =
            comphelper::getProcessComponentContext();
        if (!xCtx.is()) return;

        const uno::Reference<frame::XDesktop2> xDesktop = frame::Desktop::create(xCtx);
        if (!xDesktop.is()) return;

        const uno::Reference<frame::XFrame> xFrame = xDesktop->getActiveFrame();
        if (!xFrame.is()) return;

        const uno::Reference<frame::XController> xCtrl = xFrame->getController();
        if (!xCtrl.is()) return;

        const uno::Reference<frame::XModel> xModel = xCtrl->getModel();
        if (!xModel.is()) return;

        // Document URL + modified flag.
        const OUString docUrl = xModel->getURL();
        bool modified = false;
        if (uno::Reference<util::XModifiable> xMod(xModel, uno::UNO_QUERY); xMod.is())
            modified = xMod->isModified();

        // Word / character / paragraph counts. Writer-only for V1 —
        // XTextDocument cast fails on Calc / Impress; we just emit a
        // bare snapshot with no counts in that case.
        uint32_t wordCount = 0, charCount = 0, paraCount = 0;
        bool isWriter = false;
        uno::Reference<text::XText> xText;
        if (uno::Reference<text::XTextDocument> xTextDoc(xModel, uno::UNO_QUERY);
            xTextDoc.is())
        {
            isWriter = true;
            xText = xTextDoc->getText();
            if (xText.is())
            {
                const OUString full = xText->getString();
                charCount = static_cast<uint32_t>(full.getLength());
                wordCount = countWords(full);
                paraCount = countParagraphs(xText);
            }
        }

        // View cursor — current caret position, selected text, and the
        // character properties at the cursor. All Writer-specific; for
        // Calc / Impress the cast fails and these fields are omitted.
        sal_Int16 cursorPage = 0;
        awt::Point cursorPos{};
        bool cursorAvail = false;
        bool hasSelection = false;
        OUString selectionText;
        uno::Reference<beans::XPropertySet> xCursorProps;
        OUString fontName;
        float fontHeight = 0.0f;
        sal_Int32 fontColor = -1;
        if (isWriter)
        {
            try
            {
                uno::Reference<text::XTextViewCursorSupplier> xSupp(xCtrl, uno::UNO_QUERY);
                if (xSupp.is())
                {
                    uno::Reference<text::XTextViewCursor> xVC = xSupp->getViewCursor();
                    if (xVC.is())
                    {
                        cursorAvail = true;
                        // getPage() lives on XPageCursor; the Writer
                        // view cursor implements it but Calc / non-text
                        // implementations may not, so query rather
                        // than assume.
                        if (uno::Reference<text::XPageCursor> xPC(xVC, uno::UNO_QUERY);
                            xPC.is())
                        {
                            cursorPage = xPC->getPage();
                        }
                        cursorPos = xVC->getPosition();
                        uno::Reference<text::XTextRange> xRange(xVC, uno::UNO_QUERY);
                        if (xRange.is())
                            selectionText = xRange->getString();
                        hasSelection = !selectionText.isEmpty();
                        xCursorProps.set(xVC, uno::UNO_QUERY);
                        tryGetProp(xCursorProps, "CharFontName", fontName);
                        tryGetProp(xCursorProps, "CharHeight", fontHeight);
                        tryGetProp(xCursorProps, "CharColor", fontColor);
                    }
                }
            }
            catch (const uno::Exception&)
            {
            }
        }

        std::ostringstream os;
        os << '{'
           << R"("schemaVersion":1,)"
           << R"("capturedAt":)" << wallTimeMs() << ','
           << R"("document":{)"
           <<     R"("url":")" << escapeOUString(docUrl) << R"(",)"
           <<     R"("modified":)" << (modified ? "true" : "false") << ','
           <<     R"("isWriter":)" << (isWriter ? "true" : "false")
           << R"(},)"
           << R"("counts":{)"
           <<     R"("paragraphs":)" << paraCount << ','
           <<     R"("words":)" << wordCount << ','
           <<     R"("characters":)" << charCount
           << R"(})";
        if (cursorAvail)
        {
            os << R"(,"cursor":{)"
               <<     R"("page":)" << static_cast<int>(cursorPage) << ','
               <<     R"("x":)" << cursorPos.X << ','
               <<     R"("y":)" << cursorPos.Y
               << R"(},)"
               << R"("selection":{)"
               <<     R"("hasSelection":)" << (hasSelection ? "true" : "false") << ','
               <<     R"("length":)" << selectionText.getLength() << ','
               <<     R"("text":")" << escapeOUString(selectionText) << R"(")"
               << R"(},)"
               << R"("format":{)"
               <<     R"("font":")" << escapeOUString(fontName) << R"(",)"
               <<     R"("size":)" << fontHeight << ','
               <<     R"("bold":)" << (propIsBold(xCursorProps) ? "true" : "false") << ','
               <<     R"("italic":)" << (propIsItalic(xCursorProps) ? "true" : "false") << ','
               <<     R"("underline":)" << (propIsUnderline(xCursorProps) ? "true" : "false") << ','
               <<     R"("color":)" << fontColor
               << R"(})";
        }
        os << '}';
        writeSnapshot(os.str());
    }
    catch (const uno::Exception&)
    {
        // Active frame may be torn down between checks — ignore.
    }
}

void onTick(void* /*pThis*/, Timer* /*pTimer*/)
{
    buildAndWrite();
}

} // namespace

void install(const std::filesystem::path& sessionDir)
{
    if (g_installed) return;
    g_outcomePath = sessionDir / "outcome.jsonl";
    g_installed = true;
}

void retryStart()
{
    if (g_timerStarted.load(std::memory_order_acquire)) return;
    try
    {
        g_timer = std::make_unique<AutoTimer>("rllogger.outcome");
        g_timer->SetTimeout(250);
        g_timer->SetPriority(TaskPriority::LOWEST);
        g_timer->SetInvokeHandler(LINK_NONMEMBER(nullptr, onTick));
        g_timer->Start();
        g_timerStarted.store(true, std::memory_order_release);
    }
    catch (...)
    {
        // Scheduler may not be wired yet; next raw event will retry.
        g_timer.reset();
    }
}

void flushFinal()
{
    if (!g_installed) return;
    // The AutoTimer may have been disposed earlier in the VCL
    // teardown sequence; build the snapshot directly. buildAndWrite()
    // already wraps every UNO call in try/catch, so a half-torn-down
    // service manager produces an empty snapshot instead of a crash.
    if (g_timer) g_timer->Stop();
    buildAndWrite();
}

} // namespace rllogger::outcome

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
