// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/net.h>

#include <chainparams.h>
#include <net.h>

#ifdef USE_WAKEUP_PIPE
#include <fcntl.h>
#include <unistd.h>
#endif

void ConnmanTestMsg::NodeReceiveMsgBytes(CNode& node, Span<const uint8_t> msg_bytes, bool& complete) const
{
    assert(node.ReceiveMsgBytes(msg_bytes, complete));
    if (complete) {
        size_t nSizeAdded = 0;
        auto it(node.vRecvMsg.begin());
        for (; it != node.vRecvMsg.end(); ++it) {
            // vRecvMsg contains only completed CNetMessage
            // the single possible partially deserialized message are held by TransportDeserializer
            nSizeAdded += it->m_raw_message_size;
        }
        {
            LOCK(node.cs_vProcessMsg);
            node.vProcessMsg.splice(node.vProcessMsg.end(), node.vRecvMsg, node.vRecvMsg.begin(), it);
            node.nProcessQueueSize += nSizeAdded;
            node.fPauseRecv = node.nProcessQueueSize > nReceiveFloodSize;
        }
    }
}

bool ConnmanTestMsg::ReceiveMsgFrom(CNode& node, CSerializedNetMsg& ser_msg) const
{
    std::vector<uint8_t> ser_msg_header;
    node.m_serializer->prepareForTransport(ser_msg, ser_msg_header);

    bool complete;
    NodeReceiveMsgBytes(node, ser_msg_header, complete);
    NodeReceiveMsgBytes(node, ser_msg.data, complete);
    return complete;
}

#ifdef USE_WAKEUP_PIPE
bool ConnmanTestMsg::OpenWakeupPipe()
{
    if (pipe(wakeupPipe) != 0) return false;
    // Non-blocking, so a test asking whether a wakeup happened gets an answer when none did.
    return fcntl(wakeupPipe[0], F_SETFL, fcntl(wakeupPipe[0], F_GETFL, 0) | O_NONBLOCK) == 0;
}

bool ConnmanTestMsg::WakeupPipeSignaled()
{
    char buf;
    return read(wakeupPipe[0], &buf, 1) == 1;
}

void ConnmanTestMsg::CloseWakeupPipe()
{
    for (int& fd : wakeupPipe) {
        if (fd != -1) close(fd);
        fd = -1;
    }
}
#endif
