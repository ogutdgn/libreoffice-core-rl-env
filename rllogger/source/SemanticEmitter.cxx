/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <SemanticEmitter.hxx>

#include <CommandMap.hxx>
#include <Persist.hxx>
#include <RecentRaw.hxx>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/document/DocumentEvent.hpp>
#include <com/sun/star/document/XDocumentEventBroadcaster.hpp>
#include <com/sun/star/document/XDocumentEventListener.hpp>
#include <com/sun/star/frame/XDispatchRecorder.hpp>
#include <com/sun/star/frame/XDispatchRecorderSupplier.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/theGlobalEventBroadcaster.hpp>
#include <com/sun/star/lang/EventObject.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <comphelper/processfactory.hxx>
#include <cppuhelper/implbase.hxx>
#include <rtl/ustring.hxx>

using namespace ::com::sun::star;

namespace rllogger::semantic {

namespace {

// --- Module state -----------------------------------------------------

bool g_installed = false;
std::atomic<uint64_t> g_seq{0};

// --- JSON helpers -----------------------------------------------------

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

// Serialize one uno::Any into a JSON value. Type-dispatched in order
// of decreasing specificity: bool / integer / float / string / fall-
// back to type-name placeholder. The Any extraction operator (>>=)
// returns false when the type doesn't match, so cascading attempts
// is cheap. Sequences and structs land in the placeholder bucket —
// dispatch args carrying nested structs are rare and serializing
// them recursively isn't worth the code in V1.
std::string serializeAny(const uno::Any& aAny)
{
    {
        bool b = false;
        if (aAny >>= b) return b ? "true" : "false";
    }
    {
        sal_Int32 i32 = 0;
        if (aAny >>= i32)
        {
            std::ostringstream os;
            os << i32;
            return os.str();
        }
    }
    {
        sal_Int16 i16 = 0;
        if (aAny >>= i16)
        {
            std::ostringstream os;
            os << static_cast<int>(i16);
            return os.str();
        }
    }
    {
        sal_Int64 i64 = 0;
        if (aAny >>= i64)
        {
            std::ostringstream os;
            os << i64;
            return os.str();
        }
    }
    {
        double d = 0.0;
        if (aAny >>= d)
        {
            std::ostringstream os;
            os << d;
            return os.str();
        }
    }
    {
        float f = 0.0f;
        if (aAny >>= f)
        {
            std::ostringstream os;
            os << f;
            return os.str();
        }
    }
    {
        OUString s;
        if (aAny >>= s)
        {
            return std::string("\"") + escapeOUString(s) + "\"";
        }
    }
    // Unknown / nested type — emit a placeholder carrying the UNO
    // type name so consumers can at least see the shape.
    const OUString typeName = aAny.getValueTypeName();
    return std::string("\"<") + escapeOUString(typeName) + ">\"";
}

// Map the most recent raw event onto a coarse trigger label. Reads
// the snapshot maintained by RawCapture. The 500 ms staleness gate
// keeps dispatches that arrive long after any UI input — typically
// macro / scripting calls — labelled as `programmatic`.
std::string_view detectTrigger(uint64_t nowMs)
{
    const raw::RecentRawSnapshot s = raw::getLastRaw();
    using LR = raw::LastRawType;
    using TW = raw::TargetWidget;

    if (s.type == LR::None) return "programmatic";
    if (nowMs > s.timestampMs && nowMs - s.timestampMs > 500)
        return "programmatic";

    switch (s.type)
    {
        case LR::KeyDown:
        case LR::KeyUp:
            return "shortcut";
        case LR::MouseDown:
        case LR::MouseUp:
            switch (s.widget)
            {
                case TW::Toolbar:      return "toolbar";
                case TW::MenuBar:      return "menu";
                case TW::FloatingMenu: return "menu";
                case TW::Document:     return "click";
                default:               return "click";
            }
        default:
            return "unknown";
    }
}


// --- XDispatchRecorder implementation ---------------------------------
//
// The recorder is what SfxDispatcher / DispatchRecorderSupplier calls
// for every UNO command issued through the frame's dispatch pipeline.
// Each recordDispatch invocation becomes one line of semantic.jsonl.
//
// Step 4 emits a minimal record: schema version, ids, raw .uno: URL,
// document URL. The trigger heuristic and the URL → named-event
// mapping land in step 5 (CommandMap).

class RlDispatchRecorder
    : public ::cppu::WeakImplHelper<frame::XDispatchRecorder, lang::XServiceInfo>
{
private:
    OUString m_documentUrl;

public:
    RlDispatchRecorder() = default;

    // XDispatchRecorder
    void SAL_CALL startRecording(const uno::Reference<frame::XFrame>& xFrame) override
    {
        if (xFrame.is())
        {
            try
            {
                uno::Reference<frame::XController> xController = xFrame->getController();
                if (xController.is())
                {
                    uno::Reference<frame::XModel> xModel = xController->getModel();
                    if (xModel.is())
                        m_documentUrl = xModel->getURL();
                }
            }
            catch (const uno::Exception&)
            {
            }
        }
    }

    void SAL_CALL recordDispatch(const util::URL& aURL,
                                 const uno::Sequence<beans::PropertyValue>& lArguments) override
    {
        const uint64_t nowMs = wallTimeMs();
        const OString rawUrlUtf8 = OUStringToOString(aURL.Complete, RTL_TEXTENCODING_UTF8);
        const std::string_view rawUrl(rawUrlUtf8.getStr(), rawUrlUtf8.getLength());
        const std::string_view mapped = mapCommand(rawUrl);
        const std::string_view name = mapped.empty() ? rawUrl : mapped;
        const std::string_view trigger = detectTrigger(nowMs);
        const raw::GestureRange gesture = raw::getGestureRange();

        std::ostringstream os;
        os << '{'
           << R"("schemaVersion":1,)"
           << R"("eventId":"sem-)" << g_seq.fetch_add(1) << R"(",)"
           << R"("timestamp":)" << nowMs << ','
           << R"("documentUrl":")" << escapeOUString(m_documentUrl) << R"(",)"
           << R"("name":")" << escapeJson(name) << R"(",)"
           << R"("rawName":")" << escapeJson(rawUrl) << R"(",)"
           << R"("trigger":")" << trigger << R"(",)";
        if (gesture.valid)
        {
            os << R"("rawEventIdRange":["raw-)" << gesture.firstId
               << R"(","raw-)" << gesture.lastId << R"("],)";
        }
        // Argument introspection: emit each PropertyValue's name and
        // type-dispatched value. Duplicate names (rare but legal in
        // PropertyValue sequences) end up as last-wins in JSON object
        // form; the raw count is still emitted alongside so callers
        // can detect when a dedup happened.
        os << R"("args":{)";
        const sal_Int32 nArgs = lArguments.getLength();
        for (sal_Int32 i = 0; i < nArgs; ++i)
        {
            const beans::PropertyValue& pv = lArguments[i];
            if (i > 0) os << ',';
            os << '"' << escapeOUString(pv.Name) << "\":" << serializeAny(pv.Value);
        }
        os << "},"
           << R"("argCount":)" << nArgs
           << '}';
        persist::enqueueSemantic(os.str());
    }

    void SAL_CALL recordDispatchAsComment(const util::URL& aURL,
                                          const uno::Sequence<beans::PropertyValue>& lArguments) override
    {
        // Treat commented dispatches the same as recorded ones — the
        // RL agent shouldn't care about the macro-export distinction.
        recordDispatch(aURL, lArguments);
    }

    void SAL_CALL endRecording() override {}

    OUString SAL_CALL getRecordedMacro() override
    {
        // Macro export isn't a feature for the RL log; SfxDispatcher
        // only invokes this when user-facing macro recording is on.
        return OUString();
    }

    // XServiceInfo
    OUString SAL_CALL getImplementationName() override
    {
        return u"org.libreoffice.rllogger.RlDispatchRecorder"_ustr;
    }

    sal_Bool SAL_CALL supportsService(const OUString& sServiceName) override
    {
        return sServiceName == u"com.sun.star.frame.DispatchRecorder"_ustr;
    }

    uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override
    {
        return { u"com.sun.star.frame.DispatchRecorder"_ustr };
    }
};

