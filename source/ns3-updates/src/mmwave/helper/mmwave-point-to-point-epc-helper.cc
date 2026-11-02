#include <ns3/simulator.h>
/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 *   Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *   Copyright (c) 2015, NYU WIRELESS, Tandon School of Engineering, New York University
 *   Copyright (c) 2016, University of Padova, Dep. of Information Engineering, SIGNET lab.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *   Author: Marco Miozzo <marco.miozzo@cttc.es>
 *           Nicola Baldo  <nbaldo@cttc.es>
 *
 *   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu>
 *                         Sourjya Dutta <sdutta@nyu.edu>
 *                         Russell Ford <russell.ford@nyu.edu>
 *                         Menglei Zhang <menglei@nyu.edu>
 *
 *   Modified by: Michele Polese <michele.polese@gmail.com>
 *                Dual Connectivity and Handover functionalities
 *   Modified by: Gabriel Arrobo <gab.arrobo@gmail.com>
 *                Added IPv6 support in ActivateEpsBearer function
 */

#include <cstdlib>
#include "ns3/ipv6-static-routing-helper.h"
#include "ns3/ipv6-static-routing.h"
#include <ns3/config.h>
#include <ns3/epc-enb-application.h>
#include <ns3/epc-mme-application.h>
#include <ns3/epc-s1ap.h>
#include <ns3/epc-sgw-pgw-application.h>
#include <ns3/epc-ue-nas.h>
#include <ns3/epc-x2.h>
#include <ns3/eps-bearer.h>
#include <ns3/icmpv6-l4-protocol.h>
#include <ns3/inet-socket-address.h>
#include <ns3/internet-stack-helper.h>
#include <ns3/ipv4-address.h>
#include <ns3/ipv4-header.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/log.h>
#include <ns3/lte-enb-net-device.h>
#include <ns3/lte-enb-rrc.h>
#include <ns3/mac48-address.h>
#include <ns3/mmwave-enb-net-device.h>
#include <ns3/mmwave-point-to-point-epc-helper.h>
#include "open5gs-n2-client.h"
#include <vector>
#include <ns3/packet.h>
#include <ns3/mmwave-ue-net-device.h>
#include <ns3/packet-socket-address.h>
#include <ns3/packet-socket-helper.h>
#include <ns3/point-to-point-helper.h>
#include <ns3/queue-size.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MmWavePointToPointEpcHelper");

