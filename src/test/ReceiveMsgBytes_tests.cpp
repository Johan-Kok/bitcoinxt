// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for CNode::ReceiveMsgBytes
//


#include "main.h"
#include "net.h"
#include "pow.h"
#include "serialize.h"
#include "util.h"
#include "maxblocksize.h"
#include "thinblockutil.h"

#include "test/dummyconnman.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

static void EndMessage(CDataStream& strm)
{
    // Set the size
    assert(strm.size () >= CMessageHeader::HEADER_SIZE);
    unsigned int nSize = strm.size() - CMessageHeader::HEADER_SIZE;
    WriteLE32((uint8_t*)&strm[CMessageHeader::MESSAGE_SIZE_OFFSET], nSize);
    // Set the checksum
    uint256 hash = Hash(strm.begin() + CMessageHeader::HEADER_SIZE, strm.end());
    memcpy((char*)&strm[CMessageHeader::CHECKSUM_OFFSET], hash.begin(), CMessageHeader::CHECKSUM_SIZE);
}

BOOST_FIXTURE_TEST_SUITE(ReceiveMsgBytes_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(FullMessages)
{
    CNode testNode(42, NODE_NETWORK, 0, INVALID_SOCKET,
                   CAddress(CService("127.0.0.1", 0), NODE_NETWORK), 0);
    testNode.nVersion = 1;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << CMessageHeader(Params().NetworkMagic(), "ping", 0);
    s << (uint64_t)11; // ping nonce
    EndMessage(s);

    // Receive a full 'ping' message
    {
        bool complete;
        BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));
        BOOST_CHECK_EQUAL(testNode.vRecvMsg.size(),1UL);
        CNetMessage& msg = testNode.vRecvMsg.front();
        BOOST_CHECK(msg.complete());
        BOOST_CHECK_EQUAL(msg.hdr.GetCommand(), "ping");
        uint64_t nonce;
        msg.vRecv >> nonce;
        BOOST_CHECK_EQUAL(nonce, (uint64_t)11);
    }


    testNode.vRecvMsg.clear();

    // ...receive it one byte at a time:
    {
        for (size_t i = 0; i < s.size(); i++) {
            bool complete;
            BOOST_CHECK(testNode.ReceiveMsgBytes(&s[i], 1, complete));
        }
        BOOST_CHECK_EQUAL(testNode.vRecvMsg.size(),1UL);
        CNetMessage& msg = testNode.vRecvMsg.front();
        BOOST_CHECK(msg.complete());
        BOOST_CHECK_EQUAL(msg.hdr.GetCommand(), "ping");
        uint64_t nonce;
        msg.vRecv >> nonce;
        BOOST_CHECK_EQUAL(nonce, (uint64_t)11);
   }
}

BOOST_AUTO_TEST_CASE(TooLargeBlock)
{
    // Random real block (000000000000dab0130bbcc991d3d7ae6b81aa6f50a798888dfe62337458dc45)
    // With one tx
    CBlock block;
    CDataStream stream(ParseHex("0100000079cda856b143d9db2c1caff01d1aecc8630d30625d10e8b4b8b0000000000000b50cc069d6a3e33e3ff84a5c41d9d3febe7c770fdcc96b2c3ff60abe184f196367291b4d4c86041b8fa45d630101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff08044c86041b020a02ffffffff0100f2052a01000000434104ecd3229b0571c3be876feaac0442a9f13c5a572742927af1dc623353ecf8c202225f64868137a18cdd85cbbb4c74fbccfd4f49639cf1bdc94a5672bb15ad5d4cac00000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;

    CNode testNode(42, NODE_NETWORK, 0, INVALID_SOCKET,
                   CAddress(CService("127.0.0.1", 0), NODE_NETWORK), 0);
    testNode.nVersion = 1;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << CMessageHeader(Params().NetworkMagic(), "block", 0);
    size_t headerLen = s.size();
    s << block;

    // Test: too large
    size_t nMaxMessageSize = NextBlockRaiseCap(chainActive.Tip()->nMaxBlockSize);
    s.resize(nMaxMessageSize + headerLen + 1);
    EndMessage(s);

    bool complete;
    BOOST_CHECK(!testNode.ReceiveMsgBytes(&s[0], s.size(), complete));

    testNode.vRecvMsg.clear();

    // Test: exactly at max:
    s.resize(nMaxMessageSize + headerLen);
    EndMessage(s);

    BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));
}

BOOST_AUTO_TEST_CASE(TooLargeVerack)
{
    DummyNode testNode;
    testNode.nVersion = 1;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << CMessageHeader(Params().NetworkMagic(), "verack", 0);
    size_t headerLen = s.size();

    EndMessage(s);
    bool complete;
    BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));

    // verack is zero-length, so even one byte bigger is too big:
    s.resize(headerLen+1);
    EndMessage(s);
    BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));
    CNodeStateStats stats;
    GetNodeStateStats(testNode.GetId(), stats);
    BOOST_CHECK(stats.nMisbehavior > 0);
}

BOOST_AUTO_TEST_CASE(TooLargePing)
{
    DummyNode testNode;
    testNode.nVersion = 1;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << CMessageHeader(Params().NetworkMagic(), "ping", 0);
    s << (uint64_t)11; // 8-byte nonce

    EndMessage(s);
    bool complete;
    BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));

    // Add another nonce, sanity check should fail
    s << (uint64_t)11; // 8-byte nonce
    EndMessage(s);
    BOOST_CHECK(testNode.ReceiveMsgBytes(&s[0], s.size(), complete));
    CNodeStateStats stats;
    GetNodeStateStats(testNode.GetId(), stats);
    BOOST_CHECK(stats.nMisbehavior > 0);
}

BOOST_AUTO_TEST_SUITE_END()
