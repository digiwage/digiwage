// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The DIGIWAGE developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"

#include "clientversion.h"
#include "main.h" // Provides GetNodeStateStats, GetWarnings, LOCK(cs_main) etc.
#include "net.h"
#include "netbase.h" // Needed for CSubNet, CNetAddr, Lookup, GetNetworkName, IsLimited, IsReachable etc.
#include "protocol.h" // Provides PROTOCOL_VERSION
#include "sync.h"
#include "timedata.h" // Provides GetTimeOffset, GetTimeMillis
#include "guiinterface.h" // Provides uiInterface
#include "util.h" // Provides ValueFromAmount
#include "utilstrencodings.h"
#include "version.h" // Provides CLIENT_VERSION, strSubVersion

#include <univalue.h>

#include <string> // Include string
#include <vector> // Include vector
#include <list>   // Include list
#include <utility> // Include pair
#include <algorithm> // Include find


UniValue getconnectioncount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getconnectioncount\n"
            "\nReturns the number of connections to other nodes.\n"

            "\nbResult:\n"
            "n          (numeric) The connection count\n"

            "\nExamples:\n" +
            HelpExampleCli("getconnectioncount", "") + HelpExampleRpc("getconnectioncount", ""));

    // No need to lock cs_main here, just cs_vNodes is sufficient for vNodes.size()
    LOCK(cs_vNodes);

    return (int)vNodes.size();
}

UniValue ping(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "ping\n"
            "\nRequests that a ping be sent to all other nodes, to measure ping time.\n"
            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
            "Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.\n"

            "\nExamples:\n" +
            HelpExampleCli("ping", "") + HelpExampleRpc("ping", ""));

    // Request that each node send a ping during next message processing pass
    // Requires LOCK(cs_vNodes)
    LOCK(cs_vNodes);

    for (CNode* pNode : vNodes) {
        pNode->fPingQueued = true;
    }

    return NullUniValue;
}

