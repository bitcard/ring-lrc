/****************************************************************************
 *    Copyright (C) 2017-2020 Savoir-faire Linux Inc.                       *
 *   Author : Nicolas Jäger <nicolas.jager@savoirfairelinux.com>            *
 *   Author : Sébastien Blin <sebastien.blin@savoirfairelinux.com>          *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Lesser General Public             *
 *   License as published by the Free Software Foundation; either           *
 *   version 2.1 of the License, or (at your option) any later version.     *
 *                                                                          *
 *   This library is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include "api/newcallmodel.h"

// std
#include <chrono>
#include <random>
#include <map>

// Lrc
#include "callbackshandler.h"
#include "api/conversationmodel.h"
#include "api/contact.h"
#include "api/contactmodel.h"
#include "api/lrc.h"
#include "api/newaccountmodel.h"
#include "authority/storagehelper.h"
#include "dbus/callmanager.h"
#include "vcard.h"
#include "video/renderer.h"

// Ring daemon
#include <media_const.h>

// Qt
#include <QObject>
#include <QString>

static std::uniform_int_distribution<int> dis{ 0, std::numeric_limits<int>::max() };
static const std::map<short, QString> sip_call_status_code_map {
    {0, QObject::tr("Null")},
    {100, QObject::tr("Trying")},
    {180, QObject::tr("Ringing")},
    {181, QObject::tr("Being Forwarded")},
    {182, QObject::tr("Queued")},
    {183, QObject::tr("Progress")},
    {200, QObject::tr("OK")},
    {202, QObject::tr("Accepted")},
    {300, QObject::tr("Multiple Choices")},
    {301, QObject::tr("Moved Permanently")},
    {302, QObject::tr("Moved Temporarily")},
    {305, QObject::tr("Use Proxy")},
    {380, QObject::tr("Alternative Service")},
    {400, QObject::tr("Bad Request")},
    {401, QObject::tr("Unauthorized")},
    {402, QObject::tr("Payment Required")},
    {403, QObject::tr("Forbidden")},
    {404, QObject::tr("Not Found")},
    {405, QObject::tr("Method Not Allowed")},
    {406, QObject::tr("Not Acceptable")},
    {407, QObject::tr("Proxy Authentication Required")},
    {408, QObject::tr("Request Timeout")},
    {410, QObject::tr("Gone")},
    {413, QObject::tr("Request Entity Too Large")},
    {414, QObject::tr("Request URI Too Long")},
    {415, QObject::tr("Unsupported Media Type")},
    {416, QObject::tr("Unsupported URI Scheme")},
    {420, QObject::tr("Bad Extension")},
    {421, QObject::tr("Extension Required")},
    {422, QObject::tr("Session Timer Too Small")},
    {423, QObject::tr("Interval Too Brief")},
    {480, QObject::tr("Temporarily Unavailable")},
    {481, QObject::tr("Call TSX Does Not Exist")},
    {482, QObject::tr("Loop Detected")},
    {483, QObject::tr("Too Many Hops")},
    {484, QObject::tr("Address Incomplete")},
    {485, QObject::tr("Ambiguous")},
    {486, QObject::tr("Busy")},
    {487, QObject::tr("Request Terminated")},
    {488, QObject::tr("Not Acceptable")},
    {489, QObject::tr("Bad Event")},
    {490, QObject::tr("Request Updated")},
    {491, QObject::tr("Request Pending")},
    {493, QObject::tr("Undecipherable")},
    {500, QObject::tr("Internal Server Error")},
    {501, QObject::tr("Not Implemented")},
    {502, QObject::tr("Bad Gateway")},
    {503, QObject::tr("Service Unavailable")},
    {504, QObject::tr("Server Timeout")},
    {505, QObject::tr("Version Not Supported")},
    {513, QObject::tr("Message Too Large")},
    {580, QObject::tr("Precondition Failure")},
    {600, QObject::tr("Busy Everywhere")} ,
    {603, QObject::tr("Call Refused")},
    {604, QObject::tr("Does Not Exist Anywhere")},
    {606, QObject::tr("Not Acceptable Anywhere")}
};

namespace lrc
{

using namespace api;

class NewCallModelPimpl: public QObject
{
public:
    NewCallModelPimpl(const NewCallModel& linked, const CallbacksHandler& callbacksHandler);
    ~NewCallModelPimpl();

    /**
     * Send the profile VCard into a call
     * @param callId
     */
    void sendProfile(const QString& callId);

    NewCallModel::CallInfoMap calls;
    const CallbacksHandler& callbacksHandler;
    const NewCallModel& linked;

    /**
     * key = peer's uri
     * vector = chunks
     * @note chunks are counted from 1 to number of parts. We use 0 to store the actual number of parts stored
     */
    std::map<QString, VectorString> vcardsChunks;

    /**
     * Retrieve active calls from the daemon and init the model
     */
    void initCallFromDaemon();
    /**
     * Retrieve active conferences from the daemon and init the model
     */
    void initConferencesFromDaemon();
    bool manageCurrentCall_ {true};
    QString currentCall_ {};

    std::map<QString, QString> pendingConferences_;