namespace mmwave
{

NS_OBJECT_ENSURE_REGISTERED(MmWavePointToPointEpcHelper);

MmWavePointToPointEpcHelper::MmWavePointToPointEpcHelper()
    : m_gtpuUdpPort(2152),
      // fixed by the standard
      m_s1apUdpPort(36412)
{
    NS_LOG_FUNCTION(this);

    m_open5gsN2Client = std::make_unique<Open5gsN2Client>();

    // since we use point-to-point links for all S1-U and S1-AP links,
    // we use a /30 subnet which can hold exactly two addresses
    // (remember that net broadcast and null address are not valid)
    m_s1uIpv4AddressHelper.SetBase("10.0.0.0", "255.255.255.252");
    m_s1apIpv4AddressHelper.SetBase("11.0.0.0", "255.255.255.252");
    m_x2Ipv4AddressHelper.SetBase("12.0.0.0", "255.255.255.252");

    // we use a /8 net for all UEs
    m_uePgwAddressHelper.SetBase("10.45.1.0", "255.255.255.0", "0.0.0.2");

    // we use a /64 IPv6 net all UEs
    m_uePgwAddressHelper6.SetBase("7777:f00d::", Ipv6Prefix(64));

    NS_LOG_UNCOND(
        "[EPC PHYSICAL REMOVE] Simulated MME/SGW/PGW/TUN objects not created");
}

MmWavePointToPointEpcHelper::~MmWavePointToPointEpcHelper()
{
    NS_LOG_FUNCTION(this);
}

TypeId
MmWavePointToPointEpcHelper::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::MmWavePointToPointEpcHelper")
            .SetParent<EpcHelper>()
            .SetGroupName("mmWave")
            .AddConstructor<MmWavePointToPointEpcHelper>()
            .AddAttribute("S1uLinkDataRate",
                          "The data rate to be used for the next S1-U link to be created",
                          DataRateValue(DataRate("10Gb/s")),
                          MakeDataRateAccessor(&MmWavePointToPointEpcHelper::m_s1uLinkDataRate),
                          MakeDataRateChecker())
            .AddAttribute("S1uLinkDelay",
                          "The delay to be used for the next S1-U link to be created",
                          TimeValue(Seconds(0.001)),
                          MakeTimeAccessor(&MmWavePointToPointEpcHelper::m_s1uLinkDelay),
                          MakeTimeChecker())
            .AddAttribute(
                "S1uLinkMtu",
                "The MTU of the next S1-U link to be created. Note that, because of the "
                "additional GTP/UDP/IP tunneling overhead, you need a MTU larger than the "
                "end-to-end MTU that you want to support.",
                UintegerValue(2000),
                MakeUintegerAccessor(&MmWavePointToPointEpcHelper::m_s1uLinkMtu),
                MakeUintegerChecker<uint16_t>())
            .AddAttribute("S1apLinkDataRate",
                          "The data rate to be used for the S1-AP link to be created",
                          DataRateValue(DataRate("10Gb/s")),
                          MakeDataRateAccessor(&MmWavePointToPointEpcHelper::m_s1apLinkDataRate),
                          MakeDataRateChecker())
            .AddAttribute("S1apLinkDelay",
                          "The delay to be used for the S1-AP link to be created",
                          TimeValue(MilliSeconds(15)),
                          MakeTimeAccessor(&MmWavePointToPointEpcHelper::m_s1apLinkDelay),
                          MakeTimeChecker())
            .AddAttribute("S1apLinkMtu",
                          "The MTU of the next S1-AP link to be created",
                          UintegerValue(10000),
                          MakeUintegerAccessor(&MmWavePointToPointEpcHelper::m_s1apLinkMtu),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("X2LinkDataRate",
                          "The data rate to be used for the next X2 link to be created",
                          DataRateValue(DataRate("100Gb/s")),
                          MakeDataRateAccessor(&MmWavePointToPointEpcHelper::m_x2LinkDataRate),
                          MakeDataRateChecker())
            .AddAttribute("X2LinkDelay",
                          "The delay to be used for the next X2 link to be created",
                          TimeValue(Seconds(0.001)),
                          MakeTimeAccessor(&MmWavePointToPointEpcHelper::m_x2LinkDelay),
                          MakeTimeChecker())
            .AddAttribute("X2LinkMtu",
                          "The MTU of the next X2 link to be created. Note that, because of some "
                          "big X2 messages, you need a big MTU.",
                          UintegerValue(10000),
                          MakeUintegerAccessor(&MmWavePointToPointEpcHelper::m_x2LinkMtu),
                          MakeUintegerChecker<uint16_t>());
    return tid;
}


void
MmWavePointToPointEpcHelper::DoDispose()
{
    NS_LOG_FUNCTION(this);

    m_open5gsEnbApp = nullptr;

    if (m_open5gsN2Client)
    {
        m_open5gsN2Client->Close();
    }

    Object::DoDispose();
}


bool
MmWavePointToPointEpcHelper::InjectOpen5gsDownlink(
    const std::vector<uint8_t>& downlink,
    uint8_t pduSessionId)
{
        if (downlink.size() < 20 ||
            (downlink[0] >> 4) != 4)
        {
            return false;
        }

        const uint32_t destinationUeIpv4 =
            (static_cast<uint32_t>(downlink[16]) << 24) |
            (static_cast<uint32_t>(downlink[17]) << 16) |
            (static_cast<uint32_t>(downlink[18]) << 8) |
            static_cast<uint32_t>(downlink[19]);

        std::pair<uint16_t, uint8_t> radioRoute{};
        bool routeFound = false;

        if (pduSessionId != 0)
        {
            auto exactRoute =
                m_uePsiRadioMap.find(
                    std::make_pair(
                        destinationUeIpv4,
                        pduSessionId));

            if (exactRoute != m_uePsiRadioMap.end())
            {
                radioRoute = exactRoute->second;
                routeFound = true;
            }
        }

        // Compatibility fallback for the legacy/single-session path.
        if (!routeFound)
        {
            auto legacyRoute =
                m_ueIpv4RadioMap.find(destinationUeIpv4);

            if (legacyRoute != m_ueIpv4RadioMap.end())
            {
                radioRoute = legacyRoute->second;
                routeFound = true;
            }
        }

        if (!routeFound)
        {
            NS_LOG_UNCOND(
                "[N3 DIRECT DL] No radio route for destination UE="
                << static_cast<uint32_t>(downlink[16]) << "."
                << static_cast<uint32_t>(downlink[17]) << "."
                << static_cast<uint32_t>(downlink[18]) << "."
                << static_cast<uint32_t>(downlink[19])
                << " PSI="
                << static_cast<uint32_t>(pduSessionId));

            return false;
        }

        Ptr<Packet> dlPacket =
            Create<Packet>(
                downlink.data(),
                static_cast<uint32_t>(downlink.size()));

        m_open5gsEnbApp->InjectFromN3(
            dlPacket,
            radioRoute.first,
            radioRoute.second);

        static uint64_t deliveredDlCount = 0;
        ++deliveredDlCount;

        if (deliveredDlCount <= 5 ||
            deliveredDlCount % 1000 == 0)
        {
            NS_LOG_UNCOND(
                "[N3 DIRECT DL] Open5GS packet delivered directly"
                << " count=" << deliveredDlCount
                << " PSI="
                << static_cast<uint32_t>(pduSessionId)
                << " RNTI=" << radioRoute.first
                << " BID="
                << static_cast<uint32_t>(
                       radioRoute.second)
                << " bytes=" << downlink.size());
        }

        return true;
}


void
MmWavePointToPointEpcHelper::PollOpen5gsDownlink()
{
    if (m_open5gsN2Connected &&
        m_open5gsN2Client &&
        m_open5gsEnbApp)
    {
        for (uint32_t i = 0; i < 64; ++i)
        {
            std::vector<uint8_t> packet;
            uint8_t psi = 0;

            if (!m_open5gsN2Client->TryReceiveDownlinkIpv4(
                    packet, psi))
            {
                break;
            }

            InjectOpen5gsDownlink(packet, psi);
        }
    }

    Simulator::Schedule(
        MilliSeconds(10),
        &MmWavePointToPointEpcHelper::PollOpen5gsDownlink,
        this);
}

void
MmWavePointToPointEpcHelper::ForwardEnbPacketToOpen5gs(
    Ptr<Packet> packet,
    uint16_t rnti,
    uint8_t bid)
{
    if (!m_open5gsN2Connected ||
        !m_open5gsN2Client ||
        !m_open5gsEnbApp ||
        !packet)
    {
        return;
    }

    const uint32_t packetSize = packet->GetSize();
    if (packetSize < 20)
    {
        return;
    }

    std::vector<uint8_t> bytes(packetSize);
    packet->CopyData(bytes.data(), packetSize);

    if ((bytes[0] >> 4) != 4)
    {
        return;
    }

    const uint32_t sourceUeIpv4 =
        (static_cast<uint32_t>(bytes[12]) << 24) |
        (static_cast<uint32_t>(bytes[13]) << 16) |
        (static_cast<uint32_t>(bytes[14]) << 8) |
        static_cast<uint32_t>(bytes[15]);

    uint8_t selectedPduSessionId = 0;

    if (!m_open5gsN2Client->SendUplinkIpv4(
            bytes.data(),
            bytes.size(),
            &selectedPduSessionId))
    {
        return;
    }

    // Keep the legacy UE-IP route as a compatibility fallback.
    m_ueIpv4RadioMap[sourceUeIpv4] =
        std::make_pair(rnti, bid);

    // Exact Step-6 route for multiple PDU sessions belonging to
    // the same UE IPv4 address.
    m_uePsiRadioMap[
        std::make_pair(
            sourceUeIpv4,
            selectedPduSessionId)] =
        std::make_pair(rnti, bid);

    NS_LOG_UNCOND(
        "[N3 ROUTE] UE/PSI radio route learned"
        << " UE="
        << static_cast<uint32_t>(bytes[12]) << "."
        << static_cast<uint32_t>(bytes[13]) << "."
        << static_cast<uint32_t>(bytes[14]) << "."
        << static_cast<uint32_t>(bytes[15])
        << " PSI="
        << static_cast<uint32_t>(selectedPduSessionId)
        << " RNTI=" << rnti
        << " BID=" << static_cast<uint32_t>(bid));

    const bool isIcmp = (bytes[9] == 1);


    /*
     * Keep the original blocking receive for the explicit ICMP
     * round-trip validation path.
     */
    if (isIcmp)
    {
        std::vector<uint8_t> downlink;

        if (!m_open5gsN2Client->ReceiveDownlinkIpv4(downlink) ||
            !InjectOpen5gsDownlink(downlink, 0))
        {
            NS_LOG_UNCOND(
                "[N3 DIRECT DL] No valid Open5GS downlink packet");
        }

        return;
    }

    /*
     * Runtime traffic path:
     * drain already queued N3 GTP-U packets without blocking ns-3.
     *
     * Continuous UL packets repeatedly enter this callback, so replies
     * generated by previous UL packets are collected on subsequent calls.
     */
    constexpr uint32_t maxDlDrainPerCall = 64;

    for (uint32_t i = 0; i < maxDlDrainPerCall; ++i)
    {
        std::vector<uint8_t> downlink;
        uint8_t downlinkPduSessionId = 0;

        if (!m_open5gsN2Client->TryReceiveDownlinkIpv4(
                downlink,
                downlinkPduSessionId))
        {
            break;
        }

        InjectOpen5gsDownlink(
            downlink,
            downlinkPduSessionId);
    }
}

void
MmWavePointToPointEpcHelper::AddEnb(Ptr<Node> enb, Ptr<NetDevice> lteEnbNetDevice, uint16_t cellId)
{
    NS_LOG_FUNCTION(this << enb << lteEnbNetDevice << cellId);

    std::string n2DeviceType = "UNKNOWN";
    if (DynamicCast<MmWaveEnbNetDevice>(lteEnbNetDevice))
    {
        n2DeviceType = "mmWave";
    }
    else if (DynamicCast<LteEnbNetDevice>(lteEnbNetDevice))
    {
        n2DeviceType = "LTE";
    }

    NS_LOG_UNCOND("[N2 DEBUG] AddEnb cellId=" << cellId
                  << " type=" << n2DeviceType);

    // Experimental N2 path toward external Open5GS AMF.
    // Keep the existing S1/EPC path untouched during initial integration.

    if (!m_open5gsN2Connected && n2DeviceType == "mmWave")
    {
        const char* amfIpEnv = std::getenv("ORAN_AMF_IP");
        const std::string amfIp =
            (amfIpEnv != nullptr && amfIpEnv[0] != '\0')
                ? amfIpEnv
                : "192.168.0.18";

        NS_LOG_UNCOND("[N2] AMF target=" << amfIp << ":38412");

        m_open5gsN2Connected =
            m_open5gsN2Client->Connect(amfIp, 38412);
        if (m_open5gsN2Connected)
        {
            NS_LOG_UNCOND("[N2] ns-O-RAN successfully connected to Open5GS AMF");

            if (m_open5gsN2Client->SendNgSetupRequest(cellId))
            {
                NS_LOG_UNCOND("[N2] NG Setup completed successfully");
                NS_LOG_UNCOND("[N2 MULTI-UE] Waiting for UE IPv4 assignment before registration");
            }
            else
            {
                NS_LOG_UNCOND("[N2] NG Setup failed");
            }
        }
        else
        {
            NS_LOG_UNCOND("[N2] Failed to connect to Open5GS AMF");
        }
    }

    NS_ASSERT(enb == lteEnbNetDevice->GetNode());

    // add an IPv4 stack to the previously created eNB
    InternetStackHelper internet;
    internet.Install(enb);
    NS_LOG_LOGIC("number of Ipv4 ifaces of the eNB after node creation: "
                 << enb->GetObject<Ipv4>()->GetNInterfaces());

    // No simulated S1-U or S1-AP transport is created.
    Ptr<Socket> enbS1uSocket = nullptr;
    Ipv4Address enbAddress = Ipv4Address::GetZero();
    Ipv4Address sgwAddress = Ipv4Address::GetZero();
    int retval = 0;

    NS_LOG_UNCOND(
        "[S1-U REMOVE] Simulated S1-U link/socket not created"
        << " cellId=" << cellId);

    NS_LOG_UNCOND(
        "[S1-AP REMOVE] Simulated eNB-MME link/socket not created"
        << " cellId=" << cellId);

    // give PacketSocket powers to the eNB
    // PacketSocketHelper packetSocket;
    // packetSocket.Install (enb);

    // create LTE socket for the ENB
    Ptr<Socket> enbLteSocket =
        Socket::CreateSocket(enb, TypeId::LookupByName("ns3::PacketSocketFactory"));
    PacketSocketAddress enbLteSocketBindAddress;
    enbLteSocketBindAddress.SetSingleDevice(lteEnbNetDevice->GetIfIndex());
    enbLteSocketBindAddress.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
    retval = enbLteSocket->Bind(enbLteSocketBindAddress);
    NS_ASSERT(retval == 0);
    PacketSocketAddress enbLteSocketConnectAddress;
    enbLteSocketConnectAddress.SetPhysicalAddress(Mac48Address::GetBroadcast());
    enbLteSocketConnectAddress.SetSingleDevice(lteEnbNetDevice->GetIfIndex());
    enbLteSocketConnectAddress.SetProtocol(Ipv4L3Protocol::PROT_NUMBER);
    retval = enbLteSocket->Connect(enbLteSocketConnectAddress);
    NS_ASSERT(retval == 0);

    // create LTE socket for the ENB
    Ptr<Socket> enbLteSocket6 =
        Socket::CreateSocket(enb, TypeId::LookupByName("ns3::PacketSocketFactory"));
    PacketSocketAddress enbLteSocketBindAddress6;
    enbLteSocketBindAddress6.SetSingleDevice(lteEnbNetDevice->GetIfIndex());
    enbLteSocketBindAddress6.SetProtocol(Ipv6L3Protocol::PROT_NUMBER);
    retval = enbLteSocket6->Bind(enbLteSocketBindAddress6);
    NS_ASSERT(retval == 0);
    PacketSocketAddress enbLteSocketConnectAddress6;
    enbLteSocketConnectAddress6.SetPhysicalAddress(Mac48Address::GetBroadcast());
    enbLteSocketConnectAddress6.SetSingleDevice(lteEnbNetDevice->GetIfIndex());
    enbLteSocketConnectAddress6.SetProtocol(Ipv6L3Protocol::PROT_NUMBER);
    retval = enbLteSocket6->Connect(enbLteSocketConnectAddress6);
    NS_ASSERT(retval == 0);

    NS_LOG_INFO("create EpcEnbApplication");
    Ptr<EpcEnbApplication> enbApp = CreateObject<EpcEnbApplication>(enbLteSocket,
                                                                    enbLteSocket6,
                                                                    enbS1uSocket,
                                                                    enbAddress,
                                                                    sgwAddress,
                                                                    cellId);
    enb->AddApplication(enbApp);

    if (n2DeviceType == "LTE")
    {
        m_open5gsEnbApp = enbApp;

        Simulator::Schedule(
            MilliSeconds(1),
            &MmWavePointToPointEpcHelper::PollOpen5gsDownlink,
            this);

        NS_LOG_UNCOND("[N3 POLL] Independent downlink polling enabled");

        enbApp->SetN3UplinkCallback(
            MakeCallback(
                &MmWavePointToPointEpcHelper::ForwardEnbPacketToOpen5gs,
                this));

        NS_LOG_UNCOND(
            "[N3 DIRECT] LTE anchor direct-N3 user-plane hook enabled"
            << " cellId=" << cellId);
    }

    NS_ASSERT(enb->GetNApplications() == 1);
    NS_ASSERT_MSG(enb->GetApplication(0)->GetObject<EpcEnbApplication>(),
                  "cannot retrieve EpcEnbApplication");
    NS_LOG_LOGIC("enb: " << enb << ", enb->GetApplication (0): " << enb->GetApplication(0));

    NS_LOG_INFO("Create EpcX2 entity");
    Ptr<EpcX2> x2 = CreateObject<EpcX2>();
    
enb->AggregateObject(x2);

    NS_LOG_UNCOND(
        "[S1-AP REMOVE] Simulated S1-AP endpoint not created"
        << " cellId=" << cellId);

}

void
MmWavePointToPointEpcHelper::AddX2Interface(Ptr<Node> enb1, Ptr<Node> enb2)
{
    NS_LOG_FUNCTION(this << enb1 << enb2);

    // Create a point to point link between the two eNBs with
    // the corresponding new NetDevices on each side
    NodeContainer enbNodes;
    enbNodes.Add(enb1);
    enbNodes.Add(enb2);
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(m_x2LinkDataRate));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(m_x2LinkMtu));
    p2ph.SetChannelAttribute("Delay", TimeValue(m_x2LinkDelay));
    // in case the RLC buffer is full, the forwarding of packets from the source eNB to the target
    // eNB during a handover or a switch may overflow the transmission queue of point to point
    // devices. Therefore the following line increases the size of tx queue in p2p devices. The
    // parameter should be related to the maximum size of the RLC buffer.
    p2ph.SetQueue("ns3::DropTailQueue", "MaxSize", QueueSizeValue(QueueSize("4294967295p")));
    NetDeviceContainer enbDevices = p2ph.Install(enb1, enb2);
    NS_LOG_LOGIC("number of Ipv4 ifaces of the eNB #1 after installing p2p dev: "
                 << enb1->GetObject<Ipv4>()->GetNInterfaces());
    NS_LOG_LOGIC("number of Ipv4 ifaces of the eNB #2 after installing p2p dev: "
                 << enb2->GetObject<Ipv4>()->GetNInterfaces());
    Ptr<NetDevice> enb1Dev = enbDevices.Get(0);
    Ptr<NetDevice> enb2Dev = enbDevices.Get(1);

    m_x2Ipv4AddressHelper.NewNetwork();
    Ipv4InterfaceContainer enbIpIfaces = m_x2Ipv4AddressHelper.Assign(enbDevices);
    NS_LOG_LOGIC("number of Ipv4 ifaces of the eNB #1 after assigning Ipv4 addr to X2 dev: "
                 << enb1->GetObject<Ipv4>()->GetNInterfaces());
    NS_LOG_LOGIC("number of Ipv4 ifaces of the eNB #2 after assigning Ipv4 addr to X2 dev: "
                 << enb2->GetObject<Ipv4>()->GetNInterfaces());

    Ipv4Address enb1X2Address = enbIpIfaces.GetAddress(0);
    Ipv4Address enb2X2Address = enbIpIfaces.GetAddress(1);

    // Add X2 interface to both eNBs' X2 entities
    Ptr<EpcX2> enb1X2 = enb1->GetObject<EpcX2>();
    Ptr<MmWaveEnbNetDevice> enb1MmWaveDev = enb1->GetDevice(0)->GetObject<MmWaveEnbNetDevice>();
    Ptr<LteEnbNetDevice> enb1LteDev = enb1->GetDevice(0)->GetObject<LteEnbNetDevice>();
    // we may have a LTE or a MmWave eNB
    uint16_t enb1CellId = 0;
    if (enb1MmWaveDev)
    {
        enb1CellId = enb1MmWaveDev->GetCellId();
        NS_LOG_INFO("MmWaveEnbNetDevice #1 = " << enb1MmWaveDev << " - CellId = " << enb1CellId);
    }
    else if (enb1LteDev)
    {
        enb1CellId = enb1LteDev->GetCellId();
        NS_LOG_INFO("LteEnbNetDevice #1 = " << enb1LteDev << " - CellId = " << enb1CellId);
    }

    Ptr<EpcX2> enb2X2 = enb2->GetObject<EpcX2>();
    Ptr<MmWaveEnbNetDevice> enb2MmWaveDev = enb2->GetDevice(0)->GetObject<MmWaveEnbNetDevice>();
    Ptr<LteEnbNetDevice> enb2LteDev = enb2->GetDevice(0)->GetObject<LteEnbNetDevice>();
    // we may have a LTE or a MmWave eNB
    uint16_t enb2CellId = 0;
    if (enb2MmWaveDev)
    {
        enb2CellId = enb2MmWaveDev->GetCellId();
        NS_LOG_INFO("MmWaveEnbNetDevice #2 = " << enb2MmWaveDev << " - CellId = " << enb2CellId);
        enb2MmWaveDev->GetRrc()->AddX2Neighbour(enb1CellId);
    }
    else if (enb2LteDev)
    {
        enb2CellId = enb2LteDev->GetCellId();
        NS_LOG_INFO("LteEnbNetDevice #2 = " << enb2LteDev << " - CellId = " << enb2CellId);
        enb2LteDev->GetRrc()->AddX2Neighbour(enb1CellId);
    }

    enb1X2->AddX2Interface(enb1CellId, enb1X2Address, enb2CellId, enb2X2Address);
    enb2X2->AddX2Interface(enb2CellId, enb2X2Address, enb1CellId, enb1X2Address);

    if (enb1MmWaveDev)
    {
        enb1MmWaveDev->GetRrc()->AddX2Neighbour(enb2CellId);
    }
    else if (enb1LteDev)
    {
        enb1LteDev->GetRrc()->AddX2Neighbour(enb2CellId);
    }

    // if((enb1CellId == 1 && enb2CellId == 2) || (enb1CellId == 2 && enb2CellId == 1))
    // {
    //   //AsciiTraceHelper ascii;
    //   p2ph.EnablePcapAll("x2.pcap");
    // }
}