// Helper function to copy node stats
// Requires LOCK(cs_vNodes) to be held by caller if called directly,
// but typically called from functions that already hold the lock.
static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes); // Lock needed here to safely iterate vNodes
    vstats.reserve(vNodes.size());
    for (CNode* pnode : vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

UniValue getpeerinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getpeerinfo\n"
            "\nReturns data about each connected network node as a json array of objects.\n"

            "\nbResult:\n"
            "[\n"
            "  {\n"
            "    \"id\": n,                   (numeric) Peer index\n"
            "    \"addr\":\"host:port\",      (string) The ip address and port of the peer\n"
            "    \"addrlocal\":\"ip:port\",   (string) local address\n"
            "    \"services\":\"xxxxxxxxxxxxxxxx\",   (string) The services offered\n"
            "    \"lastsend\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last send\n"
            "    \"lastrecv\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last receive\n"
            "    \"bytessent\": n,            (numeric) The total bytes sent\n"
            "    \"bytesrecv\": n,            (numeric) The total bytes received\n"
            "    \"conntime\": ttt,           (numeric) The connection time in seconds since epoch (Jan 1 1970 GMT)\n"
            "    \"timeoffset\": ttt,         (numeric) The time offset in seconds\n"
            "    \"pingtime\": n,             (numeric) ping time (if available)\n" // Clarified availability
            "    \"pingwait\": n,             (numeric) ping wait (if non-zero)\n" // Clarified non-zero
            "    \"version\": v,              (numeric) The peer version, such as 7001\n"
            "    \"subver\": \"/DIGIWAGE Core:x.x.x.x/\",  (string) The string version\n"
            "    \"inbound\": true|false,     (boolean) Inbound (true) or Outbound (false)\n"
            "    \"startingheight\": n,       (numeric) The starting height (block) of the peer\n"
            "    \"banscore\": n,             (numeric) The ban score\n"
            "    \"synced_headers\": n,       (numeric) The last header we have in common with this peer\n"
            "    \"synced_blocks\": n,        (numeric) The last block we have in common with this peer\n"
            "    \"inflight\": [\n"
            "       n,                        (numeric) The heights of blocks we're currently asking from this peer\n"
            "       ...\n"
            "    ],\n"
            "    \"whitelisted\": true|false  (boolean) If the peer is whitelisted\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getpeerinfo", "") + HelpExampleRpc("getpeerinfo", ""));

    // Requires LOCK(cs_main) because GetNodeStateStats might need it.
    // Also requires LOCK(cs_vNodes) for CopyNodeStats. Lock cs_main first.
    LOCK(cs_main);

    std::vector<CNodeStats> vstats;
    CopyNodeStats(vstats); // Locks cs_vNodes internally

    UniValue ret(UniValue::VARR);

    for (const CNodeStats& stats : vstats) {
        UniValue obj(UniValue::VOBJ);
        CNodeStateStats statestats;
        // Call GetNodeStateStats, requires cs_main lock which we hold
        bool fStateStats = GetNodeStateStats(stats.nodeid, statestats);

        obj.pushKV("id", stats.nodeid);
        obj.pushKV("addr", stats.addrName);
        if (!(stats.addrLocal.empty()))
            obj.pushKV("addrlocal", stats.addrLocal);
        obj.pushKV("services", strprintf("%016x", stats.nServices));
        obj.pushKV("lastsend", stats.nLastSend);
        obj.pushKV("lastrecv", stats.nLastRecv);
        obj.pushKV("bytessent", stats.nSendBytes);
        obj.pushKV("bytesrecv", stats.nRecvBytes);
        obj.pushKV("conntime", stats.nTimeConnected);
        obj.pushKV("timeoffset", stats.nTimeOffset);
        // Check for valid ping times before adding
        if (stats.dPingTime > 0)
             obj.pushKV("pingtime", stats.dPingTime);
        if (stats.dPingWait > 0.0)
            obj.pushKV("pingwait", stats.dPingWait);
        obj.pushKV("version", stats.nVersion);
        // Use the sanitized form of subver here, to avoid tricksy remote peers from
        // corrupting or modifiying the JSON output by putting special characters in
        // their ver message.
        obj.pushKV("subver", stats.cleanSubVer);
        obj.pushKV("inbound", stats.fInbound);
        obj.pushKV("startingheight", stats.nStartingHeight);
        if (fStateStats) {
            obj.pushKV("banscore", statestats.nMisbehavior);
            obj.pushKV("synced_headers", statestats.nSyncHeight);
            obj.pushKV("synced_blocks", statestats.nCommonHeight);
            UniValue heights(UniValue::VARR);
            for (int height : statestats.vHeightInFlight) {
                heights.push_back(height);
            }
            obj.pushKV("inflight", heights);
        }
        obj.pushKV("whitelisted", stats.fWhitelisted);

        ret.push_back(obj);
    }

    return ret;
}