public Q_SLOTS:
    /**
     * Listen from CallbacksHandler when a call is incoming
     * @param accountId account which receives the call
     * @param callId
     * @param fromId peer uri
     * @param displayname
     */
    void slotIncomingCall(const QString& accountId, const QString& callId, const QString& fromId, const QString& displayname);
    /**
     * Listen from CallbacksHandler when a call got a new state
     * @param callId
     * @param state the new state
     * @param code unused
     */
    void slotCallStateChanged(const QString& callId, const QString &state, int code);
    /**
     * Listen from CallbacksHandler when a VCard chunk is incoming
     * @param callId
     * @param from
     * @param part
     * @param numberOfParts
     * @param payload
     */
    void slotincomingVCardChunk(const QString& callId, const QString& from, int part, int numberOfParts, const QString& payload);
    /**
     * Listen from CallbacksHandler when a conference is created.
     * @param callId
     */
    void slotConferenceCreated(const QString& callId);
    /**
     * Listen from CallbacksHandler when a voice mail notice is incoming
     * @param accountId
     * @param newCount
     * @param oldCount
     * @param urgentCount
     */
    void slotVoiceMailNotify(const QString& accountId, int newCount, int oldCount, int urgentCount);
};

NewCallModel::NewCallModel(const account::Info& owner, const CallbacksHandler& callbacksHandler)
: owner(owner)
, pimpl_(std::make_unique<NewCallModelPimpl>(*this, callbacksHandler))
{
}

NewCallModel::~NewCallModel()
{
}

const call::Info&
NewCallModel::getCallFromURI(const QString& uri, bool notOver) const
{
    // peer url = ring:uri or sip number
    auto url = (owner.profileInfo.type != profile::Type::SIP && !uri.contains("ring:")) ? "ring:" + uri : uri;
    for (const auto& call: pimpl_->calls) {
        if (call.second->peerUri == url) {
            if (!notOver || !call::isTerminating(call.second->status))
                return *call.second;
        }
    }
    throw std::out_of_range("No call at URI " + uri.toStdString());
}

const call::Info&
NewCallModel::getConferenceFromURI(const QString& uri) const
{
    for (const auto& call: pimpl_->calls) {
        if (call.second->type == call::Type::CONFERENCE) {
            QStringList callList = CallManager::instance().getParticipantList(call.first);
            foreach(const auto& callId, callList) {
                try {
                    if (pimpl_->calls.find(callId) != pimpl_->calls.end()
                        && pimpl_->calls[callId]->peerUri == uri) {
                        return *call.second;
                    }
                } catch (...) {}
            }
        }
    }
    throw std::out_of_range("No call at URI " + uri.toStdString());
}

const call::Info&
NewCallModel::getCall(const QString& uid) const
{
    return *pimpl_->calls.at(uid);
}

