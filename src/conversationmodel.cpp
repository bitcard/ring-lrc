/****************************************************************************
 *    Copyright (C) 2017-2020 Savoir-faire Linux Inc.                       *
 *   Author: Nicolas Jäger <nicolas.jager@savoirfairelinux.com>             *
 *   Author: Sébastien Blin <sebastien.blin@savoirfairelinux.com>           *
 *   Author: Guillaume Roguez <guillaume.roguez@savoirfairelinux.com>       *
 *   Author: Kateryna Kostiuk <kateryna.kostiuk@savoirfairelinux.com>       *
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
#include "api/conversationmodel.h"

 // LRC
#include "api/lrc.h"
#include "api/behaviorcontroller.h"
#include "api/contactmodel.h"
#include "api/newcallmodel.h"
#include "api/newaccountmodel.h"
#include "api/account.h"
#include "api/call.h"
#include "api/datatransfer.h"
#include "api/datatransfermodel.h"
#include "callbackshandler.h"
#include "authority/storagehelper.h"
#include "uri.h"

// Dbus
#include "dbus/configurationmanager.h"
#include "dbus/callmanager.h"

// daemon
#include <account_const.h>
#include <datatransfer_interface.h>

//Qt
#include <QtCore/QTimer>
#include <QFileInfo>

// std
#include <algorithm>
#include <mutex>
#include <regex>
#include <fstream>

namespace lrc
{

using namespace authority;
using namespace api;

class ConversationModelPimpl : public QObject
{
    Q_OBJECT
public:
    ConversationModelPimpl(const ConversationModel& linked,
                           Lrc& lrc,
                           Database& db,
                           const CallbacksHandler& callbacksHandler,
                           const BehaviorController& behaviorController);

    ~ConversationModelPimpl();

    /**
     * return a conversation index from conversations or -1 if no index is found.
     * @param uid of the contact to search.
     * @return an int.
     */
    int indexOf(const QString& uid) const;
    /**
     * return a conversation index from conversations or -1 if no index is found.
     * @param uri of the contact to search.
     * @return an int.
     */
    int indexOfContact(const QString& uri) const;
    /**
     * Initialize conversations_ and filteredConversations_
     */
    void initConversations();
    /**
     * Sort conversation by last action
     */
    void sortConversations();
    /**
     * Call contactModel.addContact if necessary
     * @param contactUri
     */
    void sendContactRequest(const QString& contactUri);
    /**
     * Add a conversation with contactUri
     * @param convId
     * @param contactUri
     */
    void addConversationWith(const QString& convId, const QString& contactUri);
    /**
     * Add call interaction for conversation with callId
     * @param callId
     * @param duration
     */
    void addOrUpdateCallMessage(const QString& callId,
                                const QString& from = {},
                                const std::time_t& duration = -1);
    /**
     * Add a new message from a peer in the database
     * @param from          the author uri
     * @param body          the content of the message
     * @param timestamp     the timestamp of the message
     * @param daemonId      the daemon id
     * @return msgId generated (in db)
     */
    int addIncomingMessage(const QString& from,
                           const QString& body,
                           const uint64_t& timestamp = 0,
                           const QString& daemonId = "");
    /**
     * Change the status of an interaction. Listen from callbacksHandler
     * @param accountId, account linked
     * @param id, interaction to update
     * @param to, peer uri
     * @param status, new status for this interaction
     */
    void slotUpdateInteractionStatus(const QString& accountId,
                                     const uint64_t id,
                                     const QString& to, int status);

    /**
     * place a call
     * @param uid, conversation id
     * @param isAudioOnly, allow to specify if the call is only audio. Set to false by default.
     */
    void placeCall(const QString& uid, bool isAudioOnly = false);

    /**
     * get number of unread messages
     */
    int getNumberOfUnreadMessagesFor(const QString& uid);

    /**
     * Handle data transfer progression
     */
    void updateTransfer(QTimer* timer, const QString& conversation, int conversationIdx,
                        int interactionId);

    bool usefulDataFromDataTransfer(long long dringId, const datatransfer::Info& info,
                                    int& interactionId, QString& convId);

    /**
     * accept a file transfer
     * @param convUid
     * @param interactionId
     * @param final name of the file
     */
    void acceptTransfer(const QString& convUid, uint64_t interactionId, const QString& path);

    const ConversationModel& linked;
    Lrc& lrc;
    Database& db;
    const CallbacksHandler& callbacksHandler;
    const BehaviorController& behaviorController;

    ConversationModel::ConversationQueue conversations; ///< non-filtered conversations
    ConversationModel::ConversationQueue filteredConversations;
    ConversationModel::ConversationQueue customFilteredConversations;
    QString filter;
    profile::Type typeFilter;
    profile::Type customTypeFilter;
    std::pair<bool, bool> dirtyConversations {true, true}; ///< true if filteredConversations/customFilteredConversations must be regenerated
    std::map<QString, std::mutex> interactionsLocks; ///< {convId, mutex}