UniValue addnode(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 ||
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw std::runtime_error(
            "addnode \"node\" \"add|remove|onetry\"\n"
            "\nAttempts add or remove a node from the addnode list.\n"
            "Or try a connection to a node once.\n"

            "\nArguments:\n"
            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
            "2. \"command\"  (string, required) 'add' to add a node to the list, 'remove' to remove a node from the list, 'onetry' to try a connection to the node once\n"

            "\nExamples:\n" +
            HelpExampleCli("addnode", "\"192.168.0.6:46003\" \"onetry\"") + HelpExampleRpc("addnode", "\"192.168.0.6:46003\", \"onetry\""));

    std::string strNode = params[0].get_str();

    if (strCommand == "onetry") {
        CAddress addr;
        // Assuming OpenNetworkConnection handles potential CAddress lookup internally
        // If addr needs to be populated first via Lookup, add that logic here.
        OpenNetworkConnection(addr, false, nullptr, strNode.c_str(), true);
        return NullUniValue;
    }

    LOCK(cs_vAddedNodes);
    auto it = std::find(vAddedNodes.begin(), vAddedNodes.end(), strNode);

    if (strCommand == "add") {
        if (it != vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
        vAddedNodes.push_back(strNode);
        // Consider saving the added nodes list here if persistence is desired immediately
        // e.g. if a WriteAddedNodes() function exists.
    } else if (strCommand == "remove") {
        if (it == vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        vAddedNodes.erase(it);
        // Consider saving the added nodes list here.
    }

    return NullUniValue;
}

UniValue disconnectnode(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "disconnectnode \"node\" \n"
            "\nImmediately disconnects from the specified node.\n"
            "Note: make sure the node argument is correctly formatted, typically \"address:port\". Use getpeerinfo to list connected nodes.\n"
            "\nArguments:\n"
            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"

            "\nExamples:\n"
            + HelpExampleCli("disconnectnode", "\"192.168.0.6:46003\"")
            + HelpExampleRpc("disconnectnode", "\"192.168.0.6:46003\"")
        );

    // Requires LOCK(cs_vNodes) for FindNode and accessing pNode
    LOCK(cs_vNodes);

    CNode* pNode = FindNode(params[0].get_str());
    if (pNode == NULL)
        throw JSONRPCError(RPC_CLIENT_NODE_NOT_CONNECTED, "Node not found in connected nodes");

    pNode->fDisconnect = true; // Mark for disconnection by the network thread

    return NullUniValue;
}

UniValue getaddednodeinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error(
            "getaddednodeinfo dns ( \"node\" )\n"
            "\nReturns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.\n"

            "\nArguments:\n"
            "1. dns        (boolean, required) If false, only a list of added nodes will be provided, otherwise connected information will also be available.\n"
            "2. \"node\"   (string, optional) If provided, return information about this specific node, otherwise all nodes are returned.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"addednode\" : \"192.168.0.201\",   (string) The node ip address or name\n"
            "    \"connected\" : true|false,          (boolean) If connected\n"
            "    \"addresses\" : [                    (list of objects) Only when connected = true\n"
            "       {\n"
            "         \"address\" : \"192.168.0.201:46003\",  (string) The DIGIWAGE server host and port of the connected peer\n"
            "         \"connected\" : \"outbound\" | \"inbound\" (string) connection type \n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getaddednodeinfo", "true") + HelpExampleCli("getaddednodeinfo", "true \"192.168.0.201\"") + HelpExampleRpc("getaddednodeinfo", "true, \"192.168.0.201\""));

    bool fDns = params[0].get_bool();

    std::list<std::string> laddedNodes;
    // Populate laddedNodes
    { // Scope for cs_vAddedNodes lock
        LOCK(cs_vAddedNodes);
        if (params.size() == 1) {
            laddedNodes.assign(vAddedNodes.begin(), vAddedNodes.end());
        } else {
            std::string strNode = params[1].get_str();
            bool found = false;
            for (const std::string& strAddNode : vAddedNodes) {
                if (strAddNode == strNode) {
                    laddedNodes.push_back(strAddNode);
                    found = true;
                    break;
                }
            }
            if (!found)
                throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        }
    } // End scope for cs_vAddedNodes lock

    UniValue ret(UniValue::VARR);
    if (!fDns) {
        // Just return the list of nodes
        for (const std::string& strAddNode : laddedNodes) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("addednode", strAddNode);
            ret.push_back(obj);
        }
        return ret;
    }

    // Requires LOCK(cs_vNodes) to iterate vNodes
    LOCK(cs_vNodes);

    // Map node addresses to connection status and details
    std::map<std::string, UniValue> mapNodeInfo;
    for (CNode* pnode : vNodes) {
        UniValue node_info(UniValue::VOBJ);
        node_info.pushKV("address", pnode->addr.ToStringIPPort());
        node_info.pushKV("connected", pnode->fInbound ? "inbound" : "outbound");

        // Store info keyed by both potential identifiers
        mapNodeInfo[pnode->addrName] = node_info;
        mapNodeInfo[pnode->addr.ToStringIPPort()] = node_info; // In case added node was IP:Port
    }


    for (const std::string& strAddNode : laddedNodes) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("addednode", strAddNode);

        UniValue addresses(UniValue::VARR);
        bool fConnected = false;

        // Check if the added node string matches a connected node
        auto it = mapNodeInfo.find(strAddNode);
        if (it != mapNodeInfo.end()) {
            addresses.push_back(it->second);
            fConnected = true;
        }
        // If not found directly, potentially try resolving strAddNode if it's a hostname
        // and check resolved IPs against connected nodes. This adds complexity.
        // The current simplified logic only matches exact string or IP:Port.


        obj.pushKV("connected", fConnected);
        obj.pushKV("addresses", addresses); // Will be empty if not connected
        ret.push_back(obj);
    }

    return ret;
}