// --- Frame attachment -------------------------------------------------
//
// SfxDispatcher routes through DispatchRecorderSupplier when that
// supplier holds a non-null recorder. Each Frame may have its own
// supplier; we ensure every newly loaded document gets ours.

void attachRecorderToFrame(const uno::Reference<frame::XFrame>& xFrame)
{
    if (!xFrame.is()) return;

    try
    {
        uno::Reference<beans::XPropertySet> xFrameProps(xFrame, uno::UNO_QUERY);
        if (!xFrameProps.is()) return;

        uno::Reference<frame::XDispatchRecorderSupplier> xSupplier;
        xFrameProps->getPropertyValue(u"DispatchRecorderSupplier"_ustr) >>= xSupplier;

        if (!xSupplier.is())
        {
            // No supplier on the frame yet. Create one.
            const uno::Reference<uno::XComponentContext> xCtx =
                comphelper::getProcessComponentContext();
            xSupplier.set(
                xCtx->getServiceManager()->createInstanceWithContext(
                    u"com.sun.star.frame.DispatchRecorderSupplier"_ustr, xCtx),
                uno::UNO_QUERY);
            if (!xSupplier.is()) return;
            xFrameProps->setPropertyValue(
                u"DispatchRecorderSupplier"_ustr, uno::Any(xSupplier));
        }

        if (!xSupplier->getDispatchRecorder().is())
        {
            uno::Reference<frame::XDispatchRecorder> xRecorder(new RlDispatchRecorder);
            xRecorder->startRecording(xFrame);
            xSupplier->setDispatchRecorder(xRecorder);
        }
    }
    catch (const uno::Exception&)
    {
        // Best-effort — never fail soffice startup over the logger.
    }
}