bool
MmWavePointToPointEpcHelper::RegisterOpen5gsUe(
    const std::string& supi,
    uint32_t ranUeNgapId,
    const std::string& ueIpv4,
    uint32_t gnbN3Teid,
    uint8_t sst,
    uint32_t sd)
{
    const std::vector<Open5gsPduSessionConfig> noAdditionalSessions;

    return RegisterOpen5gsUe(
        supi,
        ranUeNgapId,
        ueIpv4,
        gnbN3Teid,
        sst,
        sd,
        noAdditionalSessions);
}


bool
MmWavePointToPointEpcHelper::RegisterOpen5gsUe(
    const std::string& supi,
    uint32_t ranUeNgapId,
    const std::string& ueIpv4,
    uint32_t gnbN3Teid,
    uint8_t sst,
    uint32_t sd,
    const std::vector<Open5gsPduSessionConfig>& additionalSessions)
{
    if (!m_open5gsN2Connected)
    {
        NS_LOG_UNCOND(
            "[N2 MULTI-UE] Cannot register UE: N2 is not connected");
        return false;
    }

    NS_LOG_UNCOND(
        "[N2 MULTI-UE] Registering SUPI=" << supi
        << " RAN-UE-NGAP-ID=" << ranUeNgapId
        << " UE-IP=" << ueIpv4
        << " gNB-N3-TEID=" << gnbN3Teid
        << " additional-sessions=" << additionalSessions.size());

    if (!m_open5gsN2Client->SendInitialUeMessage(
            supi,
            ranUeNgapId,
            ueIpv4,
            gnbN3Teid,
            sst,
            sd))
    {
        NS_LOG_UNCOND(
            "[N2 MULTI-UE] InitialUEMessage failed for RAN-ID="
            << ranUeNgapId);
        return false;
    }

    // Important: add the complete PDU-session plan BEFORE the registration
    // exchange. ReceiveNgapMessage() constructs Requested NSSAI later.
    for (const auto& session : additionalSessions)
    {
        if (!m_open5gsN2Client->AddPduSession(
                ranUeNgapId,
                session.pduSessionId,
                session.gnbN3Teid,
                session.sst,
                session.sd,
                session.servicePort))
        {
            NS_LOG_UNCOND(
                "[N2 MULTI-UE] Failed to add PDU-session plan"
                << " RAN-ID=" << ranUeNgapId
                << " PSI=" << unsigned(session.pduSessionId));
            return false;
        }
    }

    if (!m_open5gsN2Client->ReceiveNgapMessage())
    {
        NS_LOG_UNCOND(
            "[N2 MULTI-UE] Registration flow failed for RAN-ID="
            << ranUeNgapId);
        return false;
    }

    NS_LOG_UNCOND(
        "[N2 MULTI-UE] Registration flow completed for RAN-ID="
        << ranUeNgapId);

    return true;
}