public Q_SLOTS:
    /**
     * Listen from contactModel when updated (like new alias, avatar, etc.)
     */
    void slotContactModelUpdated(const QString& uri, bool needsSorted);
    /**
     * Listen from contactModel when a new contact is added
     * @param uri
     */
    void slotContactAdded(const QString& contactUri);
    /**
     * Listen from contactModel when a pending contact is accepted
     * @param uri
     */
    void slotPendingContactAccepted(const QString& uri);
    /**
     * Listen from contactModel when aa new contact is removed
     * @param uri
     */
    void slotContactRemoved(const QString& uri);
    /**
     * Listen from callmodel for new calls.
     * @param fromId caller uri
     * @param callId
     */
    void slotIncomingCall(const QString& fromId, const QString& callId);
    /**
     * Listen from callmodel for calls status changed.
     * @param callId
     */
    void slotCallStatusChanged(const QString& callId, int code);
    /**
     * Listen from callmodel for writing "Call started"
     * @param callId
     */
    void slotCallStarted(const QString& callId);
    /**
     * Listen from callmodel for writing "Call ended"
     * @param callId
     */
    void slotCallEnded(const QString& callId);
    /**
     * Listen from CallbacksHandler for new incoming interactions;
     * @param accountId
     * @param msgId
     * @param from uri
     * @param payloads body
     */
    void slotNewAccountMessage(const QString& accountId,
                               const QString& msgId,
                               const QString& from,
                               const MapStringString& payloads);
    /**
     * Listen from CallbacksHandler for new messages in a SIP call
     * @param callId call linked to the interaction
     * @param from author uri
     * @param body of the message
     */
    void slotIncomingCallMessage(const QString& callId, const QString& from, const QString& body);
    /**
     * Listen from CallModel when a call is added to a conference
     * @param callId
     * @param confId
     */
    void slotCallAddedToConference(const QString& callId, const QString& confId);
    /**
     * Listen from CallbacksHandler when a conference is deleted.
     * @param confId
     */
    void slotConferenceRemoved(const QString& confId);
    /**
     * Listen for when a contact is composing
     * @param accountId
     * @param contactUri
     * @param isComposing
     */
    void slotComposingStatusChanged(const QString& accountId, const QString& contactUri, bool isComposing);

    void slotTransferStatusCreated(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusCanceled(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusAwaitingPeer(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusAwaitingHost(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusOngoing(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusFinished(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusError(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusTimeoutExpired(long long dringId, api::datatransfer::Info info);
    void slotTransferStatusUnjoinable(long long dringId, api::datatransfer::Info info);
    void updateTransferStatus(long long dringId, api::datatransfer::Info info, interaction::Status newStatus);
};

ConversationModel::ConversationModel(const account::Info& owner,
                                     Lrc& lrc,
                                     Database& db,
                                     const CallbacksHandler& callbacksHandler,
                                     const BehaviorController& behaviorController)
: QObject()
, pimpl_(std::make_unique<ConversationModelPimpl>(*this, lrc, db, callbacksHandler, behaviorController))
, owner(owner)
{

}

ConversationModel::~ConversationModel()
{

}

const ConversationModel::ConversationQueue&
ConversationModel::allFilteredConversations() const
{
    if (!pimpl_->dirtyConversations.first)
        return pimpl_->filteredConversations;

    pimpl_->filteredConversations = pimpl_->conversations;

    auto it = std::copy_if(
        pimpl_->conversations.begin(), pimpl_->conversations.end(),
        pimpl_->filteredConversations.begin(),
        [this] (const conversation::Info& entry) {
            try {
                auto contactInfo = owner.contactModel->getContact(entry.participants.front());

                auto filter = pimpl_->filter;
                auto uri = URI(filter);
                bool stripScheme = (uri.schemeType() < URI::SchemeType::COUNT__);
                FlagPack<URI::Section> flags = URI::Section::USER_INFO | URI::Section::HOSTNAME | URI::Section::PORT;
                if (!stripScheme) {
                    flags |= URI::Section::SCHEME;
                }

                filter = uri.format(flags);

                /* Check contact */
                // If contact is banned, only match if filter is a perfect match
                if (contactInfo.isBanned) {
                    if (filter == "") return false;
                    return contactInfo.profileInfo.uri == filter
                           || contactInfo.profileInfo.alias == filter
                           || contactInfo.registeredName == filter;
                }

                std::regex regexFilter;
                auto isValidReFilter = true;
                try {
                    regexFilter = std::regex(filter.toStdString(), std::regex_constants::icase);
                } catch(std::regex_error&) {
                    isValidReFilter = false;
                }

                auto filterUriAndReg = [regexFilter, isValidReFilter](auto contact, auto filter) {
                    auto result = contact.profileInfo.uri.contains(filter)
                        || contact.registeredName.contains(filter);
                    if (!result) {
                        auto regexFound = isValidReFilter? (!contact.profileInfo.uri.isEmpty()
                               && std::regex_search(contact.profileInfo.uri.toStdString(), regexFilter))
                               || std::regex_search(contact.registeredName.toStdString(), regexFilter) : false;
                        result |= regexFound;
                    }
                    return result;
                };

                /* Check type */
                if (pimpl_->typeFilter != profile::Type::PENDING) {
                    // Remove pending contacts and get the temporary item if filter is not empty
                    switch (contactInfo.profileInfo.type) {
                    case profile::Type::COUNT__:
                    case profile::Type::INVALID:
                    case profile::Type::PENDING:
                        return false;
                    case profile::Type::TEMPORARY:
                        return filterUriAndReg(contactInfo, filter);
                    case profile::Type::SIP:
                    case profile::Type::RING:
                        break;
                    }
                } else {
                    // We only want pending requests matching with the filter
                    if (contactInfo.profileInfo.type != profile::Type::PENDING)
                        return false;
                }

                // Otherwise perform usual regex search
                bool result = contactInfo.profileInfo.alias.contains(filter);
                if (!result && isValidReFilter)
                    result |= std::regex_search(contactInfo.profileInfo.alias.toStdString(), regexFilter);
                if (!result)
                    result |= filterUriAndReg(contactInfo, filter);
                return result;
            } catch (std::out_of_range&) {
                // getContact() failed
                return false;
            }
    });
    pimpl_->filteredConversations.resize(std::distance(pimpl_->filteredConversations.begin(), it));
    pimpl_->dirtyConversations.first = false;
    return pimpl_->filteredConversations;
}

QMap<ConferenceableItem, ConferenceableValue>
ConversationModel::getConferenceableConversations(const QString& convId, const QString& filter) const
{
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1 || !owner.enabled) {
        return {};
    }
    QMap<ConferenceableItem, ConferenceableValue> result;
    ConferenceableValue callsVector, contactsVector;

    auto currentConfId = pimpl_->conversations.at(conversationIdx).confId;
    auto currentCallId = pimpl_->conversations.at(conversationIdx).callId;
    auto calls = pimpl_->lrc.getCalls();
    auto conferences = pimpl_->lrc.getConferences();
    auto conversations = pimpl_->conversations;
    auto currentAccountID = pimpl_->linked.owner.id;
    //add contacts for current account
    for (const auto &conv : conversations) {
        // conversations with calls will be added in call section
        if(!conv.callId.isEmpty() || !conv.confId.isEmpty()) {
            continue;
        }
        auto contact = owner.contactModel->getContact(conv.participants.front());
        if(contact.isBanned || contact.profileInfo.type == profile::Type::PENDING) {
            continue;
        }
        QVector<AccountConversation> cv;
        AccountConversation accConv = {conv.uid, currentAccountID};
        cv.push_back(accConv);
        if (filter.isEmpty()) {
            contactsVector.push_back(cv);
            continue;
        }
        bool result = contact.profileInfo.alias.contains(filter) ||
                      contact.profileInfo.uri.contains(filter) ||
                      contact.registeredName.contains(filter);
        if (result) {
            contactsVector.push_back(cv);
        }
    }

    if (calls.empty() && conferences.empty()) {
        result.insert(ConferenceableItem::CONTACT, contactsVector);
        return result;
    }

    //filter out calls from conference
    for (const auto& c : conferences) {
        for (const auto& subcal : pimpl_->lrc.getConferenceSubcalls(c)) {
            auto position = std::find(calls.begin(), calls.end(), subcal);
            if (position != calls.end()) {
                calls.erase(position);
            }
        }
    }

    //found conversations and account for calls and conferences
    QMap<QString, QVector<AccountConversation>> tempConferences;
    for (const auto &account_id : pimpl_->lrc.getAccountModel().getAccountList()) {
        try {
            auto &accountInfo = pimpl_->lrc.getAccountModel().getAccountInfo(account_id);
            auto accountConv = accountInfo.conversationModel->getFilteredConversations(accountInfo.profileInfo.type);
            for (const auto &conv : accountConv) {
                bool confFilterPredicate = !conv.confId.isEmpty() && conv.confId != currentConfId &&
                    std::find(conferences.begin(), conferences.end(), conv.confId) != conferences.end();
                bool callFilterPredicate = !conv.callId.isEmpty() && conv.callId != currentCallId &&
                std::find(calls.begin(), calls.end(), conv.callId) != calls.end();

                if (!confFilterPredicate && !callFilterPredicate) {
                    continue;
                }

                // vector of conversationID accountID pair
                // for call has only one entry, for conference multyple
                QVector<AccountConversation> cv;
                AccountConversation accConv = {conv.uid, account_id};
                cv.push_back(accConv);

                bool isConference = !conv.confId.isEmpty();
                //call could be added if it is not conference and in active state
                bool shouldAddCall = false;
                if (!isConference && accountInfo.callModel->hasCall(conv.callId)) {
                    const auto& call = accountInfo.callModel->getCall(conv.callId);
                    shouldAddCall = call.status == lrc::api::call::Status::PAUSED ||
                                    call.status == lrc::api::call::Status::IN_PROGRESS;
                }

                auto contact = accountInfo.contactModel->getContact(conv.participants.front());
                //check if contact satisfy filter
                bool result = (filter.isEmpty() || isConference) ? true :
                              (contact.profileInfo.alias.contains(filter) ||
                              contact.profileInfo.uri.contains(filter) ||
                              contact.registeredName.contains(filter));
                if (!result) {
                    continue;
                }
                if (isConference && tempConferences.count(conv.confId)) {
                    tempConferences.find(conv.confId).value().push_back(accConv);
                } else if (isConference) {
                    tempConferences.insert(conv.confId, cv);
                } else if (shouldAddCall) {
                    callsVector.push_back(cv);
                }
            }
        } catch (...) {}
    }
    for(auto it : tempConferences.toStdMap()) {
        if (filter.isEmpty()) {
            callsVector.push_back(it.second);
            continue;
        }
        for(AccountConversation accConv : it.second) {
            try {
                auto &account = pimpl_->lrc.getAccountModel().getAccountInfo(accConv.accountId);
                auto conv = account.conversationModel->getConversationForUID(accConv.convId);
                auto cont = account.contactModel->getContact(conv.participants.front());
                if ( cont.profileInfo.alias.contains(filter) ||
                     cont.profileInfo.uri.contains(filter) ||
                     cont.registeredName.contains(filter)) {
                     callsVector.push_back(it.second);
                    continue;
                }
            } catch (...) {}
        }
    }
    result.insert(ConferenceableItem::CALL, callsVector);
    result.insert(ConferenceableItem::CONTACT, contactsVector);
    return result;
}

const ConversationModel::ConversationQueue&
ConversationModel::getFilteredConversations(const profile::Type& filter, bool forceUpdate, const bool includeBanned) const
{
    if (pimpl_->customTypeFilter == filter && !pimpl_->dirtyConversations.second && !forceUpdate)
        return pimpl_->customFilteredConversations;

    pimpl_->customTypeFilter = filter;
    pimpl_->customFilteredConversations = pimpl_->conversations;

    auto it = std::copy_if(
        pimpl_->conversations.begin(), pimpl_->conversations.end(),
        pimpl_->customFilteredConversations.begin(),
        [this, &includeBanned] (const conversation::Info& entry) {
            auto contactInfo = owner.contactModel->getContact(entry.participants.front());
            if (!includeBanned && contactInfo.isBanned) return false;
            return (contactInfo.profileInfo.type == pimpl_->customTypeFilter);
        });
    pimpl_->customFilteredConversations.resize(std::distance(pimpl_->customFilteredConversations.begin(), it));
    pimpl_->dirtyConversations.second = false;
    return pimpl_->customFilteredConversations;
}

conversation::Info
ConversationModel::getConversationForUID(const QString& uid) const
{
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1 || !owner.enabled) {
        return {};
    }
    try {
        return pimpl_->conversations.at(conversationIdx);
    } catch (...) {
        return {};
    }
}

conversation::Info
ConversationModel::filteredConversation(const unsigned int row) const
{
    const auto& conversations = allFilteredConversations();
    if (row >= conversations.size())
        return conversation::Info();

    auto conversationInfo = conversations.at(row);

    return conversationInfo;
}

void
ConversationModel::makePermanent(const QString& uid)
{
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1 || !owner.enabled)
        return;

    auto& conversation = pimpl_->conversations.at(conversationIdx);
    if (conversation.participants.empty()) {
        // Should not
        qDebug() << "ConversationModel::addConversation can't add a conversation with no participant";
        return;
    }

    // Send contact request if non used
    pimpl_->sendContactRequest(conversation.participants.front());
}

void
ConversationModel::selectConversation(const QString& uid) const
{
    // Get conversation
    auto conversationIdx = pimpl_->indexOf(uid);

    if (conversationIdx == -1)
        return;

    if (uid.isEmpty() && owner.contactModel->getContact("").profileInfo.uri.isEmpty()) {
        // if we select the temporary contact, check if its a valid contact.
        return;
    }

    auto& conversation = pimpl_->conversations.at(conversationIdx);
    bool callEnded = true;
    if (!conversation.callId.isEmpty()) {
        try  {
            auto call = owner.callModel->getCall(conversation.callId);
            callEnded = call.status == call::Status::ENDED;
        } catch (...) {}
    }

    if (not callEnded and not conversation.confId.isEmpty()) {
        emit pimpl_->behaviorController.showCallView(owner.id, conversation);
    } else if (callEnded) {
        emit pimpl_->behaviorController.showChatView(owner.id, conversation);
    } else {
        try  {
            auto call = owner.callModel->getCall(conversation.callId);
            switch (call.status) {
            case call::Status::INCOMING_RINGING:
            case call::Status::OUTGOING_RINGING:
            case call::Status::CONNECTING:
            case call::Status::SEARCHING:
                // We are currently in a call
                emit pimpl_->behaviorController.showIncomingCallView(owner.id, conversation);
                break;
            case call::Status::PAUSED:
            case call::Status::CONNECTED:
            case call::Status::IN_PROGRESS:
                // We are currently receiving a call
                emit pimpl_->behaviorController.showCallView(owner.id, conversation);
                break;
            case call::Status::PEER_BUSY:
                emit pimpl_->behaviorController.showLeaveMessageView(owner.id, conversation);
                break;
            case call::Status::TIMEOUT:
            case call::Status::TERMINATING:
            case call::Status::INVALID:
            case call::Status::INACTIVE:
                // call just ended
                emit pimpl_->behaviorController.showChatView(owner.id, conversation);
                break;
            case call::Status::ENDED:
            default: // ENDED
                // nothing to do
                break;
            }
        } catch (const std::out_of_range&) {
            // Should not happen
            emit pimpl_->behaviorController.showChatView(owner.id, conversation);
        }
    }
}

void
ConversationModel::removeConversation(const QString& uid, bool banned)
{
    // Get conversation
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1)
        return;

    auto& conversation = pimpl_->conversations.at(conversationIdx);
    if (conversation.participants.empty()) {
        // Should not
        qDebug() << "ConversationModel::removeConversation can't remove a conversation without participant";
        return;
    }

    // Remove contact from daemon
    // NOTE: this will also remove the conversation into the database.
    for (const auto& participant: conversation.participants)
        owner.contactModel->removeContact(participant, banned);
}

void
ConversationModel::deleteObsoleteHistory(int days)
{
    if(days < 1)
        return; // unlimited history

    auto currentTime = static_cast<long int>(std::time(nullptr)); // since epoch, in seconds...
    auto date = currentTime - (days * 86400);

    storage::deleteObsoleteHistory(pimpl_->db, date);
}

void
ConversationModelPimpl::placeCall(const QString& uid, bool isAudioOnly)
{
    auto conversationIdx = indexOf(uid);

    if (conversationIdx == -1 || !linked.owner.enabled)
        return;

    auto& conversation = conversations.at(conversationIdx);
    if (conversation.participants.empty()) {
        // Should not
        qDebug() << "ConversationModel::placeCall can't call a conversation without participant";
        return;
    }

    // Disallow multiple call
    if (!conversation.callId.isEmpty()) {
        try  {
            auto call = linked.owner.callModel->getCall(conversation.callId);
            switch (call.status) {
                case call::Status::INCOMING_RINGING:
                case call::Status::OUTGOING_RINGING:
                case call::Status::CONNECTING:
                case call::Status::SEARCHING:
                case call::Status::PAUSED:
                case call::Status::IN_PROGRESS:
                case call::Status::CONNECTED:
                    return;
                case call::Status::INVALID:
                case call::Status::INACTIVE:
                case call::Status::ENDED:
                case call::Status::PEER_BUSY:
                case call::Status::TIMEOUT:
                case call::Status::TERMINATING:
                default:
                    break;
            }
        } catch (const std::out_of_range&) {
        }
    }

    auto convId = uid;

    auto participant = conversation.participants.front();
    bool isTemporary = participant.isEmpty();
    auto contactInfo = linked.owner.contactModel->getContact(participant);
    auto uri = contactInfo.profileInfo.uri;

    if (uri.isEmpty())
        return; // Incorrect item

    // Don't call banned contact
    if (contactInfo.isBanned) {
        qDebug() << "ContactModel::placeCall: denied, contact is banned";
        return;
    }

    if (linked.owner.profileInfo.type != profile::Type::SIP) {
        uri = "ring:" + uri; // Add the ring: before or it will fail.
    }

    auto cb = std::function<void(QString)>(
        [this, isTemporary, uri, isAudioOnly, &conversation](QString convId) {
            int contactIndex;
            if (isTemporary && (contactIndex = indexOfContact(convId)) < 0) {
                qDebug() << "Can't place call: Other participant is not a contact (removed while placing call ?)";
                return;
            }

            auto& newConv = isTemporary ? conversations.at(contactIndex) : conversation;
            convId = newConv.uid;

            newConv.callId = linked.owner.callModel->createCall(uri, isAudioOnly);
            if (newConv.callId.isEmpty()) {
                qDebug() << "Can't place call (daemon side failure ?)";
                return;
            }

            dirtyConversations = { true, true };
            emit behaviorController.showIncomingCallView(linked.owner.id, newConv);
        });

    if (isTemporary) {
        QMetaObject::Connection* const connection = new QMetaObject::Connection;
        *connection = connect(&this->linked, &ConversationModel::conversationReady,
            [cb, connection](QString convId) {
                cb(convId);
                QObject::disconnect(*connection);
                if (connection) {
                    delete connection;
                }
            });
    }

    sendContactRequest(participant);

    if (!isTemporary) {
        cb(convId);
    }
}

void
ConversationModel::placeAudioOnlyCall(const QString& uid)
{
    pimpl_->placeCall(uid, true);
}

void
ConversationModel::placeCall(const QString& uid)
{
    pimpl_->placeCall(uid);
}

void
ConversationModel::sendMessage(const QString& uid, const QString& body)
{
    // FIXME potential race condition between index check and at() call
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1 || !owner.enabled)
        return;

    auto& conversation = pimpl_->conversations.at(conversationIdx);

    if (conversation.participants.empty()) {
        // Should not
        qDebug() << "ConversationModel::sendMessage can't send a interaction to a conversation with no participant";
        return;
    }

    auto convId = uid;
    bool isTemporary = conversation.participants.front() == "";

    /* Make a copy of participants list: if current conversation is temporary,
       it might me destroyed while we are reading it */
    const auto participants = conversation.participants;

    auto cb = std::function<void(QString)>(
        [this, isTemporary, body, &conversation](QString convId) {
            /* Now we should be able to retrieve the final conversation, in case the previous
               one was temporary */
               // FIXME potential race condition between index check and at() call
            int contactIndex;
            if (isTemporary && (contactIndex = pimpl_->indexOfContact(convId)) < 0) {
                qDebug() << "Can't send message: Other participant is not a contact";
                return;
            }

            uint64_t daemonMsgId = 0;
            auto status = interaction::Status::SENDING;

            auto& newConv = isTemporary ? pimpl_->conversations.at(contactIndex) : conversation;
            convId = newConv.uid;

            // Send interaction to each participant
            for (const auto& participant : newConv.participants) {
                auto contactInfo = owner.contactModel->getContact(participant);

                QStringList callLists = CallManager::instance().getCallList(); // no auto
                // workaround: sometimes, it may happen that the daemon delete a call, but lrc don't. We check if the call is
                //             still valid every time the user want to send a message.
                if (not newConv.callId.isEmpty() and not callLists.contains(newConv.callId))
                    newConv.callId.clear();

                if (not newConv.callId.isEmpty()
                    and call::canSendSIPMessage(owner.callModel->getCall(newConv.callId))) {
                    status = interaction::Status::UNKNOWN;
                    owner.callModel->sendSipMessage(newConv.callId, body);

                } else {
                    daemonMsgId = owner.contactModel->sendDhtMessage(contactInfo.profileInfo.uri, body);
                }

            }

            // Add interaction to database
            interaction::Info msg {
                {},
                body, std::time(nullptr),
                0,
                interaction::Type::TEXT,
                status,
                true
            };
            int msgId = storage::addMessageToConversation(pimpl_->db, convId, msg);

            // Update conversation
            if (status == interaction::Status::SENDING) {
                // Because the daemon already give an id for the message, we need to store it.
                storage::addDaemonMsgId(pimpl_->db, toQString(msgId), toQString(daemonMsgId));
            }

            bool ret = false;

            {
                std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convId]);
                ret = newConv.interactions.insert(std::pair<uint64_t, interaction::Info>(msgId, msg)).second;
            }

            if (!ret) {
                qDebug("ConversationModel::sendMessage failed to send message because an existing key was already present in the database (key = %d)", msgId);
                return;
            }

            newConv.lastMessageUid = msgId;
            pimpl_->dirtyConversations = { true, true };
            // Emit this signal for chatview in the client
            emit newInteraction(convId, msgId, msg);
            // This conversation is now at the top of the list
            pimpl_->sortConversations();
            // The order has changed, informs the client to redraw the list
            emit modelSorted();
        });

    if (isTemporary) {
        QMetaObject::Connection* const connection = new QMetaObject::Connection;
        *connection = connect(this, &ConversationModel::conversationReady,
            [cb, connection](QString convId) {
                cb(convId);
                QObject::disconnect(*connection);
                if (connection) {
                    delete connection;
                }
            });
    }

    /* Check participants list, send contact request if needed.
       NOTE: conferences are not implemented yet, so we have only one participant */
    for (const auto& participant: participants) {
        auto contactInfo = owner.contactModel->getContact(participant);

        if (contactInfo.isBanned) {
            qDebug() << "ContactModel::sendMessage: denied, contact is banned";
            return;
        }

        pimpl_->sendContactRequest(participant);
    }

    if (!isTemporary) {
        cb(convId);
    }
}