UniValue getnettotals(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw std::runtime_error(
            "getnettotals\n"
            "\nReturns information about network traffic, including bytes in, bytes out,\n"
            "and current time.\n"

            "\nResult:\n"
            "{\n"
            "  \"totalbytesrecv\": n,   (numeric) Total bytes received\n"
            "  \"totalbytessent\": n,   (numeric) Total bytes sent\n"
            "  \"timemillis\": t        (numeric) Current UNIX epoch time in milliseconds\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getnettotals", "") + HelpExampleRpc("getnettotals", ""));

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("totalbytesrecv", CNode::GetTotalBytesRecv());
    obj.pushKV("totalbytessent", CNode::GetTotalBytesSent());
    obj.pushKV("timemillis", GetTimeMillis());
    return obj;
}

static UniValue GetNetworksInfo()
{
    UniValue networks(UniValue::VARR);
    for (int n = 0; n < NET_MAX; ++n) {
        enum Network network = static_cast<enum Network>(n);
        // Fix: Removed NET_INTERNAL check as it's not standard
        if (network == NET_UNROUTABLE) // Exclude only unroutable
            continue;
        proxyType proxy;
        UniValue obj(UniValue::VOBJ);
        GetProxy(network, proxy);
        obj.pushKV("name", GetNetworkName(network));
        obj.pushKV("limited", IsLimited(network));
        obj.pushKV("reachable", IsReachable(network));
        obj.pushKV("proxy", proxy.IsValid() ? proxy.proxy.ToStringIPPort() : std::string());
        obj.pushKV("proxy_randomize_credentials", proxy.randomize_credentials);
        networks.push_back(obj);
    }
    return networks;
}

UniValue getnetworkinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "getnetworkinfo\n"
            "\nReturns an object containing various state info regarding P2P networking.\n"

            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,                      (numeric) the server version\n"
            "  \"subversion\": \"/DIGIWAGE Core:x.x.x.x/\",     (string) the server subversion string\n"
            "  \"protocolversion\": xxxxx,              (numeric) the protocol version\n"
            "  \"localservices\": \"xxxxxxxxxxxxxxxx\", (string) the services we offer to the network\n"
            "  \"timeoffset\": xxxxx,                   (numeric) the time offset\n"
            "  \"connections\": xxxxx,                  (numeric) the number of connections\n"
            "  \"networks\": [                          (array) information per network\n"
            "  {\n"
            "    \"name\": \"xxx\",                     (string) network (ipv4, ipv6 or onion)\n"
            "    \"limited\": true|false,               (boolean) is the network limited using -onlynet?\n"
            "    \"reachable\": true|false,             (boolean) is the network reachable?\n"
            "    \"proxy\": \"host:port\",              (string) the proxy that is used for this network, or empty if none\n"
            "    \"proxy_randomize_credentials\": true|false (boolean) Whether randomized credentials are used\n"
            "  }\n"
            "  ,...\n"
            "  ],\n"
            "  \"relayfee\": x.xxxxxxxx,                (numeric) minimum relay fee for non-free transactions in WAGE/kB\n"
            "  \"localaddresses\": [                    (array) list of local addresses\n"
            "  {\n"
            "    \"address\": \"xxxx\",                 (string) network address\n"
            "    \"port\": xxx,                         (numeric) network port\n"
            "    \"score\": xxx                         (numeric) relative score\n"
            "  }\n"
            "  ,...\n"
            "  ],\n"
            "  \"warnings\": \"...\"                    (string) any network warnings (such as alert messages)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getnetworkinfo", "") + HelpExampleRpc("getnetworkinfo", ""));

    // Requires LOCK(cs_main) for GetTimeOffset(), vNodes.size(), GetWarnings()
    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    obj.pushKV("subversion",    strSubVersion);
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
    obj.pushKV("localservices", strprintf("%016x", nLocalServices));
    obj.pushKV("timeoffset", GetTimeOffset());
    obj.pushKV("connections", (int)vNodes.size()); // Requires cs_vNodes, but cs_main is higher level
    obj.pushKV("networks", GetNetworksInfo());
    // Assumes ::minRelayTxFee is accessible and defined
    if (::minRelayTxFee.GetFeePerK() > 0) { // Check if relay fee is set
         obj.pushKV("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));
    } else {
         obj.pushKV("relayfee", 0); // Or indicate it's not set
    }

    UniValue localAddresses(UniValue::VARR);
    {
        LOCK(cs_mapLocalHost);
        for (const std::pair<const CNetAddr, LocalServiceInfo>& item : mapLocalHost) {
            UniValue rec(UniValue::VOBJ);
            rec.pushKV("address", item.first.ToString());
            rec.pushKV("port", item.second.nPort);
            rec.pushKV("score", item.second.nScore);
            localAddresses.push_back(rec);
        }
    }
    obj.pushKV("localaddresses", localAddresses);
    obj.pushKV("warnings", GetWarnings("statusbar"));

    return obj;
}