bool
MmWavePointToPointEpcHelper::GetOpen5gsUeSnssai(
    uint32_t ranUeNgapId,
    uint8_t& sst,
    uint32_t& sd) const
{
    if (!m_open5gsN2Client)
    {
        return false;
    }

    return m_open5gsN2Client->GetUeSnssai(
        ranUeNgapId,
        sst,
        sd);
}


void
MmWavePointToPointEpcHelper::AddUe(Ptr<NetDevice> ueDevice, uint64_t imsi)
{
    NS_LOG_FUNCTION(this << imsi << ueDevice);

    NS_LOG_UNCOND(
        "[EPC PHYSICAL REMOVE] UE registration kept in RAN/Open5GS only"
        << " IMSI=" << imsi);
}


uint8_t
MmWavePointToPointEpcHelper::ActivateEpsBearer(Ptr<NetDevice> ueDevice,
                                               uint64_t imsi,
                                               Ptr<EpcTft> tft,
                                               EpsBearer bearer)
{
    NS_LOG_FUNCTION(this << ueDevice << imsi);

    NS_ABORT_MSG_IF(
        !m_open5gsEnbApp,
        "Open5GS RAN application unavailable for local bearer provisioning");

    uint8_t bearerId =
        m_open5gsEnbApp->AddLocalBearer(imsi, bearer);

    NS_LOG_UNCOND(
        "[LOCAL DRB] Using local bearer provisioning"
        << " IMSI=" << imsi
        << " BID=" << static_cast<uint32_t>(bearerId));

    Ptr<mmwave::MmWaveUeNetDevice> ueLteDevice =
        ueDevice->GetObject<mmwave::MmWaveUeNetDevice>();

    if (ueLteDevice)
    {
        Simulator::ScheduleNow(
            &EpcUeNas::ActivateEpsBearer,
            ueLteDevice->GetNas(),
            bearer,
            tft);
    }

    return bearerId;
}