void
ConversationModel::refreshFilter()
{
    pimpl_->dirtyConversations = {true, true};
    emit filterChanged();
}

void
ConversationModel::setFilter(const QString& filter)
{
    pimpl_->filter = filter;
    pimpl_->dirtyConversations = {true, true};
    // Will update the temporary contact in the contactModel
    owner.contactModel->searchContact(filter);
    emit filterChanged();
}

void
ConversationModel::setFilter(const profile::Type& filter)
{
    // Switch between PENDING, RING and SIP contacts.
    pimpl_->typeFilter = filter;
    pimpl_->dirtyConversations = {true, true};
    emit filterChanged();
}

void
ConversationModel::joinConversations(const QString& uidA, const QString& uidB)
{
    auto conversationAIdx = pimpl_->indexOf(uidA);
    auto conversationBIdx = pimpl_->indexOf(uidB);
    if (conversationAIdx == -1 || conversationBIdx == -1 || !owner.enabled)
        return;
    auto& conversationA = pimpl_->conversations[conversationAIdx];
    auto& conversationB = pimpl_->conversations[conversationBIdx];

    if (conversationA.callId.isEmpty() || conversationB.callId.isEmpty())
        return;

    if (conversationA.confId.isEmpty()) {
        if(conversationB.confId.isEmpty()){
            owner.callModel->joinCalls(conversationA.callId, conversationB.callId);
        }else{
            owner.callModel->joinCalls(conversationA.callId, conversationB.confId);
            conversationA.confId = conversationB.confId;
        }
    } else {
        if(conversationB.confId.isEmpty()){
            owner.callModel->joinCalls(conversationA.confId, conversationB.callId);
            conversationB.confId = conversationA.confId;
        }else{
            owner.callModel->joinCalls(conversationA.confId, conversationB.confId);
            conversationB.confId = conversationA.confId;
        }
    }
}

void
ConversationModel::clearHistory(const QString& uid)
{
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1)
        return;

    auto& conversation = pimpl_->conversations.at(conversationIdx);
    // Remove all TEXT interactions from database
    storage::clearHistory(pimpl_->db, uid);
    // Update conversation
    {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[uid]);
        conversation.interactions.clear();
    }
    storage::getHistory(pimpl_->db, conversation); // will contains "Conversation started"
    pimpl_->sortConversations();
    emit modelSorted();
    emit conversationCleared(uid);
}