UniValue setban(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() < 2 || params.size() > 4 ||
        (strCommand != "add" && strCommand != "remove"))
        throw std::runtime_error(
            "setban \"subnet\" \"add|remove\" (bantime) (absolute)\n"
            "\nAttempts add or remove a IP/Subnet from the banned list.\n"

            "\nArguments:\n"
            "1. \"subnet\"       (string, required) The IP/Subnet (see getpeerinfo for nodes ip) with an optional netmask (default is /32 = single ip)\n"
            "2. \"command\"      (string, required) 'add' to add a IP/Subnet to the list, 'remove' to remove a IP/Subnet from the list\n"
            "3. \"bantime\"      (numeric, optional) time in seconds how long (or until when if [absolute] is set) the ip is banned (0 or empty means using the default time of 24h which can also be overwritten by the -bantime startup argument)\n"
            "4. \"absolute\"     (boolean, optional, default=false) If set, the bantime must be an absolute timestamp in seconds since epoch (Jan 1 1970 GMT)\n"

            "\nExamples:\n"
            + HelpExampleCli("setban", "\"192.168.0.6\" \"add\" 86400")
            + HelpExampleCli("setban", "\"192.168.0.0/24\" \"add\"")
            + HelpExampleRpc("setban", "\"192.168.0.6\", \"add\", 86400"));

    CSubNet subNet;
    CNetAddr netAddr; // Used only temporarily if input is a single IP
    bool isSubnet = false; // Not strictly needed anymore, we convert all to subnet

    // Parse the IP/Subnet string
    std::string strSubnet = params[0].get_str();
    if (strSubnet.find('/') != std::string::npos) {
        isSubnet = true; // Keep track for potential error messages if needed
        subNet = CSubNet(strSubnet);
        if (!subNet.IsValid())
            throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET, "Error: Invalid IP/Subnet");
    } else {
        // Treat as single IP
        if (!netAddr.SetSpecial(strSubnet)) {
            throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET, "Error: Invalid IP Address");
        }
        // Convert single IP to /32 subnet for consistent handling
        subNet = CSubNet(netAddr);
    }


    if (strCommand == "add")
    {
        // Use subnet for check
        if (CNode::IsBanned(subNet))
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: IP/Subnet already banned");

        int64_t banTime = 0; // Default bantime is set by CNode::Ban
        if (params.size() >= 3 && !params[2].isNull()) {
             // Fix: Use getInt<int64_t>()
             banTime = params[2].getInt<int64_t>();
             if (banTime < 0) // Ban time cannot be negative
                  throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Ban time must be non-negative");
        }

        bool absolute = false;
        if (params.size() == 4 && params[3].isBool()) // Check type before getting
            absolute = params[3].get_bool();

        // Use subnet for banning
        CNode::Ban(subNet, BanReasonManuallyAdded, banTime, absolute);

        // Disconnect nodes matching the subnet
        // Requires LOCK(cs_vNodes)
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes) {
            if (subNet.Match(pnode->addr)) {
                LogPrintf("Disconnecting banned node %s\n", pnode->addrName.c_str());
                pnode->fDisconnect = true; // Mark for disconnection
            }
        }
    }
    else if(strCommand == "remove")
    {
        // Use subnet for unbanning
        if (!CNode::Unban(subNet))
            throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET, "Error: Unban failed. Subnet not found in ban list.");
    }

    // Ensure ban list changes are saved and UI is notified
    DumpBanlist();
    // Fix: Call BannedListChanged directly
    uiInterface.BannedListChanged();

    return NullUniValue;
}