uint8_t
MmWavePointToPointEpcHelper::ActivateEpsBearer(Ptr<NetDevice> ueDevice,
                                               Ptr<EpcUeNas> ueNas,
                                               uint64_t imsi,
                                               Ptr<EpcTft> tft,
                                               EpsBearer bearer)
{
    NS_LOG_FUNCTION(this << ueDevice << imsi);

    NS_ABORT_MSG_IF(
        !m_open5gsEnbApp,
        "Open5GS RAN application unavailable for local bearer provisioning");

    uint8_t bearerId =
        m_open5gsEnbApp->AddLocalBearer(imsi, bearer);

    NS_LOG_UNCOND(
        "[LOCAL DRB] Using local bearer provisioning"
        << " IMSI=" << imsi
        << " BID=" << static_cast<uint32_t>(bearerId));

    // UE-side TFT/bearer classification remains inside the simulated UE.
    ueNas->ActivateEpsBearer(bearer, tft);

    return bearerId;
}


void
MmWavePointToPointEpcHelper::InjectTestN3Downlink(
    uint16_t rnti,
    uint8_t bid,
    uint32_t packetSize)
{
    NS_ABORT_MSG_IF(
        !m_open5gsEnbApp,
        "Step6 deterministic DL injection requires Open5GS eNB application");

    const uint32_t ipv4HeaderSize = 20;
    const uint32_t payloadSize =
        packetSize > ipv4HeaderSize
            ? packetSize - ipv4HeaderSize
            : 1;

    Ptr<Packet> packet =
        Create<Packet>(payloadSize);

    Ipv4Header ipv4Header;
    ipv4Header.SetSource(Ipv4Address(std::getenv("ORAN_CORE_IP")
                                ? std::getenv("ORAN_CORE_IP")
                                : "10.53.1.2"));
    ipv4Header.SetDestination(Ipv4Address("10.45.1.2"));
    ipv4Header.SetProtocol(253);
    ipv4Header.SetPayloadSize(payloadSize);
    ipv4Header.SetTtl(64);
    ipv4Header.EnableChecksum();

    packet->AddHeader(ipv4Header);

    m_open5gsEnbApp->InjectFromN3(
        packet,
        rnti,
        bid);

    NS_LOG_UNCOND(
        "[STEP6-TEST-DL] t="
        << Simulator::Now().GetSeconds()
        << " RNTI=" << rnti
        << " BID=" << static_cast<uint32_t>(bid)
        << " bytes=" << packetSize);
}