void
ConversationModel::clearInteractionFromConversation(const QString& convId, const uint64_t& interactionId)
{
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1)
        return;

    auto erased_keys = 0;
    bool lastInteractionUpdated = false;
    bool updateDisplayedUid = false;
    uint64_t newDisplayedUid = 0;
    QString participantURI = "";
    {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convId]);
        try
        {
            auto& conversation = pimpl_->conversations.at(conversationIdx);
            storage::clearInteractionFromConversation(pimpl_->db, convId, interactionId);
            erased_keys = conversation.interactions.erase(interactionId);
            auto messageId = conversation.lastDisplayedMessageUid
                             .find(conversation.participants.front());

            if (messageId != conversation.lastDisplayedMessageUid.end() &&
                messageId->second == interactionId) {
                // Update lastDisplayedMessageUid
                for (auto iter = conversation.interactions.find(interactionId); iter != conversation.interactions.end(); --iter) {
                    if (isOutgoing(iter->second) && iter->first != interactionId) {
                        newDisplayedUid = iter->first;
                        break;
                    }
                }
                updateDisplayedUid = true;
                participantURI = conversation.participants.front();
                conversation.lastDisplayedMessageUid
                            .at(conversation.participants.front()) = newDisplayedUid;
            }

            if (conversation.lastMessageUid == interactionId) {
                // Update lastMessageUid
                auto newLastId = 0;
                if (!conversation.interactions.empty())
                    newLastId = conversation.interactions.rbegin()->first;
                conversation.lastMessageUid = newLastId;
                lastInteractionUpdated = true;
            }

        } catch (const std::out_of_range& e) {
            qDebug() << "can't clear interaction from conversation: " << e.what();
        }
    }
    if (updateDisplayedUid) {
        emit displayedInteractionChanged(convId, participantURI, interactionId, newDisplayedUid);
    }
    if (erased_keys > 0) {
        pimpl_->dirtyConversations.first = true;
        emit interactionRemoved(convId, interactionId);
    }
    if (lastInteractionUpdated) {
        // last interaction as changed, so the order can changes.
        pimpl_->sortConversations();
        emit modelSorted();
    }
}

void
ConversationModel::retryInteraction(const QString& convId, const uint64_t& interactionId)
{
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1)
        return;

    auto interactionType = interaction::Type::INVALID;
    QString body = {};
    {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convId]);
        try {
            auto& conversation = pimpl_->conversations.at(conversationIdx);

            auto& interactions = conversation.interactions;
            auto it = interactions.find(interactionId);
            if (it == interactions.end())
                return;

            if (!interaction::isOutgoing(it->second))
                return;  // Do not retry non outgoing info

            if (it->second.type == interaction::Type::TEXT ||
                (it->second.type == interaction::Type::DATA_TRANSFER &&
                    interaction::isOutgoing(it->second))) {
                body = it->second.body;
                interactionType = it->second.type;
            } else
                return;

            storage::clearInteractionFromConversation(pimpl_->db, convId, interactionId);
            conversation.interactions.erase(interactionId);
        } catch (const std::out_of_range& e) {
            qDebug() << "can't find interaction from conversation: " << e.what();
            return;
        }
    }
    emit interactionRemoved(convId, interactionId);

    // Send a new interaction like the previous one
    if (interactionType == interaction::Type::TEXT) {
        sendMessage(convId, body);
    } else {
        // send file
        QFileInfo f(body);
        sendFile(convId, body, f.fileName());
    }
}

bool
ConversationModel::isLastDisplayed(const QString& convId, const uint64_t& interactionId, const QString participant)
{
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1)
        return false;
    try {
        auto& conversation = pimpl_->conversations.at(conversationIdx);
        return conversation.lastDisplayedMessageUid.find(participant)->second == interactionId;
    } catch (const std::out_of_range& e) {
        return false;
    }
}

void
ConversationModel::clearAllHistory()
{
    storage::clearAllHistory(pimpl_->db);

    for (auto& conversation : pimpl_->conversations) {
        {
            std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[conversation.uid]);
            conversation.interactions.clear();
        }
        storage::getHistory(pimpl_->db, conversation);
    }
    pimpl_->sortConversations();
    emit modelSorted();
}

void
ConversationModel::setInteractionRead(const QString& convId,
                                      const uint64_t& interactionId)
{
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1) {
        return;
    }
    bool emitUpdated = false;
    interaction::Info itCopy;
    {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convId]);
        auto& interactions = pimpl_->conversations[conversationIdx].interactions;
        auto it = interactions.find(interactionId);
        if (it != interactions.end()) {
            emitUpdated = true;
            if (it->second.isRead) {
                return;
            }
            it->second.isRead = true;
            if (pimpl_->conversations[conversationIdx].unreadMessages != 0)
                pimpl_->conversations[conversationIdx].unreadMessages -= 1;
            itCopy = it->second;
        }
    }
    if (emitUpdated) {
        pimpl_->dirtyConversations = {true, true};
        auto daemonId = storage::getDaemonIdByInteractionId(pimpl_->db, QString::number(interactionId));
        if (owner.profileInfo.type != profile::Type::SIP) {
            ConfigurationManager::instance().setMessageDisplayed(owner.id, pimpl_->conversations[conversationIdx].participants.front(), daemonId, 3);
        }
        storage::setInteractionRead(pimpl_->db, interactionId);
        emit interactionStatusUpdated(convId, interactionId, itCopy);
        emit pimpl_->behaviorController.newReadInteraction(owner.id, convId, interactionId);
    }
}

void
ConversationModel::clearUnreadInteractions(const QString& convId) {
    auto conversationIdx = pimpl_->indexOf(convId);
    if (conversationIdx == -1) {
        return;
    }
    bool emitUpdated = false;
    QString lastDisplayed;
    {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convId]);
        auto& interactions = pimpl_->conversations[conversationIdx].interactions;
        std::for_each(interactions.begin(), interactions.end(),
                        [&] (decltype(*interactions.begin())& it) {
                            if (!it.second.isRead) {
                                emitUpdated = true;
                                it.second.isRead = true;
                                if (owner.profileInfo.type != profile::Type::SIP)
                                lastDisplayed = storage::getDaemonIdByInteractionId(pimpl_->db, QString::number(it.first));
                                storage::setInteractionRead(pimpl_->db, it.first);
                            }
                        });
    }
    if (!lastDisplayed.isEmpty())
        ConfigurationManager::instance().setMessageDisplayed(
            owner.id, pimpl_->conversations[conversationIdx].participants.front(), lastDisplayed, 3
        );
    if (emitUpdated) {
        pimpl_->conversations[conversationIdx].unreadMessages = 0;
        pimpl_->dirtyConversations = {true, true};
        emit conversationUpdated(convId);
    }
}

ConversationModelPimpl::ConversationModelPimpl(const ConversationModel& linked,
                                               Lrc& lrc,
                                               Database& db,
                                               const CallbacksHandler& callbacksHandler,
                                               const BehaviorController& behaviorController)
: linked(linked)
, lrc {lrc}
, db(db)
, callbacksHandler(callbacksHandler)
, typeFilter(profile::Type::INVALID)
, customTypeFilter(profile::Type::INVALID)
, behaviorController(behaviorController)
{
    initConversations();

    // Contact related
    connect(&*linked.owner.contactModel, &ContactModel::modelUpdated,
            this, &ConversationModelPimpl::slotContactModelUpdated);
    connect(&*linked.owner.contactModel, &ContactModel::contactAdded,
            this, &ConversationModelPimpl::slotContactAdded);
    connect(&*linked.owner.contactModel, &ContactModel::pendingContactAccepted,
            this, &ConversationModelPimpl::slotPendingContactAccepted);
    connect(&*linked.owner.contactModel, &ContactModel::contactRemoved,
            this, &ConversationModelPimpl::slotContactRemoved);

    // Messages related
    connect(&*linked.owner.contactModel, &lrc::ContactModel::newAccountMessage,
            this, &ConversationModelPimpl::slotNewAccountMessage);
    connect(&callbacksHandler, &CallbacksHandler::incomingCallMessage,
            this, &ConversationModelPimpl::slotIncomingCallMessage);
    connect(&callbacksHandler, &CallbacksHandler::accountMessageStatusChanged,
            this, &ConversationModelPimpl::slotUpdateInteractionStatus);

    // Call related
    connect(&*linked.owner.callModel, &NewCallModel::newIncomingCall,
            this, &ConversationModelPimpl::slotIncomingCall);
    connect(&*linked.owner.contactModel, &ContactModel::incomingCallFromPending,
            this, &ConversationModelPimpl::slotIncomingCall);
    connect(&*linked.owner.callModel,
            &lrc::api::NewCallModel::callStatusChanged,
            this,
            &ConversationModelPimpl::slotCallStatusChanged);
    connect(&*linked.owner.callModel,
            &lrc::api::NewCallModel::callStarted,
            this,
            &ConversationModelPimpl::slotCallStarted);
    connect(&*linked.owner.callModel,
            &lrc::api::NewCallModel::callEnded,
            this,
            &ConversationModelPimpl::slotCallEnded);
    connect(&*linked.owner.callModel,
            &lrc::api::NewCallModel::callAddedToConference,
            this,
            &ConversationModelPimpl::slotCallAddedToConference);
    connect(&callbacksHandler,
            &CallbacksHandler::conferenceRemoved,
            this,
            &ConversationModelPimpl::slotConferenceRemoved);
    connect(&ConfigurationManager::instance(),
            &ConfigurationManagerInterface::composingStatusChanged,
            this,
            &ConversationModelPimpl::slotComposingStatusChanged);

    // data transfer
    connect(&*linked.owner.contactModel, &ContactModel::newAccountTransfer,
            this,
            &ConversationModelPimpl::slotTransferStatusCreated);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusCanceled,
            this,
            &ConversationModelPimpl::slotTransferStatusCanceled);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusAwaitingPeer,
            this,
            &ConversationModelPimpl::slotTransferStatusAwaitingPeer);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusAwaitingHost,
            this,
            &ConversationModelPimpl::slotTransferStatusAwaitingHost);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusOngoing,
            this,
            &ConversationModelPimpl::slotTransferStatusOngoing);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusFinished,
            this,
            &ConversationModelPimpl::slotTransferStatusFinished);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusError,
            this,
            &ConversationModelPimpl::slotTransferStatusError);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusTimeoutExpired,
            this,
            &ConversationModelPimpl::slotTransferStatusTimeoutExpired);
    connect(&callbacksHandler,
            &CallbacksHandler::transferStatusUnjoinable,
            this,
            &ConversationModelPimpl::slotTransferStatusUnjoinable);
}