UniValue listbanned(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "listbanned\n"
            "\nList all banned IPs/Subnets.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"address\": \"xxx\",          (string) The IP/Subnet address of the banned node\n"
            "    \"banned_until\": ttt,         (numeric) The timestamp (seconds since Unix epoch) when the ban expires\n"
            "    \"ban_created\": ttt,          (numeric) The timestamp (seconds since Unix epoch) when the ban was created\n"
            "    \"ban_reason\": \"xxx\"        (string) The reason for the ban (e.g., 'manually added', 'node misbehaving')\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listbanned", "")
            + HelpExampleRpc("listbanned", ""));

    banmap_t banMap;
    CNode::GetBanned(banMap); // GetBanned locks cs_setBanned internally

    UniValue bannedAddresses(UniValue::VARR);
    for (const auto& banEntryPair : banMap)
    {
        const CBanEntry& banEntry = banEntryPair.second;
        UniValue rec(UniValue::VOBJ);
        rec.pushKV("address", banEntryPair.first.ToString());
        rec.pushKV("banned_until", banEntry.nBanUntil);
        rec.pushKV("ban_created", banEntry.nCreateTime);
        // Call the const version of banReasonToString (fix was made in net.h)
        rec.pushKV("ban_reason", banEntry.banReasonToString());

        bannedAddresses.push_back(rec);
    }

    return bannedAddresses;
}

UniValue clearbanned(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "clearbanned\n"
            "\nClear all banned IPs.\n"

            "\nExamples:\n"
            + HelpExampleCli("clearbanned", "")
            + HelpExampleRpc("clearbanned", ""));

    CNode::ClearBanned(); // ClearBanned locks cs_setBanned internally
    DumpBanlist(); // Store banlist to disk
    // Fix: Call BannedListChanged directly
    uiInterface.BannedListChanged();

    return NullUniValue;
}

// JSON-RPC Command Table (likely defined elsewhere, but shown for context)
// static const CRPCCommand commands[] =
// { // RPC Commands Table
// // ... other commands ...
//     { "net",          "getconnectioncount", &getconnectioncount, true,  {} },
//     { "net",          "ping",               &ping,               true,  {} },
//     { "net",          "getpeerinfo",        &getpeerinfo,        true,  {} },
//     { "net",          "addnode",            &addnode,            true,  {"node", "command"} },
//     { "net",          "disconnectnode",     &disconnectnode,     true,  {"node"} },
//     { "net",          "getaddednodeinfo",   &getaddednodeinfo,   true,  {"dns", "node"} },
//     { "net",          "getnettotals",       &getnettotals,       true,  {} },
//     { "net",          "getnetworkinfo",     &getnetworkinfo,     true,  {} },
//     { "net",          "setban",             &setban,             true,  {"subnet", "command", "bantime", "absolute"} },
//     { "net",          "listbanned",         &listbanned,         true,  {} },
//     { "net",          "clearbanned",        &clearbanned,        true,  {} },
// // ... other commands ...
// };

// void RegisterNetRPCCommands(CRPCTable &tableRPC)
// {
//     for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
//         tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
// }