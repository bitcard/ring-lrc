/****************************************************************************
 *    Copyright (C) 2015-2019 Savoir-faire Linux Inc.                               *
 *   Author : Emmanuel Lepage Vallee <emmanuel.lepage@savoirfairelinux.com> *
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
#include "text.h"

//Dring
#include <media_const.h>
#include "dbus/callmanager.h"
#include "dbus/configurationmanager.h"

//Ring
#include <call.h>
#include <account.h>
#include <person.h>
#include <contactmethod.h>
#include <mime.h>
#include <media/recordingmodel.h>
#include <phonedirectorymodel.h>
#include <private/call_p.h>
#include <private/vcardutils.h>
#include <private/imconversationmanagerprivate.h>
#include <accountmodel.h>
#include <personmodel.h>

/*
 * Instant message have 3 major modes, "past", "in call" and "offline"
 *
 * Offline messages are currently implemented over the DHT and may eventually
 * be implemented over account registration (publish--subscribe)
 *
 * In call messages are the fastest as they have a communication channels of their
 * own.
 *
 * Past messages are local copy of past communication stored somewhere.
 *
 * All 3 sources have to be combined dynamically into a single stream per person.
 *
 * So chunks can come from all 3 sources for the same stream, including part that
 * are shared in a conference with multiple peers.
 */

class MediaTextPrivate
{
public:
   MediaTextPrivate(media::Text* parent);

   //Attributes
   bool                  m_HasChecked;
   QHash<QString,bool>   m_hMimeTypes;
   QStringList           m_lMimeTypes;

   //Helper
   void updateMimeList(const QMap<QString,QString>& payloads);

private:
    media::Text* q_ptr;
};

class ProfileChunk
{
public:
   //Helper
   static Person* addChunk( const QMap<QString,QString>& args, const QString& payload, ContactMethod *contactMethod);

private:
   //Attributes
   QHash<int, QString> m_hParts     {       };
   int                 m_PartsCount { 0     };
   static QHash<QString, ProfileChunk*> m_hRequest;
};

QHash<QString, ProfileChunk*> ProfileChunk::m_hRequest;

IMConversationManagerPrivate::IMConversationManagerPrivate(QObject* parent) : QObject(parent)
{
   CallManagerInterface& callManager                   = CallManager::instance();
   ConfigurationManagerInterface& configurationManager = ConfigurationManager::instance();

   connect(&configurationManager, &ConfigurationManagerInterface::incomingAccountMessage, this, &IMConversationManagerPrivate::newAccountMessage);
   connect(&configurationManager, &ConfigurationManagerInterface::accountMessageStatusChanged  , this, &IMConversationManagerPrivate::accountMessageStatusChanged);
   connect(&callManager         , &CallManagerInterface::incomingMessage                , this, &IMConversationManagerPrivate::newMessage       );
}

IMConversationManagerPrivate& IMConversationManagerPrivate::instance()
{
   static auto instance = new IMConversationManagerPrivate(nullptr);
   return *instance;
}

Person* ProfileChunk::addChunk(const QMap<QString, QString>& args, const QString& payload, ContactMethod *contactMethod)
{
    const int total  = args[ "of"   ].toInt();
    const int part   = args[ "part" ].toInt();
    const QString id = args[ "id"   ];

    auto c = m_hRequest[id];
    if (!c) {
        c = new ProfileChunk();
        c->m_PartsCount = total;
        m_hRequest[id] = c;
    }

    if (part < 1 and part > total)
        return nullptr;

    c->m_hParts[part] = payload;

    if (c->m_hParts.size() != c->m_PartsCount)
        return nullptr;

    QByteArray cv;
    for (int i=0; i < c->m_PartsCount; ++i) {
        cv += c->m_hParts[i+1];
    }

    m_hRequest[id] = nullptr;
    delete c;

    return VCardUtils::mapToPersonFromReceivedProfile(contactMethod, cv);
}

///Called when a new message is incoming
void IMConversationManagerPrivate::newMessage(const QString& callId, const QString& from, const QMap<QString,QString>& message)
{
   return;
}

void IMConversationManagerPrivate::newAccountMessage(const QString& accountId, const QString& from, const QMap<QString,QString>& payloads)
{
}

void IMConversationManagerPrivate::accountMessageStatusChanged(const QString& accountId, uint64_t id, const QString& to, int status)
{
}

MediaTextPrivate::MediaTextPrivate(media::Text* parent) : q_ptr(parent),m_HasChecked(false)
{
}

media::Text::Text(Call* parent, const Media::Direction direction) : media::Media(parent, direction), d_ptr(new MediaTextPrivate(this))
{
   Q_ASSERT(parent);
}

media::Media::Type media::Text::type()
{
   return media::Media::Type::TEXT;
}

media::Text::~Text()
{
   delete d_ptr;
}


bool media::Text::hasMimeType( const QString& mimeType ) const
{
   return d_ptr->m_hMimeTypes.contains(mimeType);
}

QStringList media::Text::mimeTypes() const
{
   return d_ptr->m_lMimeTypes;
}


void MediaTextPrivate::updateMimeList(const QMap<QString,QString>& payloads)
{
   const int prevSize = m_hMimeTypes.size();

   QMapIterator<QString, QString> iter(payloads);

   while (iter.hasNext()) {
      iter.next();

      // Mime types can have some arguments after ';'
      const QString mimeType = iter.key();
      const int hasArgs = mimeType.indexOf(';');
      const QString strippedMimeType = hasArgs != -1 ? mimeType.left(hasArgs) : mimeType;
      const int currentSize = m_hMimeTypes.size();

      m_hMimeTypes[strippedMimeType] = true;

      if (currentSize != m_hMimeTypes.size())
         m_lMimeTypes << strippedMimeType;
   }

   if (prevSize != m_hMimeTypes.size())
      emit q_ptr->mimeTypesChanged();

}

/**
 * Send a message to the peer.
 *
 * @param message A messages encoded in various alternate payloads
 *
 * The send a single messages. Just as e-mails, the message can be
 * encoded differently. The peer client will interpret the richest
 * payload it support and then fallback to lesser ones.
 *
 * Suggested payloads include:
 *
 * "text/plain"    : The most common plain UTF-8 text
 * "text/enriched" : (RTF) The rich text format used by older applications (like WordPad and OS X TextEdit)
 * "text/html"     : The format used by web browsers
 */
void media::Text::send(const QMap<QString,QString>& message, const bool isMixed)
{
   CallManagerInterface& callManager = CallManager::instance();
   Q_NOREPLY callManager.sendTextMessage(call()->dringId(), message, isMixed);

   d_ptr->updateMimeList(message);

   emit messageSent(message);
}

#include <text.moc>