ConversationModelPimpl::~ConversationModelPimpl()
{
    // Contact related
    disconnect(&*linked.owner.contactModel, &ContactModel::modelUpdated,
               this, &ConversationModelPimpl::slotContactModelUpdated);
    disconnect(&*linked.owner.contactModel, &ContactModel::contactAdded,
               this, &ConversationModelPimpl::slotContactAdded);
    disconnect(&*linked.owner.contactModel, &ContactModel::pendingContactAccepted,
               this, &ConversationModelPimpl::slotPendingContactAccepted);
    disconnect(&*linked.owner.contactModel, &ContactModel::contactRemoved,
               this, &ConversationModelPimpl::slotContactRemoved);

    // Messages related
    disconnect(&*linked.owner.contactModel, &lrc::ContactModel::newAccountMessage,
               this, &ConversationModelPimpl::slotNewAccountMessage);
    disconnect(&callbacksHandler, &CallbacksHandler::incomingCallMessage,
               this, &ConversationModelPimpl::slotIncomingCallMessage);
    disconnect(&callbacksHandler, &CallbacksHandler::accountMessageStatusChanged,
               this, &ConversationModelPimpl::slotUpdateInteractionStatus);

    // Call related
    disconnect(&*linked.owner.callModel, &NewCallModel::newIncomingCall,
               this, &ConversationModelPimpl::slotIncomingCall);
    disconnect(&*linked.owner.contactModel, &ContactModel::incomingCallFromPending,
               this, &ConversationModelPimpl::slotIncomingCall);
    disconnect(&*linked.owner.callModel, &lrc::api::NewCallModel::callStatusChanged,
               this, &ConversationModelPimpl::slotCallStatusChanged);
    disconnect(&*linked.owner.callModel, &lrc::api::NewCallModel::callStarted,
               this, &ConversationModelPimpl::slotCallStarted);
    disconnect(&*linked.owner.callModel, &lrc::api::NewCallModel::callEnded,
               this, &ConversationModelPimpl::slotCallEnded);
    disconnect(&*linked.owner.callModel, &lrc::api::NewCallModel::callAddedToConference,
               this, &ConversationModelPimpl::slotCallAddedToConference);
    disconnect(&callbacksHandler, &CallbacksHandler::conferenceRemoved,
               this, &ConversationModelPimpl::slotConferenceRemoved);
    disconnect(&ConfigurationManager::instance(), &ConfigurationManagerInterface::composingStatusChanged,
               this, &ConversationModelPimpl::slotComposingStatusChanged);

    // data transfer
    disconnect(&*linked.owner.contactModel, &ContactModel::newAccountTransfer,
               this, &ConversationModelPimpl::slotTransferStatusCreated);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusCanceled,
               this, &ConversationModelPimpl::slotTransferStatusCanceled);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusAwaitingPeer,
               this, &ConversationModelPimpl::slotTransferStatusAwaitingPeer);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusAwaitingHost,
               this, &ConversationModelPimpl::slotTransferStatusAwaitingHost);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusOngoing,
               this, &ConversationModelPimpl::slotTransferStatusOngoing);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusFinished,
               this, &ConversationModelPimpl::slotTransferStatusFinished);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusError,
               this, &ConversationModelPimpl::slotTransferStatusError);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusTimeoutExpired,
               this, &ConversationModelPimpl::slotTransferStatusTimeoutExpired);
    disconnect(&callbacksHandler, &CallbacksHandler::transferStatusUnjoinable,
               this, &ConversationModelPimpl::slotTransferStatusUnjoinable);
}

void
ConversationModelPimpl::initConversations()
{
    const MapStringString accountDetails = ConfigurationManager::instance().getAccountDetails(linked.owner.id);
    if (accountDetails.empty())
        return;

    // Fill conversations
    for (auto const& c : linked.owner.contactModel->getAllContacts().toStdMap())
    {
        auto conv = storage::getConversationsWithPeer(db, c.second.profileInfo.uri);
        if (conv.empty()) {
            // Can't find a conversation with this contact. Start it.
            auto newConversationsId = storage::beginConversationWithPeer(db, c.second.profileInfo.uri, c.second.isTrusted);
            conv.push_back(std::move(newConversationsId));
        }

        addConversationWith(conv[0], c.first);

        auto convIdx = indexOf(conv[0]);

        // Check if file transfer interactions were left in an incorrect state
        std::lock_guard<std::mutex> lk(interactionsLocks[conversations[convIdx].uid]);
        for(auto& interaction : conversations[convIdx].interactions) {
            if (interaction.second.status == interaction::Status::TRANSFER_CREATED
                || interaction.second.status == interaction::Status::TRANSFER_AWAITING_HOST
                || interaction.second.status == interaction::Status::TRANSFER_AWAITING_PEER
                || interaction.second.status == interaction::Status::TRANSFER_ONGOING
                || interaction.second.status == interaction::Status::TRANSFER_ACCEPTED) {
                // If a datatransfer was left in a non-terminal status in DB, we switch this status to ERROR
                // TODO : Improve for DBus clients as daemon and transfer may still be ongoing
                storage::updateInteractionStatus(db, interaction.first, interaction::Status::TRANSFER_ERROR);
                interaction.second.status = interaction::Status::TRANSFER_ERROR;
            }
        }
    }

    sortConversations();
    filteredConversations = conversations;
    dirtyConversations.first = false;

    // Load all non treated messages for this account
    QVector<Message> messages = ConfigurationManager::instance().getLastMessages(
        linked.owner.id,
        storage::getLastTimestamp(db)
    );
    for (const auto& message : messages) {
        uint64_t timestamp = 0;
        try {
            timestamp = static_cast<uint64_t>(message.received);
        } catch (...) {}
        addIncomingMessage(message.from,
                           message.payloads["text/plain"],
                           timestamp);
    }
}

void
ConversationModelPimpl::sortConversations()
{
    std::sort(
        conversations.begin(), conversations.end(),
        [this](const auto& conversationA, const auto& conversationB)
        {
            // A or B is a temporary contact
            if (conversationA.participants.isEmpty()) return true;
            if (conversationB.participants.isEmpty()) return false;

            if (conversationA.uid == conversationB.uid) return false;

            auto& mtxA = interactionsLocks[conversationA.uid];
            auto& mtxB = interactionsLocks[conversationB.uid];
            std::lock(mtxA, mtxB);
            std::lock_guard<std::mutex> lockConvA(mtxA, std::adopt_lock);
            std::lock_guard<std::mutex> lockConvB(mtxB, std::adopt_lock);

            auto historyA = conversationA.interactions;
            auto historyB = conversationB.interactions;

            // A or B is a new conversation (without CONTACT interaction)
            if (conversationA.uid.isEmpty() || conversationB.uid.isEmpty())
                return conversationA.uid.isEmpty();

            if (historyA.empty() && historyB.empty()) {
                // If no information to compare, sort by Ring ID
                return conversationA.participants.front() > conversationB.participants.front();
            }
            if (historyA.empty()) return false;
            if (historyB.empty()) return true;
            // Sort by last Interaction
            try
            {
                auto lastMessageA = historyA.at(conversationA.lastMessageUid);
                auto lastMessageB = historyB.at(conversationB.lastMessageUid);
                return lastMessageA.timestamp > lastMessageB.timestamp;
            }
            catch (const std::exception& e)
            {
                qDebug() << "ConversationModel::sortConversations(), can't get lastMessage";
                return false;
            }
        });
    dirtyConversations = {true, true};
}

void
ConversationModelPimpl::sendContactRequest(const QString& contactUri)
{
    auto contact = linked.owner.contactModel->getContact(contactUri);
    auto isNotUsed = contact.profileInfo.type == profile::Type::TEMPORARY
        || contact.profileInfo.type == profile::Type::PENDING;
    if (isNotUsed) linked.owner.contactModel->addContact(contact);
}

void
ConversationModelPimpl::slotContactAdded(const QString& contactUri)
{
    auto type = linked.owner.profileInfo.type;
    profile::Info profileInfo{ contactUri, {}, {}, type };
    try {
        auto contact = linked.owner.contactModel->getContact(contactUri);
        type =  contact.profileInfo.type;
        profileInfo.alias = contact.profileInfo.alias;
    } catch (...) {}
    storage::createOrUpdateProfile(linked.owner.id, profileInfo, true);
    auto conv = storage::getConversationsWithPeer(db, profileInfo.uri);
    if (conv.empty()) {
        // pass conversation UID through only element
        conv.push_back(storage::beginConversationWithPeer(db, profileInfo.uri));
    }
    // Add the conversation if not already here
    if (indexOf(conv[0]) == -1) {
        addConversationWith(conv[0], profileInfo.uri);
        emit linked.newConversation(conv[0]);
    }

    // delete temporary conversation if it exists and it has the uri of the added contact as uid
    if (indexOf(profileInfo.uri) >= 0) {
        conversations.erase(conversations.begin() + indexOf(profileInfo.uri));
    }

    sortConversations();
    emit linked.conversationReady(profileInfo.uri);
    emit linked.modelSorted();
}

void
ConversationModelPimpl::slotPendingContactAccepted(const QString& uri)
{
    auto type = linked.owner.profileInfo.type;
    try {
        type = linked.owner.contactModel->getContact(uri).profileInfo.type;
    } catch (std::out_of_range& e) {}
    profile::Info profileInfo{ uri, {}, {}, type };
    storage::createOrUpdateProfile(linked.owner.id, profileInfo, true);
    auto convs = storage::getConversationsWithPeer(db, uri);
    if (convs.empty()) {
        convs.push_back(storage::beginConversationWithPeer(db, uri));
    } else {
        try {
            auto contact = linked.owner.contactModel->getContact(uri);
            auto interaction = interaction::Info { uri,
                                                   {},
                                                   std::time(nullptr),
                                                   0,
                                                   interaction::Type::CONTACT,
                                                   interaction::Status::SUCCESS,
                                                   true };
            auto msgId = storage::addMessageToConversation(db, convs[0], interaction);
            interaction.body = storage::getContactInteractionString(uri, interaction::Status::SUCCESS);
            auto convIdx = indexOf(convs[0]);
            {
                std::lock_guard<std::mutex> lk(interactionsLocks[conversations[convIdx].uid]);
                conversations[convIdx].interactions.emplace(msgId, interaction);
            }
            dirtyConversations = {true, true};
            emit linked.newInteraction(convs[0], msgId, interaction);
        } catch (std::out_of_range& e) {
            qDebug() << "ConversationModelPimpl::slotContactAdded can't find contact";
        }
    }
}

void
ConversationModelPimpl::slotContactRemoved(const QString& uri)
{
    auto conversationIdx = indexOfContact(uri);
    if (conversationIdx == -1) {
        qDebug() << "ConversationModelPimpl::slotContactRemoved, but conversation not found";
        return; // Not a contact
    }
    auto& conversationUid = conversations[conversationIdx].uid;
    conversations.erase(conversations.begin() + conversationIdx);
    dirtyConversations = {true, true};
    emit linked.conversationRemoved(conversationUid);
    emit linked.modelSorted();
}