QString
NewCallModel::createCall(const QString& uri, bool isAudioOnly)
{
#ifdef ENABLE_LIBWRAP
    auto callId = isAudioOnly ? CallManager::instance().placeCall(owner.id, uri, {{"AUDIO_ONLY", "true"}})
                                  : CallManager::instance().placeCall(owner.id, uri);
#else // dbus
    // do not use auto here (QDBusPendingReply<QString>)
    QString callId = isAudioOnly ? CallManager::instance().placeCallWithDetails(owner.id, uri, {{"AUDIO_ONLY", "true"}})
                                 : CallManager::instance().placeCall(owner.id, uri);
#endif // ENABLE_LIBWRAP

    if (callId.isEmpty()) {
        qDebug() << "no call placed between (account: " << owner.id << ", contact: " << uri << ")";
        return "";
    }

    auto callInfo = std::make_shared<call::Info>();
    callInfo->id = callId;
    callInfo->peerUri = uri;
    callInfo->isOutgoing = true;
    callInfo->status =  call::Status::SEARCHING;
    callInfo->type =  call::Type::DIALOG;
    callInfo->isAudioOnly = isAudioOnly;
    pimpl_->calls.emplace(callId, std::move(callInfo));

    return callId;
}

void
NewCallModel::accept(const QString& callId) const
{
    CallManager::instance().accept(callId);
}

void
NewCallModel::hangUp(const QString& callId) const
{
    if (!hasCall(callId)) return;
    auto& call = pimpl_->calls[callId];
    switch(call->type)
    {
    case call::Type::DIALOG:
        CallManager::instance().hangUp(callId);
        break;
    case call::Type::CONFERENCE:
        CallManager::instance().hangUpConference(callId);
        break;
    case call::Type::INVALID:
    default:
        break;
    }
}

void
NewCallModel::refuse(const QString& callId) const
{
    if (!hasCall(callId)) return;
    CallManager::instance().refuse(callId);
}

void
NewCallModel::toggleAudioRecord(const QString& callId) const
{
    CallManager::instance().toggleRecording(callId);
}

void
NewCallModel::playDTMF(const QString& callId, const QString& value) const
{
    if (!hasCall(callId)) return;
    if (pimpl_->calls[callId]->status != call::Status::IN_PROGRESS) return;
    CallManager::instance().playDTMF(value);
}

void
NewCallModel::togglePause(const QString& callId) const
{
    if (!hasCall(callId)) return;
    auto& call = pimpl_->calls[callId];

    if (call->status == call::Status::PAUSED) {
        if (call->type == call::Type::DIALOG) {
            CallManager::instance().unhold(callId);
            setCurrentCall(callId);
        } else {
            CallManager::instance().unholdConference(callId);
        }
    } else if (call->status == call::Status::IN_PROGRESS) {
        if (call->type == call::Type::DIALOG)
            CallManager::instance().hold(callId);
        else {
            CallManager::instance().holdConference(callId);
        }
    }
}

void
NewCallModel::toggleMedia(const QString& callId, const NewCallModel::Media media) const
{
    if (!hasCall(callId)) return;
    auto& call = pimpl_->calls[callId];
    switch(media)
    {
    case NewCallModel::Media::AUDIO:
        CallManager::instance().muteLocalMedia(callId,
                                               DRing::Media::Details::MEDIA_TYPE_AUDIO,
                                               !call->audioMuted);
        call->audioMuted = !call->audioMuted;
        break;

    case NewCallModel::Media::VIDEO:
        CallManager::instance().muteLocalMedia(callId,
                                               DRing::Media::Details::MEDIA_TYPE_VIDEO,
                                               !call->videoMuted);
        call->videoMuted = !call->videoMuted;
        break;

    case NewCallModel::Media::NONE:
    default:
        break;
    }
}

void
NewCallModel::setQuality(const QString& callId, const double quality) const
{
    Q_UNUSED(callId)
    Q_UNUSED(quality)
    qDebug() << "setQuality isn't implemented yet";
}

void
NewCallModel::transfer(const QString& callId, const QString& to) const
{
    CallManager::instance().transfer(callId, to);
}

void
NewCallModel::transferToCall(const QString& callId, const QString& callIdDest) const
{
    CallManager::instance().attendedTransfer(callId, callIdDest);
}