Ptr<Node>
MmWavePointToPointEpcHelper::GetPgwNode()
{
    return nullptr;
}



Ptr<Node>
MmWavePointToPointEpcHelper::GetMmeNode()
{
    return nullptr;
}


Ipv4InterfaceContainer
MmWavePointToPointEpcHelper::AssignUeIpv4Address(NetDeviceContainer ueDevices)
{
    return m_uePgwAddressHelper.Assign(ueDevices);
}

Ipv6InterfaceContainer
MmWavePointToPointEpcHelper::AssignUeIpv6Address(NetDeviceContainer ueDevices)
{
    for (NetDeviceContainer::Iterator iter = ueDevices.Begin(); iter != ueDevices.End(); iter++)
    {
        Ptr<Icmpv6L4Protocol> icmpv6 = (*iter)->GetNode()->GetObject<Icmpv6L4Protocol>();
        icmpv6->SetAttribute("DAD", BooleanValue(false));
    }
    return m_uePgwAddressHelper6.Assign(ueDevices);
}

Ipv4Address
MmWavePointToPointEpcHelper::GetUeDefaultGatewayAddress()
{
    // Logical UE-side gateway for the real Open5GS-integrated path.
    // No dependency on the simulated SGW/PGW node.
    return Ipv4Address("10.45.1.1");
}


Ipv6Address
MmWavePointToPointEpcHelper::GetUeDefaultGatewayAddress6()
{
    return Ipv6Address("7777:f00d::1");
}



int64_t
MmWavePointToPointEpcHelper::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    return 0;
}


} // namespace mmwave

} // namespace ns3