void
ConversationModelPimpl::slotContactModelUpdated(const QString& uri, bool needsSorted)
{
    // We don't create newConversationItem if we already filter on pending
    conversation::Info newConversationItem;
    if (!filter.isEmpty()) {
        // Create a conversation with the temporary item
        conversation::Info conversationInfo;
        auto& temporaryContact = linked.owner.contactModel->getContact("");
        conversationInfo.uid = temporaryContact.profileInfo.uri;
        conversationInfo.participants.push_back("");
        conversationInfo.accountId = linked.owner.id;

        // if temporary contact is already present, its alias is not empty (namely "Searching ..."),
        // or its registeredName is set because it was found on the nameservice.
        if (not temporaryContact.profileInfo.alias.isEmpty() || not temporaryContact.registeredName.isEmpty()) {
            if (!conversations.empty()) {
                auto firstContactUri = conversations.front().participants.front();
                //if first conversation has uri it is already a contact
                // then we must add temporary item
                if (not firstContactUri.isEmpty()) {
                    conversations.emplace_front(conversationInfo);
                } else if (not conversationInfo.uid.isEmpty()) {
                    // If firstContactUri is empty it means that we have to update
                    // this element as it is the temporary.
                    // Only when we have found an uri.
                    conversations.front() = conversationInfo;
                } else if (not conversations.front().uid.isEmpty()) {
                    //update conversation when uri not found
                    //but conversation have uri from previous search
                    conversations.front() = conversationInfo;
                }
            } else {
                // no conversation, add temporaryItem
                conversations.emplace_front(conversationInfo);
            }
            dirtyConversations = {true, true};
            if (needsSorted) {
                emit linked.modelSorted();
            } else {
                emit linked.conversationUpdated(conversations.front().uid);
            }
            return;
        }
    } else {
        // No filter, so we can remove the newConversationItem
        if (!conversations.empty()) {
            auto firstContactUri = conversations.front().participants.front();

            if (firstContactUri.isEmpty() && needsSorted) {
                conversations.pop_front();
                dirtyConversations = {true, true};
                emit linked.modelSorted();
                return;
            }
        }
    }
    // trigger dirtyConversation in all cases to flush emptied temporary element due to filtered contact present in list
    // TL:DR : avoid duplicates and empty elements
    dirtyConversations = {true, true};
    int index = indexOfContact(uri);
    if (index != -1) {
        if (!conversations.empty() && conversations.front().participants.front().isEmpty() &&
            needsSorted) {
            // In this case, contact is present in list, so temporary item does not longer exists
            emit linked.modelSorted();
        } else {
            // In this case, a presence is updated
            emit linked.conversationUpdated(conversations.at(index).uid);
        }
    }
}

void
ConversationModelPimpl::addConversationWith(const QString& convId,
                                            const QString& contactUri)
{
    conversation::Info conversation;
    conversation.uid = convId;
    conversation.accountId = linked.owner.id;
    conversation.participants = {contactUri};
    try {
        conversation.confId = linked.owner.callModel->getConferenceFromURI(contactUri).id;
    } catch (...) {
        conversation.confId = "";
    }
    try {
        conversation.callId = linked.owner.callModel->getCallFromURI(contactUri).id;
    } catch (...) {
        conversation.callId = "";
    }
    storage::getHistory(db, conversation);
    std::vector<std::function<void(void)>> updateSlots;
    {
        std::lock_guard<std::mutex> lk(interactionsLocks[convId]);
        for (auto& interaction: conversation.interactions) {
            if (interaction.second.status != interaction::Status::SENDING) {
                continue;
            }
            // Get the message status from daemon, else unknown
            auto id = storage::getDaemonIdByInteractionId(db, toQString(interaction.first));
            int status = 0;
            if (id.isEmpty()) {
                continue;
            }
            try {
                auto msgId = std::stoull(id.toStdString());
                status = ConfigurationManager::instance().getMessageStatus(msgId);
                updateSlots.emplace_back(
                    [this, msgId, contactUri, status]() -> void {
                        auto accId = linked.owner.id;
                        slotUpdateInteractionStatus(accId, msgId, contactUri, status);
                    });
            } catch (const std::exception& e) {
                qDebug() << "message id was invalid";
            }
        }
    }
    for (const auto& s: updateSlots) { s(); }

    conversation.unreadMessages = getNumberOfUnreadMessagesFor(convId);
    conversations.emplace_back(conversation);
    dirtyConversations = {true, true};
}

int
ConversationModelPimpl::indexOf(const QString& uid) const
{
    for (unsigned int i = 0; i < conversations.size(); ++i) {
        if (conversations.at(i).uid == uid) return i;
    }
    return -1;
}

int
ConversationModelPimpl::indexOfContact(const QString& uri) const
{
    for (unsigned int i = 0; i < conversations.size(); ++i) {
        if (conversations.at(i).participants.front() == uri) return i;
    }
    return -1;
}

void
ConversationModelPimpl::slotIncomingCall(const QString& fromId, const QString& callId)
{
    auto conversationIdx = indexOfContact(fromId);

    if (conversationIdx == -1) {
        qDebug() << "ConversationModelPimpl::slotIncomingCall, but conversation not found";
        return; // Not a contact
    }

    auto& conversation = conversations.at(conversationIdx);

    qDebug() << "Add call to conversation with " << fromId;
    conversation.callId = callId;
    dirtyConversations = {true, true};
    emit behaviorController.showIncomingCallView(linked.owner.id, conversation);
}

void
ConversationModelPimpl::slotCallStatusChanged(const QString& callId, int code)
{
    Q_UNUSED(code)
    // Get conversation
    auto i = std::find_if(
        conversations.begin(), conversations.end(),
        [callId](const conversation::Info& conversation) {
            return conversation.callId == callId;
        });

    try {
        auto call = linked.owner.callModel->getCall(callId);
        if (i == conversations.end()) {
            // In this case, the user didn't pass through placeCall
            // This means that a participant was invited to a call
            // or a call was placed via dbus.
            // We have to update the model
            for (auto& conversation: conversations) {
                if (conversation.participants.front() == call.peerUri) {
                    conversation.callId = callId;
                    dirtyConversations = {true, true};
                    emit linked.conversationUpdated(conversation.uid);
                }
            }
        } else if (call.status == call::Status::PEER_BUSY) {
            emit behaviorController.showLeaveMessageView(linked.owner.id, *i);
        }
    } catch (std::out_of_range& e) {
        qDebug() << "ConversationModelPimpl::slotCallStatusChanged can't get inexistant call";
    }
}

void
ConversationModelPimpl::slotCallStarted(const QString& callId)
{

    try {
        auto call = linked.owner.callModel->getCall(callId);
        addOrUpdateCallMessage(callId, (!call.isOutgoing ? call.peerUri : ""));
    } catch (std::out_of_range& e) {
        qDebug() << "ConversationModelPimpl::slotCallStarted can't start inexistant call";
    }
}

void
ConversationModelPimpl::slotCallEnded(const QString& callId)
{
    try {
        auto call = linked.owner.callModel->getCall(callId);
        // get duration
        std::time_t duration = 0;
        if (call.startTime.time_since_epoch().count() != 0) {
            auto duration_ns = std::chrono::steady_clock::now() - call.startTime;
            duration = std::chrono::duration_cast<std::chrono::seconds>(duration_ns).count();
        }
        // add or update call interaction with duration
        addOrUpdateCallMessage(callId, (!call.isOutgoing ? call.peerUri : ""), duration);
        /* Reset the callId stored in the conversation.
           Do not call selectConversation() since it is already done in slotCallStatusChanged. */
        for (auto& conversation: conversations)
            if (conversation.callId == callId) {
                conversation.callId = "";
                conversation.confId = ""; // The participant is detached
                dirtyConversations = {true, true};
                emit linked.conversationUpdated(conversation.uid);
            }
    } catch (std::out_of_range& e) {
        qDebug() << "ConversationModelPimpl::slotCallEnded can't end inexistant call";
    }
}

void
ConversationModelPimpl::addOrUpdateCallMessage(const QString& callId,
                                               const QString& from,
                                               const std::time_t& duration)
{
    // Get conversation
    auto conv_it = std::find_if(conversations.begin(), conversations.end(),
        [&callId](const conversation::Info& conversation) {
            return conversation.callId == callId;
        });
    if (conv_it == conversations.end()) {
        return;
    }
    auto uid = conv_it->uid;
    auto uriString = storage::prepareUri(from, linked.owner.profileInfo.type);
    auto msg = interaction::Info { uriString, {}, std::time(nullptr), duration,
                                 interaction::Type::CALL, interaction::Status::SUCCESS, true };
    // update the db
    int msgId = storage::addOrUpdateMessage(db, conv_it->uid, msg, callId);
    // now set the formatted call message string in memory only
    msg.body = storage::getCallInteractionString(uriString, duration);
    auto newInteraction = conv_it->interactions.find(msgId) == conv_it->interactions.end();
    if (newInteraction) {
        conv_it->lastMessageUid = msgId;
        std::lock_guard<std::mutex> lk(interactionsLocks[conv_it->uid]);
        conv_it->interactions.emplace(msgId, msg);
    } else {
        std::lock_guard<std::mutex> lk(interactionsLocks[conv_it->uid]);
        conv_it->interactions[msgId] = msg;
    }
    dirtyConversations = { true, true };
    if (newInteraction)
        emit linked.newInteraction(conv_it->uid, msgId, msg);
    else
        emit linked.interactionStatusUpdated(conv_it->uid, msgId, msg);
    sortConversations();
    emit linked.modelSorted();
}

void
ConversationModelPimpl::slotNewAccountMessage(const QString& accountId,
                                              const QString& msgId,
                                              const QString& from,
                                              const MapStringString& payloads)
{
    if (accountId != linked.owner.id)
        return;

    for (const auto &payload : payloads.keys()) {
        if (payload.contains("text/plain")) {
            addIncomingMessage(from, payloads.value(payload), 0, msgId);
        }
    }
}

void
ConversationModelPimpl::slotIncomingCallMessage(const QString& callId, const QString& from, const QString& body)
{
    if (not linked.owner.callModel->hasCall(callId))
        return;

    auto& call = linked.owner.callModel->getCall(callId);
    if (call.type == call::Type::CONFERENCE) {
        // Show messages in all conversations for conferences.
        for (const auto& conversation: conversations) {
            if (conversation.confId == callId) {
                if (conversation.participants.empty()) {
                    continue;
                }
                addIncomingMessage(from, body);
            }
        }
    } else {
        addIncomingMessage(from, body);
    }

}

