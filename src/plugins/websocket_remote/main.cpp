//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2016 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "Context.h"

#include "HttpServer.h"
#include "WebSocketServer.h"

#include <core/sdk/IPlaybackRemote.h>
#include <core/sdk/IPlugin.h>

#include <thread>

#ifdef WIN32
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

using namespace musik::core::sdk;

static Context context;

static class Plugin : public IPlugin {
    public:
        virtual void Destroy() { }
        virtual const char* Name() { return "WebSockets IPlaybackRemote"; }
        virtual const char* Version() { return "0.6.0"; }
        virtual const char* Author() { return "clangen"; }
        virtual const char* Guid() { return "9fc897a3-dfd5-4524-a0fc-b02f46aea4a9"; }
        virtual bool Configurable() { return false; }
        virtual void Configure() { }
        virtual void Reload() { }
        virtual int SdkVersion() { return musik::core::sdk::SdkVersion; }
} plugin;

static class PlaybackRemote : public IPlaybackRemote {
    private:
        HttpServer httpServer;
        WebSocketServer webSocketServer;

    public:
        PlaybackRemote()
        : httpServer(context)
        , webSocketServer(context) {
        }

        virtual ~PlaybackRemote() {
            if (this->thread) {
                httpServer.Stop();
                webSocketServer.Stop();
                this->thread->join();
            }
        }

        virtual void Destroy() {
        }

        void CheckRunningStatus() {
            if (!thread && context.environment && context.playback && context.prefs && context.dataProvider) {
                thread.reset(new std::thread(std::bind(&PlaybackRemote::ThreadProc, this)));
            }
            else if (thread && (!context.environment || !context.playback || !context.prefs || !context.dataProvider)) {
                httpServer.Stop();
                webSocketServer.Stop();
                this->thread->join();
                this->thread.reset();
            }
        }

        virtual void SetPlaybackService(IPlaybackService* playback) {
            auto wl = context.lock.Write();
            context.playback = playback;
            this->CheckRunningStatus();
        }

        virtual void OnTrackChanged(ITrack* track) {
            webSocketServer.OnTrackChanged(track);
        }

        virtual void OnPlaybackStateChanged(PlaybackState state) {
            webSocketServer.OnPlaybackStateChanged(state);
        }

        virtual void OnVolumeChanged(double volume) {
            webSocketServer.OnVolumeChanged(volume);
        }

        virtual void OnModeChanged(RepeatMode repeatMode, bool shuffled) {
            webSocketServer.OnModeChanged(repeatMode, shuffled);
        }

        virtual void OnPlayQueueChanged() {
            webSocketServer.OnPlayQueueChanged();
        }

    private:
        void ThreadProc() {
            if (context.prefs->GetBool(prefs::http_server_enabled.c_str(), true)) {
                httpServer.Start();
            }

            webSocketServer.Start();
            httpServer.Wait();
            webSocketServer.Wait();
        }

        std::shared_ptr<std::thread> thread;
} remote;

extern "C" DLL_EXPORT IPlugin* GetPlugin() {
    return &plugin;
}

extern "C" DLL_EXPORT IPlaybackRemote* GetPlaybackRemote() {
    return &remote;
}

extern "C" DLL_EXPORT void SetEnvironment(musik::core::sdk::IEnvironment* env) {
    auto wl = context.lock.Write();
    context.environment = env;
    remote.CheckRunningStatus();
}

extern "C" DLL_EXPORT void SetPreferences(musik::core::sdk::IPreferences* prefs) {
    auto wl = context.lock.Write();
    context.prefs = prefs;

    if (prefs) {
        prefs->GetInt(prefs::websocket_server_port.c_str(), defaults::websocket_server_port);
        prefs->GetInt(prefs::http_server_port.c_str(), defaults::http_server_port);
        prefs->GetBool(prefs::http_server_enabled.c_str(), true);
        prefs->GetString(key::password.c_str(), nullptr, 0, defaults::password.c_str());
        prefs->GetInt(prefs::transcoder_cache_count.c_str(), defaults::transcoder_cache_count);
        prefs->GetBool(prefs::transcoder_synchronous.c_str(), defaults::transcoder_synchronous);
        prefs->GetBool(prefs::transcoder_synchronous_fallback.c_str(), defaults::transcoder_synchronous_fallback);
        prefs->Save();
    }

    remote.CheckRunningStatus();
}

extern "C" DLL_EXPORT void SetSimpleDataProvider(musik::core::sdk::ISimpleDataProvider* dataProvider) {
    auto wl = context.lock.Write();
    context.dataProvider = dataProvider;
    remote.CheckRunningStatus();
}