// --- Global event listener --------------------------------------------
//
// theGlobalEventBroadcaster fires for every document lifecycle change.
// We listen for "OnLoad" (existing file opened), "OnNew" (blank doc
// created) and "OnCreate" (Frame created) and attach the recorder to
// the affected document's frame.

class RlDocumentEventListener
    : public ::cppu::WeakImplHelper<document::XDocumentEventListener>
{
public:
    void SAL_CALL documentEventOccured(const document::DocumentEvent& aEvent) override
    {
        const OUString& name = aEvent.EventName;
        if (name != u"OnLoad"_ustr && name != u"OnNew"_ustr && name != u"OnCreate"_ustr)
            return;

        try
        {
            uno::Reference<frame::XModel> xModel(aEvent.Source, uno::UNO_QUERY);
            if (!xModel.is()) return;
            uno::Reference<frame::XController> xController = xModel->getCurrentController();
            if (!xController.is()) return;
            uno::Reference<frame::XFrame> xFrame = xController->getFrame();
            attachRecorderToFrame(xFrame);
        }
        catch (const uno::Exception&)
        {
        }
    }

    void SAL_CALL disposing(const lang::EventObject&) override {}
};

uno::Reference<document::XDocumentEventListener> g_docListener;
std::atomic<bool> g_uno_subscription_done{false};

// Subscribing to theGlobalEventBroadcaster from rllogger::initialize()
// (run early in sofficemain) hits the UNO bootstrap window when the
// service manager is not yet primed. Use a lazy synchronous attempt:
// the first time a raw VCL event fires after install() we retry the
// subscription, since by then Application::Execute() is running and
// the UNO context is fully wired. The first successful attempt sets
// g_uno_subscription_done so the retry path becomes a cheap atomic
// read on every later event.
void trySubscribeOnce()
{
    if (g_uno_subscription_done.load(std::memory_order_acquire)) return;

    try
    {
        const uno::Reference<uno::XComponentContext> xCtx =
            comphelper::getProcessComponentContext();
        if (!xCtx.is()) return;

        uno::Reference<frame::XGlobalEventBroadcaster> xBroadcaster =
            frame::theGlobalEventBroadcaster::get(xCtx);
        if (!xBroadcaster.is()) return;

        g_docListener.set(new RlDocumentEventListener);
        xBroadcaster->addDocumentEventListener(g_docListener);
        g_uno_subscription_done.store(true, std::memory_order_release);
    }
    catch (const uno::Exception&)
    {
        // Try again later — leave g_uno_subscription_done false.
    }
}

} // namespace

void retrySubscription()
{
    trySubscribeOnce();
}

void install(const std::filesystem::path& /*sessionDir*/)
{
    if (g_installed) return;
    g_installed = true;

    // The UNO subscription is *not* attempted here. install() runs from
    // sofficemain before the UNO bootstrap completes, and
    // comphelper::getProcessComponentContext() can SIGSEGV (not throw)
    // when called at that point. The first raw VCL event in
    // RawCapture.cxx calls retrySubscription() once the scheduler is
    // ticking and UNO is wired.
}

} // namespace rllogger::semantic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