void
NewCallModel::joinCalls(const QString& callIdA, const QString& callIdB) const
{
    // Get call informations
    call::Info call1, call2;
    QString accountIdCall1 = {}, accountIdCall2 = {};
    for (const auto &account_id : owner.accountModel->getAccountList()) {
        try {
            auto &accountInfo = owner.accountModel->getAccountInfo(account_id);
            if (accountInfo.callModel->hasCall(callIdA)) {
                call1 = accountInfo.callModel->getCall(callIdA);
                accountIdCall1 = account_id;
            }
            if (accountInfo.callModel->hasCall(callIdB)) {
                call2 = accountInfo.callModel->getCall(callIdB);
                accountIdCall2 = account_id;
            }
            if (!accountIdCall1.isEmpty() && !accountIdCall2.isEmpty()) break;
        } catch (...) {}
    }
    if (accountIdCall1.isEmpty() || accountIdCall2.isEmpty()) {
        qWarning() << "Can't join inexistent calls.";
        return;
    }

    if (call1.type == call::Type::CONFERENCE && call2.type == call::Type::CONFERENCE) {
        bool joined = CallManager::instance().joinConference(callIdA, callIdB);

        if (!joined) {
            qWarning() << "Conference: " << callIdA << " couldn't join conference " << callIdB;
            return;
        }
        if (accountIdCall1 != owner.id) {
            // If the conference is added from another account
            try {
                auto &accountInfo = owner.accountModel->getAccountInfo(accountIdCall1);
                if (accountInfo.callModel->hasCall(callIdA)) {
                    emit accountInfo.callModel->callAddedToConference(callIdA, callIdB);
                }
            } catch (...) {}
        } else {
            emit callAddedToConference(callIdA, callIdB);
        }
    } else if (call1.type == call::Type::CONFERENCE || call2.type == call::Type::CONFERENCE) {
        auto call = call1.type == call::Type::CONFERENCE ? callIdB : callIdA;
        auto conf = call1.type == call::Type::CONFERENCE ? callIdA : callIdB;
        // Unpause conference if conference was not active
        CallManager::instance().unholdConference(conf);
        auto accountCall = call1.type == call::Type::CONFERENCE ? accountIdCall2 : accountIdCall1;

        bool joined = CallManager::instance().addParticipant(call, conf);
        if (!joined) {
            qWarning() << "Call: " << call << " couldn't join conference " << conf;
            return;
        }
        if (accountCall != owner.id) {
            // If the call is added from another account
            try {
                auto &accountInfo = owner.accountModel->getAccountInfo(accountCall);
                if (accountInfo.callModel->hasCall(call)) {
                    accountInfo.callModel->pimpl_->slotConferenceCreated(conf);
                }
            } catch (...) {}
        } else
            emit callAddedToConference(call, conf);

        // Remove from pendingConferences_
        pimpl_->pendingConferences_.erase(call);
    } else {
        CallManager::instance().joinParticipant(callIdA, callIdB);
        // NOTE: This will trigger slotConferenceCreated.
    }
}

QString
NewCallModel::callAndAddParticipant(const QString uri, const QString& callId, bool audioOnly)
{
    auto newCallId = createCall(uri, audioOnly);
    pimpl_->pendingConferences_.insert({newCallId, callId});
    return newCallId;
}

void
NewCallModel::removeParticipant(const QString& callId, const QString& participant) const
{
    Q_UNUSED(callId)
    Q_UNUSED(participant)
    qDebug() << "removeParticipant() isn't implemented yet";
}

QString
NewCallModel::getFormattedCallDuration(const QString& callId) const
{
    if (!hasCall(callId)) return "00:00";
    auto& startTime = pimpl_->calls[callId]->startTime;
    if (startTime.time_since_epoch().count() == 0) return "00:00";
    auto now = std::chrono::steady_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch() - startTime.time_since_epoch()).count();
    return authority::storage::getFormattedCallDuration(d);
}

bool
NewCallModel::isRecording(const QString& callId) const
{
    if (!hasCall(callId)) return false;
    return CallManager::instance().getIsRecording(callId);
}

QString
NewCallModel::getSIPCallStatusString(const short& statusCode)
{
    auto element = sip_call_status_code_map.find(statusCode);
    if(element != sip_call_status_code_map.end()){
        return element->second;
    }
    return "";
}

