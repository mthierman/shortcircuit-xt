/*
 * ShortCircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
 * transaction log.
 *
 * ShortCircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortCircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortCircuitXT is inspired by, and shares some code with, the
 * commercial product ShortCircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for ShortCircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortCircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#ifndef SCXT_SRC_MESSAGING_CLIENT_CLIENT_SERIAL_H
#define SCXT_SRC_MESSAGING_CLIENT_CLIENT_SERIAL_H

#include "messaging/messaging.h"

namespace scxt::messaging::client
{
/**
 * These IDs are used inside a session only and are not streamed,
 * so add whatever you want as long as (1) you keep them contig
 * (so don't assign values) and (2) the num_ enum is the last one
 */
enum ClientToSerializationMessagesIds
{
    c2s_request_processor_metadata,
    c2s_request_processor_data,

    num_clientToSerializationMessages
};

enum SerializationToClientMessageIds
{
    s2c_voice_count,

    s2c_respond_processor_metadata,
    s2c_respond_processor_data,

    num_seralizationToClientMessages
};

typedef uint8_t unimpl_t;
template <ClientToSerializationMessagesIds id> struct ClientToSerializationType
{
    typedef unimpl_t T;
};

template <SerializationToClientMessageIds id, typename Client> struct SerializationToClientType
{
    typedef unimpl_t T;
};

template <typename T> void clientSendToSerialization(const T &message, MessageController &mc);
template <typename T>
void serializationSendToClient(SerializationToClientMessageIds id, const T &payload,
                               messaging::MessageController &mc);

void serializationThreadExecuteClientMessage(const std::string &msgView, const engine::Engine &e,
                                             MessageController &mc);
template <typename Client>
void clientThreadExecuteSerializationMessage(const std::string &msgView, Client *c);

} // namespace scxt::messaging::client

#endif // SHORTCIRCUIT_CLIENT_SERIAL_H