int
ConversationModelPimpl::addIncomingMessage(const QString& from,
                                           const QString& body,
                                           const uint64_t& timestamp,
                                           const QString& daemonId)
{
    auto convIds = storage::getConversationsWithPeer(db, from);
    if (convIds.empty()) {
        convIds.push_back(storage::beginConversationWithPeer(db, from, false));
    }
    auto msg = interaction::Info { from, body,
                                  timestamp == 0 ? std::time(nullptr) : static_cast<time_t>(timestamp), 0,
                                  interaction::Type::TEXT, interaction::Status::SUCCESS, false};
    auto msgId = storage::addMessageToConversation(db, convIds[0], msg);
    if (!daemonId.isEmpty()) {
        storage::addDaemonMsgId(db, QString::number(msgId), daemonId);
    }
    auto conversationIdx = indexOf(convIds[0]);
    // Add the conversation if not already here
    if (conversationIdx == -1) {
        addConversationWith(convIds[0], from);
        emit linked.newConversation(convIds[0]);
    } else {
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[conversations[conversationIdx].uid]);
            conversations[conversationIdx].interactions.emplace(msgId, msg);
        }
        conversations[conversationIdx].lastMessageUid = msgId;
        conversations[conversationIdx].unreadMessages = getNumberOfUnreadMessagesFor(convIds[0]);
    }
    dirtyConversations = {true, true};
    emit behaviorController.newUnreadInteraction(linked.owner.id, convIds[0], msgId, msg);
    emit linked.newInteraction(convIds[0], msgId, msg);
    sortConversations();
    emit linked.modelSorted();
    return msgId;
}

void
ConversationModelPimpl::slotCallAddedToConference(const QString& callId, const QString& confId)
{
    for (auto& conversation: conversations) {
        if (conversation.callId == callId || conversation.confId == confId) {
            conversation.confId = confId;
            dirtyConversations = {true, true};
            emit linked.selectConversation(conversation.uid);
        }
    }
}

void
ConversationModelPimpl::slotUpdateInteractionStatus(const QString& accountId,
                                                    const uint64_t daemon_id,
                                                    const QString& peer_uri,
                                                    int status)
{
    if (accountId != linked.owner.id) {
        return;
    }
    auto newStatus = interaction::Status::INVALID;
    switch (static_cast<DRing::Account::MessageStates>(status))
    {
    case DRing::Account::MessageStates::SENDING:
        newStatus = interaction::Status::SENDING;
        break;
    case DRing::Account::MessageStates::CANCELLED:
        newStatus = interaction::Status::TRANSFER_CANCELED;
        break;
    case DRing::Account::MessageStates::SENT:
        newStatus = interaction::Status::SUCCESS;
        break;
    case DRing::Account::MessageStates::FAILURE:
        newStatus = interaction::Status::FAILURE;
        break;
    case DRing::Account::MessageStates::DISPLAYED:
        newStatus = interaction::Status::DISPLAYED;
        break;
    case DRing::Account::MessageStates::UNKNOWN:
    default:
        newStatus = interaction::Status::UNKNOWN;
        break;
    }
    // Update database
    auto interactionId = storage::getInteractionIdByDaemonId(db, toQString(daemon_id));
    if (interactionId.isEmpty()) {
        return;
    }
    auto msgId = std::stoull(interactionId.toStdString());
    storage::updateInteractionStatus(db, msgId, newStatus);
    // Update conversations
    auto convIds = storage::getConversationsWithPeer(db, peer_uri);
    if (!convIds.empty()) {
        auto conversationIdx = indexOf(convIds[0]);
        interaction::Info itCopy;
        bool emitUpdated = false;
        bool updateDisplayedUid = false;
        uint64_t oldDisplayedUid = 0;
        if (conversationIdx != -1) {
            std::lock_guard<std::mutex> lk(interactionsLocks[conversations[conversationIdx].uid]);
            auto& interactions = conversations[conversationIdx].interactions;
            auto it = interactions.find(msgId);
            auto messageId = conversations[conversationIdx].lastDisplayedMessageUid.find(peer_uri);
            if (it != interactions.end()) {
                it->second.status = newStatus;
                if (messageId != conversations[conversationIdx].lastDisplayedMessageUid.end()) {
                    auto lastDisplayedIt = interactions.find(messageId->second);
                    bool interactionDisplayed = newStatus == interaction::Status::DISPLAYED
                                                             && isOutgoing(it->second);
                    bool interactionIsLast = lastDisplayedIt == interactions.end() ||
                          lastDisplayedIt->second.timestamp < it->second.timestamp;
                    updateDisplayedUid = interactionDisplayed && interactionIsLast;
                    if (updateDisplayedUid) {
                        oldDisplayedUid = messageId->second;
                        conversations[conversationIdx].lastDisplayedMessageUid.at(peer_uri) = it->first;
                    }
                }
                emitUpdated = true;
                itCopy = it->second;
            }
        }
        if (updateDisplayedUid) {
            emit linked.displayedInteractionChanged(convIds[0], peer_uri, oldDisplayedUid, msgId);
        }
        if (emitUpdated) {
            dirtyConversations = { true, true };
            emit linked.interactionStatusUpdated(convIds[0], msgId, itCopy);
        }
    }
}

void
ConversationModelPimpl::slotConferenceRemoved(const QString& confId)
{
    // Get conversation
    for(auto& i : conversations) {
        if (i.confId == confId) {
            i.confId = "";
        }
    }
}

void
ConversationModelPimpl::slotComposingStatusChanged(const QString& accountId, const QString& contactUri, bool isComposing)
{
    if (accountId != linked.owner.id) return;
    // Check conversation's validity
    auto convIds = storage::getConversationsWithPeer(db, contactUri);
    if (convIds.empty()) return;
    emit linked.composingStatusChanged(convIds.front(), contactUri, isComposing);
}

int
ConversationModelPimpl::getNumberOfUnreadMessagesFor(const QString& uid)
{
    return storage::countUnreadFromInteractions(db, uid);
}

void
ConversationModel::setIsComposing(const QString& uid, bool isComposing)
{
    auto conversationIdx = pimpl_->indexOf(uid);
    if (conversationIdx == -1 || !owner.enabled)
        return;

    const auto peerUri = pimpl_->conversations[conversationIdx].participants.front();
    ConfigurationManager::instance().setIsComposing(owner.id, peerUri, isComposing);
}

void
ConversationModel::sendFile(const QString& convUid,
                            const QString& path,
                            const QString& filename)
{
    auto conversationIdx = pimpl_->indexOf(convUid);
    if (conversationIdx == -1 || !owner.enabled)
        return;

    const auto peerUri = pimpl_->conversations[conversationIdx].participants.front();
    bool isTemporary = peerUri.isEmpty();

    /* It is necessary to make a copy of convUid since it may very well point to
       a field in the temporary conversation, which is going to be destroyed by
       slotContactAdded() (indirectly triggered by sendContactrequest(). Not doing
       so may result in use after free/crash. */
    auto convUidCopy = convUid;

    pimpl_->sendContactRequest(peerUri);

    auto cb = std::function<void(QString)>(
        [this, isTemporary, peerUri, path, filename](QString convId) {
            int contactIndex;
            if (isTemporary && (contactIndex = pimpl_->indexOfContact(convId)) < 0) {
                qDebug() << "Can't send file: Other participant is not a contact (removed while sending file ?)";
                return;
            }

            // Retrieve final peer uri after creation of the conversation
            const auto& newPeerUri = isTemporary ? pimpl_->conversations.at(contactIndex).participants.front() : peerUri;

            auto contactInfo = owner.contactModel->getContact(newPeerUri);
            if (contactInfo.isBanned) {
                qDebug() << "ContactModel::sendFile: denied, contact is banned";
                return;
            }

            pimpl_->lrc.getDataTransferModel().sendFile(owner.id,
                                                        newPeerUri,
                                                        path,
                                                        filename);
        });

    if (isTemporary) {
        QMetaObject::Connection* const connection = new QMetaObject::Connection;
        *connection = connect(this, &ConversationModel::conversationReady,
            [cb, connection](QString convId) {
                cb(convId);
                QObject::disconnect(*connection);
                if (connection) {
                    delete connection;
                }
            });
    } else {
        cb(convUidCopy);
    }
}

void
ConversationModel::acceptTransfer(const QString& convUid, uint64_t interactionId)
{
    lrc::api::datatransfer::Info info = {};
    getTransferInfo(interactionId, info);
    acceptTransfer(convUid, interactionId, info.displayName);
}

void
ConversationModel::acceptTransfer(const QString& convUid, uint64_t interactionId, const QString& path)
{
    pimpl_->acceptTransfer(convUid, interactionId, path);
}

void
ConversationModel::cancelTransfer(const QString& convUid, uint64_t interactionId)
{
    // For this action, we change interaction status before effective canceling as daemon will
    // emit Finished event code immediately (before leaving this method) in non-DBus mode.
    auto conversationIdx = pimpl_->indexOf(convUid);
    interaction::Info itCopy;
    bool emitUpdated = false;
    if (conversationIdx != -1) {
        std::lock_guard<std::mutex> lk(pimpl_->interactionsLocks[convUid]);
        auto& interactions = pimpl_->conversations[conversationIdx].interactions;
        auto it = interactions.find(interactionId);
        if (it != interactions.end()) {
            it->second.status = interaction::Status::TRANSFER_CANCELED;

            // update information in the db
            storage::updateInteractionStatus(pimpl_->db, interactionId, interaction::Status::TRANSFER_CANCELED);
            emitUpdated = true;
            itCopy = it->second;
        }
    }
    if (emitUpdated) {
        // Forward cancel action to daemon (will invoke slotTransferStatusCanceled)
        pimpl_->lrc.getDataTransferModel().cancel(interactionId);
        pimpl_->dirtyConversations = {true, true};
        emit interactionStatusUpdated(convUid, interactionId, itCopy);
        emit pimpl_->behaviorController.newReadInteraction(owner.id, convUid, interactionId);
    }
}

void
ConversationModel::getTransferInfo(uint64_t interactionId, datatransfer::Info& info)
{
    try {
        auto dringId = pimpl_->lrc.getDataTransferModel().getDringIdFromInteractionId(interactionId);
        pimpl_->lrc.getDataTransferModel().transferInfo(dringId, info);
    } catch (...) {
        info.status = datatransfer::Status::INVALID;
    }
}

int
ConversationModel::getNumberOfUnreadMessagesFor(const QString& convUid)
{
    return pimpl_->getNumberOfUnreadMessagesFor(convUid);
}

bool
ConversationModelPimpl::usefulDataFromDataTransfer(long long dringId, const datatransfer::Info& info,
                                                   int& interactionId, QString& convId)
{
    if (info.accountId != linked.owner.id) return false;
    try {
        interactionId = lrc.getDataTransferModel().getInteractionIdFromDringId(dringId);
    } catch (const std::out_of_range& e) {
        qWarning() << "Couldn't get interaction from daemon Id: " << dringId;
        return false;
    }

    convId = storage::conversationIdFromInteractionId(db, interactionId);
    return true;
}