NewCallModelPimpl::NewCallModelPimpl(const NewCallModel& linked, const CallbacksHandler& callbacksHandler)
: linked(linked)
, callbacksHandler(callbacksHandler)
{
    connect(&callbacksHandler, &CallbacksHandler::incomingCall, this, &NewCallModelPimpl::slotIncomingCall);
    connect(&callbacksHandler, &CallbacksHandler::callStateChanged, this, &NewCallModelPimpl::slotCallStateChanged);
    connect(&callbacksHandler, &CallbacksHandler::incomingVCardChunk, this, &NewCallModelPimpl::slotincomingVCardChunk);
    connect(&callbacksHandler, &CallbacksHandler::conferenceCreated, this , &NewCallModelPimpl::slotConferenceCreated);
    connect(&callbacksHandler, &CallbacksHandler::voiceMailNotify, this, &NewCallModelPimpl::slotVoiceMailNotify);

#ifndef ENABLE_LIBWRAP
    // Only necessary with dbus since the daemon runs separately
    initCallFromDaemon();
    initConferencesFromDaemon();
#endif
}

NewCallModelPimpl::~NewCallModelPimpl()
{

}

void
NewCallModelPimpl::initCallFromDaemon()
{
    QStringList callList = CallManager::instance().getCallList();
    for (const auto& callId : callList)
    {
        MapStringString details = CallManager::instance().getCallDetails(callId);
        auto accountId = details["ACCOUNTID"];
        if (accountId == linked.owner.id) {
            auto callInfo = std::make_shared<call::Info>();
            callInfo->id = callId;
            auto now = std::chrono::steady_clock::now();
            auto system_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            auto diff = static_cast<int64_t>(system_now) - std::stol(details["TIMESTAMP_START"].toStdString());
            callInfo->startTime = now - std::chrono::seconds(diff);
            callInfo->status = call::to_status(details["CALL_STATE"]);
            auto endId = details["PEER_NUMBER"].indexOf("@");
            callInfo->peerUri = details["PEER_NUMBER"].left(endId);
            if (linked.owner.profileInfo.type == lrc::api::profile::Type::RING) {
                callInfo->peerUri = "ring:" + callInfo->peerUri;
            }
            callInfo->videoMuted = details["VIDEO_MUTED"] == "true";
            callInfo->audioMuted = details["AUDIO_MUTED"] == "true";
            callInfo->type = call::Type::DIALOG;
            calls.emplace(callId, std::move(callInfo));
            // NOTE/BUG: the videorenderer can't know that the client has restarted
            // So, for now, a user will have to manually restart the medias until
            // this renderer is not redesigned.
        }
    }
}

void
NewCallModelPimpl::initConferencesFromDaemon()
{
    QStringList callList = CallManager::instance().getConferenceList();
    for (const auto& callId : callList)
    {
        QMap<QString, QString> details = CallManager::instance().getConferenceDetails(callId);
        auto callInfo = std::make_shared<call::Info>();
        callInfo->id = callId;
        QStringList callList = CallManager::instance().getParticipantList(callId);
        auto isForThisAccount = true;
        foreach(const auto& call, callList) {
            MapStringString callDetails = CallManager::instance().getCallDetails(call);
            isForThisAccount = callDetails["ACCOUNTID"] == linked.owner.id;
            if (!isForThisAccount) break;
            auto now = std::chrono::steady_clock::now();
            auto system_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            auto diff = static_cast<int64_t>(system_now) - std::stol(callDetails["TIMESTAMP_START"].toStdString());
            callInfo->status =  details["CONF_STATE"] == "ACTIVE_ATTACHED"? call::Status::IN_PROGRESS : call::Status::PAUSED;
            callInfo->startTime = now - std::chrono::seconds(diff);
            emit linked.callAddedToConference(call, callId);
        }
        if (!isForThisAccount) break;
        callInfo->type = call::Type::CONFERENCE;
        calls.emplace(callId, std::move(callInfo));
    }
}

void
NewCallModel::setCurrentCall(const QString& callId) const
{
    if (!pimpl_->manageCurrentCall_) return;
    auto it = pimpl_->pendingConferences_.find(callId);
    // Set current call only if not adding this call
    // to a current conference
    if (it != pimpl_->pendingConferences_.end()) return;
    if (!hasCall(callId)) return;

    // The client should be able to set the current call multiple times
    if (pimpl_->currentCall_ == callId) return;
    pimpl_->currentCall_ = callId;

    // Unhold call
    auto& call = pimpl_->calls[callId];
    if (call->status == call::Status::PAUSED) {
        auto& call = pimpl_->calls[callId];
        if (call->type == call::Type::DIALOG) {
            CallManager::instance().unhold(callId);
        } else {
            CallManager::instance().unholdConference(callId);
        }
    }

    VectorString filterCalls;
    QStringList conferences = CallManager::instance().getConferenceList();
    for (const auto& confId : conferences) {
        QStringList callList = CallManager::instance().getParticipantList(confId);
        foreach(const auto& cid, callList) {
            filterCalls.push_back(cid);
        }
    }
    for (const auto& cid : Lrc::activeCalls()) {
        auto filtered = std::find(filterCalls.begin(), filterCalls.end(), cid) != filterCalls.end();
        if (cid != callId && !filtered) {
            CallManager::instance().hold(cid);
        }
    }
    if (!lrc::api::Lrc::holdConferences) {
        return;
    }
    for (const auto& confId : conferences) {
        if (callId != confId) {
            QStringList callList = CallManager::instance().getParticipantList(confId);
            if (callList.indexOf(callId) == -1)
                CallManager::instance().holdConference(confId);
        }
    }
}

void
NewCallModel::sendSipMessage(const QString& callId, const QString& body) const
{
    MapStringString payloads;
    payloads["text/plain"] = body;

    CallManager::instance().sendTextMessage(callId, payloads, true /* not used */);
}

void
NewCallModel::hangupCallsAndConferences()
{
    QStringList conferences = CallManager::instance().getConferenceList();
    for (const auto& conf : conferences) {
        CallManager::instance().hangUpConference(conf);
    }
    QStringList calls = CallManager::instance().getCallList();
    for (const auto &call : calls) {
        CallManager::instance().hangUp(call);
    }
}

void
NewCallModelPimpl::slotIncomingCall(const QString& accountId, const QString& callId, const QString& fromId, const QString& displayname)
{
    if (linked.owner.id != accountId) {
        return;
    }

    // do not use auto here (QDBusPendingReply<MapStringString>)
    MapStringString callDetails = CallManager::instance().getCallDetails(callId);

    auto callInfo = std::make_shared<call::Info>();
    callInfo->id = callId;
    // peer uri = ring:<jami_id> or sip number
    auto uri = (linked.owner.profileInfo.type != profile::Type::SIP && !fromId.contains("ring:")) ? "ring:" + fromId : fromId;
    callInfo->peerUri = uri;
    callInfo->isOutgoing = false;
    callInfo->status =  call::Status::INCOMING_RINGING;
    callInfo->type =  call::Type::DIALOG;
    callInfo->isAudioOnly = callDetails["AUDIO_ONLY"] == "true" ? true : false;
    calls.emplace(callId, std::move(callInfo));

    if (!linked.owner.confProperties.allowIncoming && linked.owner.profileInfo.type == profile::Type::RING) {
        linked.refuse(callId);
        return;
    }

    emit linked.newIncomingCall(fromId, callId, displayname);

    // HACK. BECAUSE THE DAEMON DOESN'T HANDLE THIS CASE!
    if (linked.owner.confProperties.autoAnswer) {
        linked.accept(callId);
    }
}

void
NewCallModelPimpl::slotCallStateChanged(const QString& callId, const QString& state, int code)
{
    if (!linked.hasCall(callId)) return;

    auto status = call::to_status(state);
    auto& call = calls[callId];

    if (status == call::Status::ENDED && !call::isTerminating(call->status)) {
        call->status = call::Status::TERMINATING;
        emit linked.callStatusChanged(callId, code);
    }

    // proper state transition
    auto previousStatus = call->status;
    call->status = status;

    if (previousStatus == call->status) {
        // call state didn't change, simply ignore signal
        return;
    }

    qDebug() << QString("slotCallStateChanged (call: %1), from %2 to %3")
        .arg(callId)
        .arg(call::to_string(previousStatus))
        .arg(call::to_string(status));

    // NOTE: signal emission order matters, always emit CallStatusChanged before CallEnded
    emit linked.callStatusChanged(callId, code);

    if (call->status == call::Status::ENDED) {
        emit linked.callEnded(callId);
    } else if (call->status == call::Status::IN_PROGRESS) {
        if (previousStatus == call::Status::INCOMING_RINGING
                || previousStatus == call::Status::OUTGOING_RINGING) {

            if (previousStatus == call::Status::INCOMING_RINGING
                && linked.owner.profileInfo.type != profile::Type::SIP)
                linked.setCurrentCall(callId);
            call->startTime = std::chrono::steady_clock::now();
            emit linked.callStarted(callId);
            sendProfile(callId);
        }
        // Add to calls if in pendingConferences_
        auto it = pendingConferences_.find(callId);
        if (it != pendingConferences_.end()) {
            linked.joinCalls(it->second, it->first);
        }
    }
}