void
ConversationModelPimpl::slotTransferStatusCreated(long long dringId, datatransfer::Info info)
{
    // check if transfer is for the current account
    if (info.accountId != linked.owner.id) return;

    const MapStringString accountDetails = ConfigurationManager::instance().getAccountDetails(linked.owner.id);
    if (accountDetails.empty())
        return;
    // create a new conversation if needed
    auto convIds = storage::getConversationsWithPeer(db, info.peerUri);
    if (convIds.empty()) {
        convIds.push_back(storage::beginConversationWithPeer(db, info.peerUri, false));
    }

    // add interaction to the db
    const auto& convId = convIds[0];
    auto interactionId = storage::addDataTransferToConversation(db, convId, info);

    // map dringId and interactionId for latter retrivial from client (that only known the interactionId)
    lrc.getDataTransferModel().registerTransferId(dringId, interactionId);

    auto interaction = interaction::Info{ info.isOutgoing ? "" : info.peerUri,
                                          info.isOutgoing ? info.path : info.displayName,
                                          std::time(nullptr), 0,
                                          interaction::Type::DATA_TRANSFER,
                                          interaction::Status::TRANSFER_CREATED, false};

    // prepare interaction Info and emit signal for the client
    auto conversationIdx = indexOf(convId);
    if (conversationIdx == -1) {
        addConversationWith(convId, info.peerUri);
        emit linked.newConversation(convId);
    } else {
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[conversations[conversationIdx].uid]);
            conversations[conversationIdx].interactions.emplace(interactionId, interaction);
        }
        conversations[conversationIdx].lastMessageUid = interactionId;
        conversations[conversationIdx].unreadMessages = getNumberOfUnreadMessagesFor(convId);
    }
    dirtyConversations = {true, true};
    emit behaviorController.newUnreadInteraction(linked.owner.id, convId, interactionId, interaction);
    emit linked.newInteraction(convId, interactionId, interaction);
    sortConversations();
    emit linked.modelSorted();
}

void
ConversationModelPimpl::slotTransferStatusAwaitingPeer(long long dringId, datatransfer::Info info)
{
    updateTransferStatus(dringId, info, interaction::Status::TRANSFER_AWAITING_PEER);
}

void
ConversationModelPimpl::slotTransferStatusAwaitingHost(long long dringId, datatransfer::Info info)
{
    int interactionId;
    QString convId;
    if (not usefulDataFromDataTransfer(dringId, info, interactionId, convId))
        return;

    auto newStatus = interaction::Status::TRANSFER_AWAITING_HOST;
    storage::updateInteractionStatus(db, interactionId, newStatus);

    auto conversationIdx = indexOf(convId);
    if (conversationIdx != -1) {
        bool emitUpdated = false;
        interaction::Info itCopy;
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[convId]);
            auto& interactions = conversations[conversationIdx].interactions;
            auto it = interactions.find(interactionId);
            if (it != interactions.end()) {
                emitUpdated = true;
                it->second.status = newStatus;
                itCopy = it->second;
            }
        }
        if (emitUpdated) {
            dirtyConversations = {true, true};
            emit linked.interactionStatusUpdated(convId, interactionId, itCopy);
            // Only accept if contact is added
            if (!lrc.getDataTransferModel().acceptFromUnstrusted) {
                try {
                    auto contactUri = conversations[conversationIdx].participants.front();
                    auto contactInfo = linked.owner.contactModel->getContact(contactUri);
                    if (contactInfo.profileInfo.type == profile::Type::PENDING) return;
                } catch (...) {
                    return;
                }
            }
            // If it's an accepted file type and less than 20 MB, accept transfer.
            if (lrc.getDataTransferModel().automaticAcceptTransfer) {
                if (lrc.getDataTransferModel().acceptBehindMb == 0 || info.totalSize < lrc.getDataTransferModel().acceptBehindMb * 1024 * 1024)
                    acceptTransfer(convId, interactionId, info.displayName);
            }
        }
    }
}

void
ConversationModelPimpl::acceptTransfer(const QString& convUid, uint64_t interactionId, const QString& path)
{
    auto destinationDir = lrc.getDataTransferModel().downloadDirectory;
    if (destinationDir.isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    if (destinationDir.right(1) != '/') {
        destinationDir += "/";
    }
#endif
    QDir dir = QFileInfo(destinationDir + path).absoluteDir();
    if (!dir.exists())
        dir.mkpath(".");
    auto acceptedFilePath = lrc.getDataTransferModel().accept(interactionId, destinationDir + path, 0);
    storage::updateInteractionBody(db, interactionId, acceptedFilePath);
    storage::updateInteractionStatus(db, interactionId, interaction::Status::TRANSFER_ACCEPTED);

    // prepare interaction Info and emit signal for the client
    auto conversationIdx = indexOf(convUid);
    interaction::Info itCopy;
    bool emitUpdated = false;
    if (conversationIdx != -1) {
        std::lock_guard<std::mutex> lk(interactionsLocks[convUid]);
        auto& interactions = conversations[conversationIdx].interactions;
        auto it = interactions.find(interactionId);
        if (it != interactions.end()) {
            it->second.body = acceptedFilePath;
            it->second.status = interaction::Status::TRANSFER_ACCEPTED;
            emitUpdated = true;
            itCopy = it->second;
        }
    }
    if (emitUpdated) {
        sendContactRequest(conversations[conversationIdx].participants.front());
        dirtyConversations = {true, true};
        emit linked.interactionStatusUpdated(convUid, interactionId, itCopy);
        emit behaviorController.newReadInteraction(linked.owner.id, convUid, interactionId);
    }
}

void
ConversationModelPimpl::slotTransferStatusOngoing(long long dringId, datatransfer::Info info)
{
    int interactionId;
    QString convId;
    if (not usefulDataFromDataTransfer(dringId, info, interactionId, convId))
        return;

    auto newStatus = interaction::Status::TRANSFER_ONGOING;
    storage::updateInteractionStatus(db, interactionId, newStatus);

    auto conversationIdx = indexOf(convId);
    if (conversationIdx != -1) {
        bool emitUpdated = false;
        interaction::Info itCopy;
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[convId]);
            auto& interactions = conversations[conversationIdx].interactions;
            auto it = interactions.find(interactionId);
            if (it != interactions.end()) {
                emitUpdated = true;
                it->second.status = newStatus;
                itCopy = it->second;
            }
        }
        if (emitUpdated) {
            auto* timer = new QTimer();
            connect(timer, &QTimer::timeout,
                    [=] { updateTransfer(timer, convId, conversationIdx, interactionId); });
            timer->start(1000);
            dirtyConversations = {true, true};
            emit linked.interactionStatusUpdated(convId, interactionId, itCopy);
        }
    }
}

void
ConversationModelPimpl::slotTransferStatusFinished(long long dringId, datatransfer::Info info)
{
    int interactionId;
    QString convId;
    if (not usefulDataFromDataTransfer(dringId, info, interactionId, convId))
        return;

    // prepare interaction Info and emit signal for the client
    auto conversationIdx = indexOf(convId);
    if (conversationIdx != -1) {
        bool emitUpdated = false;
        auto newStatus = interaction::Status::TRANSFER_FINISHED;
        interaction::Info itCopy;
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[convId]);
            auto& interactions = conversations[conversationIdx].interactions;
            auto it = interactions.find(interactionId);
            if (it != interactions.end()) {
                // We need to check if current status is ONGOING as CANCELED must not be transformed into FINISHED
                if (it->second.status == interaction::Status::TRANSFER_ONGOING) {
                    emitUpdated = true;
                    it->second.status = newStatus;
                    itCopy = it->second;
                }
            }
        }
        if (emitUpdated) {
            dirtyConversations = {true, true};
            storage::updateInteractionStatus(db, interactionId, newStatus);
            emit linked.interactionStatusUpdated(convId, interactionId, itCopy);
        }
    }
}

void
ConversationModelPimpl::slotTransferStatusCanceled(long long dringId, datatransfer::Info info)
{
    updateTransferStatus(dringId, info, interaction::Status::TRANSFER_CANCELED);
}

void
ConversationModelPimpl::slotTransferStatusError(long long dringId, datatransfer::Info info)
{
    updateTransferStatus(dringId, info, interaction::Status::TRANSFER_ERROR);
}

void
ConversationModelPimpl::slotTransferStatusUnjoinable(long long dringId, datatransfer::Info info)
{
    updateTransferStatus(dringId, info, interaction::Status::TRANSFER_UNJOINABLE_PEER);
}

void
ConversationModelPimpl::slotTransferStatusTimeoutExpired(long long dringId, datatransfer::Info info)
{
    updateTransferStatus(dringId, info, interaction::Status::TRANSFER_TIMEOUT_EXPIRED);
}

void
ConversationModelPimpl::updateTransferStatus(long long dringId, datatransfer::Info info, interaction::Status newStatus)
{
    int interactionId;
    QString convId;
    if (not usefulDataFromDataTransfer(dringId, info, interactionId, convId))
        return;

    // update information in the db
    storage::updateInteractionStatus(db, interactionId, newStatus);

    // prepare interaction Info and emit signal for the client
    auto conversationIdx = indexOf(convId);
    if (conversationIdx != -1) {
        bool emitUpdated = false;
        interaction::Info itCopy;
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[convId]);
            auto& interactions = conversations[conversationIdx].interactions;
            auto it = interactions.find(interactionId);
            if (it != interactions.end()) {
                emitUpdated = true;
                it->second.status = newStatus;
                itCopy = it->second;
            }
        }
        if (emitUpdated) {
            dirtyConversations = {true, true};
            emit linked.interactionStatusUpdated(convId, interactionId, itCopy);
        }
    }
}

void
ConversationModelPimpl::updateTransfer(QTimer* timer, const QString& conversation,
                                       int conversationIdx, int interactionId)
{
    try {
        bool emitUpdated = false;
        interaction::Info itCopy;
        {
            std::lock_guard<std::mutex> lk(interactionsLocks[conversations[conversationIdx].uid]);
            const auto& interactions = conversations[conversationIdx].interactions;
            const auto& it = interactions.find(interactionId);
            if (it != std::cend(interactions)
                and it->second.status == interaction::Status::TRANSFER_ONGOING) {
                emitUpdated = true;
                itCopy = it->second;
            }
        }
        if (emitUpdated) {
            emit linked.interactionStatusUpdated(conversation, interactionId, itCopy);
            return;
        }
    } catch (...) {}

    timer->stop();
    delete timer;
}

} // namespace lrc

#include "api/moc_conversationmodel.cpp"
#include "conversationmodel.moc"