void
NewCallModelPimpl::slotincomingVCardChunk(const QString& callId,
                                          const QString& from,
                                          int part,
                                          int numberOfParts,
                                          const QString& payload)
{
    if (!linked.hasCall(callId)) return;

    auto it = vcardsChunks.find(from);
    if (it != vcardsChunks.end()) {
        vcardsChunks[from][part-1] = payload;

        if ( not std::any_of(vcardsChunks[from].begin(), vcardsChunks[from].end(),
            [](const auto& s) { return s.isEmpty(); }) ) {

            profile::Info profileInfo;
            profileInfo.uri = from;
            profileInfo.type = profile::Type::RING;

            QString vcardPhoto;

            for (auto& chunk : vcardsChunks[from])
                vcardPhoto += chunk;

            for (auto& e : QString(vcardPhoto).split( "\n" ))
                if (e.contains("PHOTO"))
                    profileInfo.avatar = e.split( ":" )[1];
                else if (e.contains("FN"))
                    profileInfo.alias = e.split( ":" )[1];

            contact::Info contactInfo;
            contactInfo.profileInfo = profileInfo;

            linked.owner.contactModel->addContact(contactInfo);
            vcardsChunks.erase(from); // Transfer is finish, we don't want to reuse this entry.
        }
    } else {
        vcardsChunks[from] = VectorString(numberOfParts);
        vcardsChunks[from][part-1] = payload;
    }
}

void
NewCallModelPimpl::slotVoiceMailNotify(const QString& accountId, int newCount, int oldCount, int urgentCount)
{
    emit linked.voiceMailNotify(accountId, newCount, oldCount, urgentCount);
}

bool
NewCallModel::hasCall(const QString& callId) const
{
    return pimpl_->calls.find(callId) != pimpl_->calls.end();
}

void
NewCallModelPimpl::slotConferenceCreated(const QString& confId)
{
    auto callInfo = std::make_shared<call::Info>();
    callInfo->id = confId;
    callInfo->status =  call::Status::IN_PROGRESS;
    callInfo->type =  call::Type::CONFERENCE;
    callInfo->startTime = std::chrono::steady_clock::now();
    calls[confId] = callInfo;
    QStringList callList = CallManager::instance().getParticipantList(confId);
    foreach(const auto& call, callList) {
        emit linked.callAddedToConference(call, confId);
        // Remove acll from pendingConferences_
        pendingConferences_.erase(call);
    }

}

void
NewCallModelPimpl::sendProfile(const QString& callId)
{
    auto vCard = linked.owner.accountModel->accountVCard(linked.owner.id);

    std::random_device rdev;
    auto key = std::to_string(dis(rdev));

    int i = 0;
    int total = vCard.size()/1000 + (vCard.size()%1000?1:0);
    while (vCard.size()) {
        auto sizeLimit = std::min(1000, static_cast<int>(vCard.size()));
        MapStringString chunk;
        chunk[QString("%1; id=%2,part=%3,of=%4")
               .arg( lrc::vCard::PROFILE_VCF     )
               .arg( key.c_str()                )
               .arg( QString::number( i+1   )   )
               .arg( QString::number( total )   )
            ] = vCard.left(sizeLimit);
        vCard.remove(0, sizeLimit);
        ++i;
        CallManager::instance().sendTextMessage(callId, chunk, false);
    }
}

} // namespace lrc

#include "api/moc_newcallmodel.cpp"
