#include <cmath>
/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Andrea Lacava <thecave003@gmail.com>
 *          Michele Polese <michele.polese@gmail.com>
 */

#include <cstdlib>
#include "ns3/core-module.h"
#include <sstream>
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/point-to-point-helper.h"
#include <ns3/lte-ue-net-device.h>
#include "ns3/mmwave-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/lte-helper.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/mmwave-radio-energy-model-enb-helper.h"
#include <fstream>
#include "ns3/mmwave-flex-tti-mac-scheduler.h"
#include "ns3/mc-ue-net-device.h"


using namespace ns3;
using namespace mmwave;

/**
 * Scenario Zero
 *
 */

NS_LOG_COMPONENT_DEFINE ("ScenarioZero");

void
EnergyConsumptionUpdate (int nodeIndex,
                         std::string filename,
                         double totaloldEnergyConsumption,
                         double totalnewEnergyConsumption)
{
  Time currentTime = Simulator::Now ();

  std::ofstream outFile;
  outFile.open (filename, std::ios_base::out | std::ios_base::app);

  outFile << currentTime.GetSeconds ()
          << ","
          << totalnewEnergyConsumption
          << ","
          << (totalnewEnergyConsumption - totaloldEnergyConsumption)
          << std::endl;
}



void
PrintGnuplottableUeListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteUeNetDevice> uedev = node->GetDevice (j)->GetObject<LteUeNetDevice> ();
          Ptr<MmWaveUeNetDevice> mmuedev = node->GetDevice (j)->GetObject<MmWaveUeNetDevice> ();
          Ptr<McUeNetDevice> mcuedev = node->GetDevice (j)->GetObject<McUeNetDevice> ();
          if (uedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << uedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
          else if (mmuedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << mmuedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
          else if (mcuedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << mcuedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
        }
    }
}

void
PrintGnuplottableEnbListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteEnbNetDevice> enbdev = node->GetDevice (j)->GetObject<LteEnbNetDevice> ();
          Ptr<MmWaveEnbNetDevice> mmdev = node->GetDevice (j)->GetObject<MmWaveEnbNetDevice> ();
          if (enbdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << enbdev->GetCellId () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"blue\" front  point pt 4 ps "
                         "0.3 lc rgb \"blue\" offset 0,0"
                      << std::endl;
            }
          else if (mmdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << mmdev->GetCellId () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"red\" front  point pt 4 ps "
                         "0.3 lc rgb \"red\" offset 0,0"
                      << std::endl;
            }
        }
    }
}

void
PrintPosition (Ptr<Node> node)
{
  Ptr<MobilityModel> model = node->GetObject<MobilityModel> ();
  NS_LOG_UNCOND ("Position +****************************** " << model->GetPosition () << " at time "
                                                             << Simulator::Now ().GetSeconds ());
}

static ns3::GlobalValue g_bufferSize ("bufferSize", "RLC tx buffer size (MB)",
                                      ns3::UintegerValue (10),
                                      ns3::MakeUintegerChecker<uint32_t> ());

static ns3::GlobalValue g_enableTraces ("enableTraces", "If true, generate ns-3 traces",
                                        ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2lteEnabled ("e2lteEnabled", "If true, send LTE E2 reports",
                                        ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2nrEnabled ("e2nrEnabled", "If true, send NR E2 reports",
                                       ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2du ("e2du", "If true, send DU reports", ns3::BooleanValue (true),
                                ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2cuUp ("e2cuUp", "If true, send CU-UP reports", ns3::BooleanValue (true),
                                  ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2cuCp ("e2cuCp", "If true, send CU-CP reports", ns3::BooleanValue (true),
                                  ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_reducedPmValues ("reducedPmValues", "If true, use a subset of the the pm containers",
                                        ns3::BooleanValue (false), ns3::MakeBooleanChecker ());

static ns3::GlobalValue
    g_hoSinrDifference ("hoSinrDifference",
                        "The value for which an handover between MmWave eNB is triggered",
                        ns3::DoubleValue (3), ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue
    g_indicationPeriodicity ("indicationPeriodicity",
                             "E2 Indication Periodicity reports (value in seconds)",
                             ns3::DoubleValue (0.1), ns3::MakeDoubleChecker<double> (0.01, 2.0));

static ns3::GlobalValue g_simTime ("simTime", "Simulation time in seconds", ns3::DoubleValue (1000),
                                   ns3::MakeDoubleChecker<double> (0.1, 1000.0));

static ns3::GlobalValue g_outageThreshold ("outageThreshold",
                                           "SNR threshold for outage events [dB]", // use -1000.0 with NoAuto
                                           ns3::DoubleValue (-5.0),
                                           ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue g_numberOfRaPreambles (
    "numberOfRaPreambles",
    "how many random access preambles are available for the contention based RACH process",
    ns3::UintegerValue (40), // Indicated for TS use case, 52 is default
    ns3::MakeUintegerChecker<uint8_t> ());

static ns3::GlobalValue
    g_handoverMode ("handoverMode",
                    "HO euristic to be used,"
                    "can be only \"NoAuto\", \"FixedTtt\", \"DynamicTtt\",   \"Threshold\"",
                    ns3::StringValue ("DynamicTtt"), ns3::MakeStringChecker ());

static ns3::GlobalValue g_e2TermIp ("e2TermIp", "The IP address of the RIC E2 termination",
                                    ns3::StringValue ("127.0.0.1"), ns3::MakeStringChecker ());

static ns3::GlobalValue
    g_enableE2FileLogging ("enableE2FileLogging",
                           "If true, generate offline file logging instead of connecting to RIC",
                           ns3::BooleanValue (false), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_controlFileName ("controlFileName",
                                           "The path to the control file (can be absolute)",
                                           ns3::StringValue (""),
                                           ns3::MakeStringChecker ());


static void
BindCurrentUesToSlices (
    NetDeviceContainer mcUeDevs,
    NetDeviceContainer mmWaveEnbDevs,
    Ptr<MmWavePointToPointEpcHelper> epcHelper,
    std::map<uint64_t, uint32_t> imsiToRanUeId,
    std::map<uint64_t, uint32_t> imsiToSlice)
{
  for (uint32_t u = 0; u < mcUeDevs.GetN (); ++u)
    {
      Ptr<McUeNetDevice> ueDev =
          DynamicCast<McUeNetDevice> (mcUeDevs.Get (u));

      if (ueDev == nullptr)
        {
          continue;
        }

      const uint64_t imsi = ueDev->GetImsi ();

      auto ranIdIt = imsiToRanUeId.find (imsi);

      NS_ABORT_MSG_IF (
          ranIdIt == imsiToRanUeId.end (),
          "Strict S-NSSAI binding requires a registered RAN-UE-NGAP-ID");

      NS_ABORT_MSG_IF (
          epcHelper == nullptr,
          "Strict S-NSSAI binding requires the Open5GS EPC helper");

      uint8_t requestedSst = 0;
      uint32_t requestedSd = 0;

      const bool hasRegisteredSnssai =
          epcHelper->GetOpen5gsUeSnssai (
              ranIdIt->second,
              requestedSst,
              requestedSd);

      NS_ABORT_MSG_IF (
          !hasRegisteredSnssai,
          "Strict S-NSSAI binding requires core-registered S-NSSAI");

      // Authoritative current radio state.
      Ptr<LteUeRrc> mmWaveRrc = ueDev->GetMmWaveRrc ();
      Ptr<LteUeRrc> lteRrc = ueDev->GetLteRrc ();

      NS_LOG_UNCOND (
          "[SLICE-STATE] IMSI=" << imsi
          << " mmCell=" << (mmWaveRrc ? mmWaveRrc->GetCellId () : 0)
          << " mmRNTI=" << (mmWaveRrc ? mmWaveRrc->GetRnti () : 0)
          << " lteCell=" << (lteRrc ? lteRrc->GetCellId () : 0)
          << " lteRNTI=" << (lteRrc ? lteRrc->GetRnti () : 0)
          << " SST=" << unsigned(requestedSst)
          << " SD=0x" << std::hex << requestedSd << std::dec);

      if (mmWaveRrc == nullptr)
        {
          continue;
        }

      const uint16_t cellId = mmWaveRrc->GetCellId ();
      const uint16_t rnti = mmWaveRrc->GetRnti ();

      if (cellId == 0 || rnti == 0)
        {
          continue; // attachment not complete
        }

      Ptr<MmWaveEnbNetDevice> servingGnb = nullptr;

      for (uint32_t g = 0; g < mmWaveEnbDevs.GetN (); ++g)
        {
          Ptr<MmWaveEnbNetDevice> candidate =
              mmWaveEnbDevs.Get (g)->GetObject<MmWaveEnbNetDevice> ();

          if (candidate != nullptr && candidate->HasCellId (cellId))
            {
              servingGnb = candidate;
              break;
            }
        }

      if (servingGnb == nullptr)
        {
          continue;
        }

      Ptr<MmWaveFlexTtiMacScheduler> scheduler =
          DynamicCast<MmWaveFlexTtiMacScheduler> (
              servingGnb->GetSchedulerForCellId (cellId));

      NS_ABORT_MSG_IF (
          scheduler == nullptr,
          "Unable to resolve slice scheduler for serving CellId");

      const uint32_t desiredSlice =
          scheduler->ResolveSliceIdBySnssai (
              requestedSst,
              requestedSd);

      NS_ABORT_MSG_IF (
          desiredSlice == 0,
          "No radio slice policy matches core-registered S-NSSAI");

      NS_LOG_UNCOND (
          "[S-NSSAI-BIND] IMSI=" << imsi
          << " RAN-ID=" << ranIdIt->second
          << " SST=" << unsigned(requestedSst)
          << " SD=0x"
          << std::hex << requestedSd << std::dec
          << " -> SliceId=" << desiredSlice
          << " CellId=" << cellId
          << " RNTI=" << rnti
          << " source=snssai");

      const uint32_t currentSlice =
          scheduler->GetSliceIdForUe (rnti);

      if (currentSlice != desiredSlice)
        {
          NS_ABORT_MSG_IF (
              !scheduler->AssociateUeToSlice (
                  rnti,
                  desiredSlice),
              "Failed to associate UE with slice");

          NS_LOG_UNCOND (
              "[SLICE-BIND] UE-index=" << u
              << " IMSI=" << imsi
              << " CellId=" << cellId
              << " RNTI=" << rnti
              << " Slice=" << currentSlice
              << "->" << desiredSlice);
        }
      // Step 6: same physical UE / same RNTI, different DRBs
      // are associated with different radio slices.
      if (u == 0)
        {
          const uint32_t secondSessionSlice =
              scheduler->ResolveSliceIdBySnssai (
                  2,
                  0x000002);

          NS_ABORT_MSG_IF (
              secondSessionSlice == 0,
              "Step6: SST2/SD000002 has no radio slice policy");

          NS_ABORT_MSG_IF (
              !scheduler->AssociateFlowToSlice (
                  rnti,
                  3,
                  desiredSlice),
              "Step6: failed LCID3 flow binding");

          NS_ABORT_MSG_IF (
              !scheduler->AssociateFlowToSlice (
                  rnti,
                  4,
                  secondSessionSlice),
              "Step6: failed LCID4 flow binding");

          static std::map<uint16_t, bool> step6FlowBindLogged;

          if (!step6FlowBindLogged[rnti])
            {
              NS_LOG_UNCOND (
                  "[STEP6-FLOW-BIND] RNTI=" << rnti
                  << " LCID3->Slice" << desiredSlice
                  << " LCID4->Slice" << secondSessionSlice);

              step6FlowBindLogged[rnti] = true;
            }
        }
    }

  // Continuous reconciliation handles delayed attach and handover.
  Simulator::Schedule (
      MilliSeconds (250),
      &BindCurrentUesToSlices,
      mcUeDevs,
      mmWaveEnbDevs,
      epcHelper,
      imsiToRanUeId,
      imsiToSlice);
}

static void
InjectStep6DeterministicDualDl (
    Ptr<MmWavePointToPointEpcHelper> epcHelper)
{
  NS_ABORT_MSG_IF (
      epcHelper == nullptr,
      "Step6 deterministic test requires EPC helper");

  NS_LOG_UNCOND (
      "[STEP6-DETERMINISTIC] injecting BID1 + BID2 at t="
      << Simulator::Now ().GetSeconds ());

  // Both bearers are queued inside the SAME simulator event.
  // BID1 -> LCID3 -> Slice1
  // BID2 -> LCID4 -> Slice2
  for (uint32_t i = 0; i < 8; ++i)
    {
      epcHelper->InjectTestN3Downlink (1, 1, 220);
      epcHelper->InjectTestN3Downlink (1, 2, 220);
    }
}


int
main (int argc, char *argv[])
{
  
    ns3::GlobalValue::Bind(
        "SimulatorImplementationType",
        ns3::StringValue("ns3::RealtimeSimulatorImpl"));

// LogComponentEnableAll (LOG_PREFIX_ALL);
   LogComponentEnable ("KpmIndication", LOG_LEVEL_DEBUG);
// LogComponentEnable ("MmWaveEnbNetDevice", LOG_LEVEL_DEBUG);

  // The maximum X coordinate of the scenario


  double maxXAxis = 4000;
  // The maximum Y coordinate of the scenario
  double maxYAxis = 4000;

  // Command line arguments
  // Number of UEs can be selected at runtime, e.g. --ues=5.
  uint32_t ues = 3;
  uint32_t nMmWaveEnbNodes = 3;
  double bandwidth = 20e6;
  double centerFrequency = 3.5e9;
  double isd = 1000;
  double ueSpacing = 2.0;
  uint32_t numAntennasMcUe = 1;
  uint32_t numAntennasMmWave = 1;

  CommandLine cmd;
  cmd.AddValue ("ues", "Number of UEs to create and register with Open5GS", ues);
  cmd.AddValue ("N_MmWaveEnbNodes", "Number of mmWave gNBs", nMmWaveEnbNodes);
  cmd.AddValue ("Bandwidth", "Carrier bandwidth in Hz", bandwidth);
  cmd.AddValue ("CenterFrequency", "Center frequency in Hz", centerFrequency);
  cmd.AddValue ("IntersideDistanceCells", "Distance between gNBs in meters", isd);
  cmd.AddValue ("IntersideDistanceUEs", "UE spacing in meters", ueSpacing);
  cmd.AddValue ("N_AntennasMcUe", "UE antenna count", numAntennasMcUe);
  cmd.AddValue ("N_AntennasMmWave", "gNB antenna count", numAntennasMmWave);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (ues < 1 || ues > 1000, "Invalid UE count");
  NS_ABORT_MSG_IF (nMmWaveEnbNodes < 1 || nMmWaveEnbNodes > 255,
                   "Invalid gNB count");
  NS_ABORT_MSG_IF (!std::isfinite (bandwidth) || bandwidth <= 0 ||
                   !std::isfinite (centerFrequency) || centerFrequency <= 0 ||
                   !std::isfinite (isd) || isd <= 0 ||
                   !std::isfinite (ueSpacing) || ueSpacing <= 0,
                   "Invalid radio or distance parameter");
  NS_ABORT_MSG_IF (numAntennasMcUe < 1 || numAntennasMmWave < 1,
                   "Invalid antenna count");

  NS_LOG_UNCOND ("[GUI-PARAMS] UEs=" << ues
                 << " gNBs=" << nMmWaveEnbNodes
                 << " bandwidthHz=" << bandwidth
                 << " frequencyHz=" << centerFrequency
                 << " cellDistanceM=" << isd
                 << " ueSpacingM=" << ueSpacing
                 << " ueAntennas=" << numAntennasMcUe
                 << " gnbAntennas=" << numAntennasMmWave);

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  bool harqEnabled = true;

  UintegerValue uintegerValue;
  BooleanValue booleanValue;
  StringValue stringValue;
  DoubleValue doubleValue;

  GlobalValue::GetValueByName ("hoSinrDifference", doubleValue);
  double hoSinrDifference = doubleValue.Get ();
  GlobalValue::GetValueByName ("bufferSize", uintegerValue);
  uint32_t bufferSize = uintegerValue.Get ();
  GlobalValue::GetValueByName ("enableTraces", booleanValue);
  bool enableTraces = booleanValue.Get ();
  GlobalValue::GetValueByName ("outageThreshold", doubleValue);
  double outageThreshold = doubleValue.Get ();
  GlobalValue::GetValueByName ("handoverMode", stringValue);
  std::string handoverMode = stringValue.Get ();
  GlobalValue::GetValueByName ("e2TermIp", stringValue);
  std::string e2TermIp = stringValue.Get ();
  GlobalValue::GetValueByName ("enableE2FileLogging", booleanValue);
  bool enableE2FileLogging = booleanValue.Get ();
  GlobalValue::GetValueByName ("numberOfRaPreambles", uintegerValue);
  uint8_t numberOfRaPreambles = uintegerValue.Get ();

  NS_LOG_UNCOND ("bufferSize " << bufferSize << " OutageThreshold " << outageThreshold
                               << " HandoverMode " << handoverMode << " e2TermIp " << e2TermIp
                               << " enableE2FileLogging " << enableE2FileLogging);

  GlobalValue::GetValueByName ("e2lteEnabled", booleanValue);
  bool e2lteEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2nrEnabled", booleanValue);
  bool e2nrEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2du", booleanValue);
  bool e2du = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuUp", booleanValue);
  bool e2cuUp = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuCp", booleanValue);
  bool e2cuCp = booleanValue.Get ();

  GlobalValue::GetValueByName ("reducedPmValues", booleanValue);
  bool reducedPmValues = booleanValue.Get ();

  GlobalValue::GetValueByName ("indicationPeriodicity", doubleValue);
  double indicationPeriodicity = doubleValue.Get ();
  GlobalValue::GetValueByName ("controlFileName", stringValue);
  std::string controlFilename = stringValue.Get ();

  NS_LOG_UNCOND ("e2lteEnabled " << e2lteEnabled << " e2nrEnabled " << e2nrEnabled << " e2du "
                                 << e2du << " e2cuCp " << e2cuCp << " e2cuUp " << e2cuUp
                                 << " controlFilename " << controlFilename
                                 << " indicationPeriodicity " << indicationPeriodicity);

  Config::SetDefault ("ns3::LteEnbNetDevice::ControlFileName", StringValue (controlFilename));
  Config::SetDefault ("ns3::LteEnbNetDevice::E2Periodicity", DoubleValue (indicationPeriodicity));
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::E2Periodicity",
                      DoubleValue (indicationPeriodicity));

  Config::SetDefault ("ns3::MmWaveHelper::E2ModeLte", BooleanValue (e2lteEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::E2ModeNr", BooleanValue (e2nrEnabled));

  // The DU PM reports should come from both NR gNB as well as LTE eNB,
  // since in the RLC/MAC/PHY entities are present in BOTH NR gNB as well as LTE eNB.
  // DU reports from LTE eNB are not implemented in this release
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue (e2du));

  // The CU-UP PM reports should only come from LTE eNB, since in the NS3 “EN-DC
  // simulation (Option 3A)”, the PDCP is only in the LTE eNB and NOT in the NR gNB
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue (e2cuUp));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuUpReport", BooleanValue (e2cuUp));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue (e2cuCp));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuCpReport", BooleanValue (e2cuCp));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue (reducedPmValues));
  Config::SetDefault ("ns3::LteEnbNetDevice::ReducedPmValues", BooleanValue (reducedPmValues));

  Config::SetDefault ("ns3::LteEnbNetDevice::EnableE2FileLogging",
                      BooleanValue (enableE2FileLogging));
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableE2FileLogging",
                      BooleanValue (enableE2FileLogging));

  Config::SetDefault ("ns3::MmWaveEnbMac::NumberOfRaPreambles",
                      UintegerValue (numberOfRaPreambles));

  Config::SetDefault ("ns3::MmWaveHelper::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::UseIdealRrc", BooleanValue (true));
  Config::SetDefault ("ns3::MmWaveHelper::E2TermIp", StringValue (e2TermIp));

  Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue (100));
  //Config::SetDefault ("ns3::MmWaveBearerStatsCalculator::EpochDuration", TimeValue (MilliSeconds (10.0)));

  // set to false to use the 3GPP radiation pattern (proper configuration of the bearing and downtilt angles is needed)
  Config::SetDefault("ns3::PhasedArrayModel::AntennaElement",
                       PointerValue(CreateObject<IsotropicAntennaModel>()));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (100.0)));
  Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod",
                      TimeValue (MilliSeconds (100)));

  Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue (MilliSeconds (10.0)));
  Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer",
                      TimeValue (MilliSeconds (10.0)));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize",
                      UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (bufferSize * 1024 * 1024));

  Config::SetDefault ("ns3::LteEnbRrc::OutageThreshold", DoubleValue (outageThreshold));
  Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode", StringValue (handoverMode));
  Config::SetDefault ("ns3::LteEnbRrc::HoSinrDifference", DoubleValue (hoSinrDifference));

  // Carrier bandwidth configured through CommandLine.
  // Center frequency configured through CommandLine.
  // gNB spacing configured through CommandLine.
  // UE antennas configured through CommandLine.
  // gNB antennas configured through CommandLine.

  NS_LOG_INFO ("Bandwidth " << bandwidth << " centerFrequency " << double (centerFrequency)
                            << " isd " << isd << " numAntennasMcUe " << numAntennasMcUe
                            << " numAntennasMmWave " << numAntennasMmWave);

  // Set the number of antennas in the devices
  Config::SetDefault ("ns3::McUeNetDevice::AntennaNum", UintegerValue (numAntennasMcUe));
  Config::SetDefault ("ns3::MmWaveNetDevice::AntennaNum", UintegerValue (numAntennasMmWave));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue (bandwidth));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue (centerFrequency));

  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  mmwaveHelper->SetPathlossModelType ("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType ("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper> ();
  mmwaveHelper->SetEpcHelper (epcHelper);

  // nMmWaveEnbNodes defined before CommandLine parsing.
  uint8_t nLteEnbNodes = 1;
  uint32_t nUeNodes = ues; //* nMmWaveEnbNodes;
  //uint8_t nUeNodes = 1;
  NS_LOG_INFO (" Bandwidth " << bandwidth << " centerFrequency " << double (centerFrequency)
                             << " isd " << isd << " numAntennasMcUe " << numAntennasMcUe
                             << " numAntennasMmWave " << numAntennasMmWave << " nMmWaveEnbNodes "
                             << unsigned (nMmWaveEnbNodes));

  // Internet stack helper is still used for simulated UE nodes.
  // The legacy simulated PGW <-> remoteHost test network is removed.
  // User traffic is validated through the real Open5GS N3 path.
  InternetStackHelper internet;
  Ipv4StaticRoutingHelper ipv4RoutingHelper;

  // create LTE, mmWave eNB nodes and UE node
  NodeContainer ueNodes;
  NodeContainer mmWaveEnbNodes;
  NodeContainer lteEnbNodes;
  NodeContainer allEnbNodes;
  mmWaveEnbNodes.Create (nMmWaveEnbNodes);
  lteEnbNodes.Create (nLteEnbNodes);
  ueNodes.Create (nUeNodes);
  allEnbNodes.Add (lteEnbNodes);
  allEnbNodes.Add (mmWaveEnbNodes);

  // Position
  Vector centerPosition = Vector (maxXAxis / 2, maxYAxis / 2, 3);

  // Install Mobility Model
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator> ();

  // We want a center with one LTE enb and one mmWave co-located in the same place
  enbPositionAlloc->Add (centerPosition);
  enbPositionAlloc->Add (centerPosition);

  double x, y;
  double nConstellation = nMmWaveEnbNodes - 1;

  // This guarantee that each of the rest BSs is placed at the same distance from the two co-located in the center
  for (int8_t i = 0; i < nConstellation; ++i)
    {
      x = isd * cos ((2 * M_PI * i) / (nConstellation));
      y = isd * sin ((2 * M_PI * i) / (nConstellation));
      enbPositionAlloc->Add (Vector (centerPosition.x + x, centerPosition.y + y, 3));
    }

  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  enbmobility.SetPositionAllocator (enbPositionAlloc);
  enbmobility.Install (allEnbNodes);

  // Deterministic slicing-validation UE placement.
  // Keep all UEs close to the center mmWave cell and stationary so that
  // radio-resource differences come from slicing, not mobility/handover.
  MobilityHelper uemobility;

  Ptr<ListPositionAllocator> uePositionAlloc =
      CreateObject<ListPositionAllocator> ();

  for (uint32_t u = 0; u < nUeNodes; ++u)
    {
      uePositionAlloc->Add (
          Vector (centerPosition.x + 5.0 + (ueSpacing * u),
                  centerPosition.y + 5.0,
                  1.5));
    }

  uemobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  uemobility.SetPositionAllocator (uePositionAlloc);
  uemobility.Install (ueNodes);

  NS_LOG_UNCOND ("[SLICE-VALIDATION] "
                 << nUeNodes
                 << " stationary UEs placed near center gNB");

  // Install mmWave, lte, mc Devices to the nodes
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice (lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice (mmWaveEnbNodes);

  // -----------------------------------------------------------------
  // Static slicing foundation.
  // These policies are service-model independent: later RC or CCC can
  // update the same scheduler-side SliceManager.
  //
  // Initial validation profile:
  //   Slice 1 -> 50%
  //   Slice 2 -> 30%
  //   Slice 3 -> 20%
  // -----------------------------------------------------------------
  for (uint32_t g = 0; g < mmWaveEnbDevs.GetN (); ++g)
    {
      Ptr<MmWaveEnbNetDevice> gnb =
          mmWaveEnbDevs.Get (g)->GetObject<MmWaveEnbNetDevice> ();

      Ptr<MmWaveFlexTtiMacScheduler> scheduler =
          DynamicCast<MmWaveFlexTtiMacScheduler> (gnb->GetScheduler ());

      NS_ABORT_MSG_IF (scheduler == nullptr,
                       "Static slicing requires MmWaveFlexTtiMacScheduler");

      MmWaveSlicePolicy slice1;
      slice1.sliceId = 1;
      slice1.sst = 1;
      slice1.sd = 0x000001;
      slice1.minRatio = 30;
      slice1.maxRatio = 70;
      slice1.dedicatedRatio = 20;

      MmWaveSlicePolicy slice2;
      slice2.sliceId = 2;
      slice2.sst = 2;
      slice2.sd = 0x000002;
      slice2.minRatio = 20;
      slice2.maxRatio = 50;
      slice2.dedicatedRatio = 10;

      MmWaveSlicePolicy slice3;
      slice3.sliceId = 3;
      slice3.sst = 3;
      slice3.sd = 0x000003;
      slice3.minRatio = 10;
      slice3.maxRatio = 30;
      slice3.dedicatedRatio = 10;

      NS_ABORT_MSG_IF (!scheduler->SetSlicePolicy (slice1),
                       "Invalid Slice 1 policy");
      NS_ABORT_MSG_IF (!scheduler->SetSlicePolicy (slice2),
                       "Invalid Slice 2 policy");
      NS_ABORT_MSG_IF (!scheduler->SetSlicePolicy (slice3),
                       "Invalid Slice 3 policy");

      NS_ABORT_MSG_IF (!scheduler->SetSliceTargetRatio (1, 50),
                       "Invalid Slice 1 target ratio");
      NS_ABORT_MSG_IF (!scheduler->SetSliceTargetRatio (2, 30),
                       "Invalid Slice 2 target ratio");
      NS_ABORT_MSG_IF (!scheduler->SetSliceTargetRatio (3, 20),
                       "Invalid Slice 3 target ratio");

      NS_LOG_UNCOND ("[SLICE-POLICY] Cell=" << gnb->GetCellId ()
                     << " S1=50% S2=30% S3=20%");
    }

  // ---- mmWave gNB energy accounting ----
  BasicEnergySourceHelper basicEnergySourceHelper;

  basicEnergySourceHelper.Set (
      "BasicEnergySourceInitialEnergyJ",
      DoubleValue (1000000000000.0));

  basicEnergySourceHelper.Set (
      "BasicEnergySupplyVoltageV",
      DoubleValue (5.0));

  energy::EnergySourceContainer energySources =
      basicEnergySourceHelper.Install (mmWaveEnbNodes);

  MmWaveRadioEnergyModelEnbHelper nrEnbEnergyHelper;

  energy::DeviceEnergyModelContainer enbEnergyModels =
      nrEnbEnergyHelper.Install (mmWaveEnbDevs, energySources);

  for (uint32_t i = 0; i < mmWaveEnbDevs.GetN (); ++i)
    {
      Ptr<MmWaveEnbNetDevice> mmdev =
          mmWaveEnbDevs.Get (i)->GetObject<MmWaveEnbNetDevice> ();

      NS_ASSERT (mmdev != nullptr);

      uint16_t cellId = mmdev->GetCellId ();

      std::ostringstream filename;
      filename << "/telemetry/energyfilecell" << cellId << ".csv";

      std::ofstream energyFile (
          filename.str (),
          std::ios_base::out | std::ios_base::trunc);

      energyFile << "Time,NetEnergy,DiffEnergy" << std::endl;
      energyFile.close ();

      enbEnergyModels.Get (i)->TraceConnectWithoutContext (
          "TotalEnergyConsumption",
          MakeBoundCallback (
              &EnergyConsumptionUpdate,
              static_cast<int> (i),
              filename.str ()));

      NS_LOG_UNCOND (
          "[ENERGY] Attached energy model to mmWave Cell "
          << cellId
          << " -> "
          << filename.str ());
    }
  // ---------------------------------------


  NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice (ueNodes);

  // Desired UE slice membership is keyed by stable IMSI, NOT RNTI.
  // RNTI binding will be performed after RRC attachment.
  std::map<uint64_t, uint32_t> staticImsiToSlice;

  // Slicing validation pattern:
  // UE0 -> Slice1
  // UE1 -> Slice2
  // UE2 -> Slice3
  const uint32_t slicePattern[3] = {1, 2, 3};

  for (uint32_t u = 0; u < mcUeDevs.GetN (); ++u)
    {
      Ptr<McUeNetDevice> ueDev =
          DynamicCast<McUeNetDevice> (mcUeDevs.Get (u));

      NS_ABORT_MSG_IF (ueDev == nullptr,
                       "Unable to access McUeNetDevice for slicing");

      uint64_t imsi = ueDev->GetImsi ();
      uint32_t sliceId;

      // Dedicated validation layout for intra-slice multi-UE scheduling:
      // UE0 -> Slice 1
      // UE1 -> Slice 1
      // UE2 -> Slice 2
      // UE3 -> Slice 3
      //
      // Other UE counts preserve the original cyclic mapping.
      if (ues == 4)
        {
          static const uint32_t multiUeSlicePattern[4] =
              {1, 1, 2, 3};

          sliceId = multiUeSlicePattern[u];
        }
      else
        {
          sliceId = slicePattern[u % 3];
        }

      staticImsiToSlice[imsi] = sliceId;

      NS_LOG_UNCOND ("[SLICE-DESIRED] UE index=" << u
                     << " IMSI=" << imsi
                     << " -> Slice=" << sliceId);
    }

  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (mcUeDevs));

  // Only successfully registered Open5GS UEs are used by
  // the S-NSSAI-driven radio binding path.
  std::map<uint64_t, uint32_t> registeredImsiToRanUeId;

  // Dynamic N-UE Open5GS registration.
  // Each UE uses its actual IPv4 address assigned by ns-3.
  for (uint32_t u = 0; u < ueIpIface.GetN (); ++u)
    {
      // Dynamic SUPI allocation:
      // UE0 -> 001010123456780
      // UE1 -> 001010123456781
      // UE2 -> 001010123456782
      // ...
      const uint32_t imsiSuffix = 6780 + u;

      NS_ABORT_MSG_IF (
          imsiSuffix > 9999,
          "Dynamic Open5GS IMSI pool exhausted");

      const std::string supi =
          "00101012345" + std::to_string (imsiSuffix);

      const uint32_t ranUeNgapId = u + 1;
      const uint32_t gnbN3Teid = u + 1;
      std::ostringstream ueIpStream;
      ueIpStream << ueIpIface.GetAddress (u);
      const std::string ueIpv4 = ueIpStream.str ();

      Ptr<McUeNetDevice> sliceUeDev =
          DynamicCast<McUeNetDevice> (mcUeDevs.Get (u));

      NS_ABORT_MSG_IF (
          sliceUeDev == nullptr,
          "Unable to resolve UE S-NSSAI mapping");

      const uint64_t radioImsi = sliceUeDev->GetImsi ();
      const uint32_t desiredSliceId =
          staticImsiToSlice.at (radioImsi);

      uint8_t requestedSst = 0;
      uint32_t requestedSd = 0;

      switch (desiredSliceId)
        {
        case 1:
          requestedSst = 1;
          requestedSd = 0x000001;
          break;
        case 2:
          requestedSst = 2;
          requestedSd = 0x000002;
          break;
        case 3:
          requestedSst = 3;
          requestedSd = 0x000003;
          break;
        default:
          NS_ABORT_MSG ("Unknown SliceId for S-NSSAI mapping");
        }

      NS_LOG_UNCOND (
          "[S-NSSAI-DESIRED] UE-index=" << u
          << " IMSI=" << radioImsi
          << " SliceId=" << desiredSliceId
          << " SST=" << unsigned(requestedSst)
          << " SD=0x" << std::hex
          << requestedSd
          << std::dec);

      NS_LOG_UNCOND ("[N2 MULTI-UE TEST] UE index=" << u
                     << " SUPI=" << supi
                     << " IP=" << ueIpv4
                     << " RAN-ID=" << ranUeNgapId
                     << " DL-TEID=" << gnbN3Teid);

      std::vector<
          MmWavePointToPointEpcHelper::Open5gsPduSessionConfig>
          additionalSessions;

      // Step 6: same UE, multiple PDU sessions on different S-NSSAIs.
      if (u == 0)
        {
          MmWavePointToPointEpcHelper::Open5gsPduSessionConfig secondSession;

          secondSession.pduSessionId = 6;
          secondSession.gnbN3Teid = 101;
          secondSession.sst = 2;
          secondSession.sd = 0x000002;
          secondSession.servicePort = 6000;

          additionalSessions.push_back (secondSession);

          NS_LOG_UNCOND (
              "[STEP6-PDU-PLAN] IMSI=" << radioImsi
              << " PSI=5 SST=1 SD=0x000001 port=5000"
              << " + PSI=6 SST=2 SD=0x000002 port=6000");
        }

      if (!epcHelper->RegisterOpen5gsUe (
              supi,
              ranUeNgapId,
              ueIpv4,
              gnbN3Teid,
              requestedSst,
              requestedSd,
              additionalSessions))
        {
          NS_LOG_UNCOND ("[N2 MULTI-UE TEST] FAILED UE index=" << u);
        }
      else
        {
          registeredImsiToRanUeId[radioImsi] = ranUeNgapId;

          NS_LOG_UNCOND (
              "[N2 MULTI-UE TEST] SUCCESS UE index=" << u
              << " IMSI=" << radioImsi
              << " RAN-ID=" << ranUeNgapId);
        }
    }
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting =
          ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  // Add X2 interfaces
  mmwaveHelper->AddX2Interface (lteEnbNodes, mmWaveEnbNodes);

  // Manual attachment
  mmwaveHelper->AttachToClosestEnb (mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

  // Step 6 radio bearer split for UE1:
  //   PSI 5 / UDP 5000 -> default bearer BID1 -> LCID3
  //   PSI 6 / UDP 6000 -> dedicated bearer BID2 -> LCID4
  //
  // The default bearer was already registered by AttachToClosestEnb().
  // EpcTftClassifier evaluates later bearers first, so this specific
  // UDP/6000 TFT overrides the default wildcard bearer.
  if (mcUeDevs.GetN () > 0)
    {
      Ptr<McUeNetDevice> ue0 =
          DynamicCast<McUeNetDevice> (mcUeDevs.Get (0));

      NS_ABORT_MSG_IF (
          ue0 == nullptr,
          "Step6: unable to resolve UE1 device");

      Ptr<EpcTft> secondSessionTft =
          Create<EpcTft> ();

      EpcTft::PacketFilter secondSessionFilter;
      secondSessionFilter.direction =
          EpcTft::BIDIRECTIONAL;
      secondSessionFilter.remotePortStart = 6000;
      secondSessionFilter.remotePortEnd = 6000;

      secondSessionTft->Add (secondSessionFilter);

      const uint8_t secondBearerId =
          epcHelper->ActivateEpsBearer (
              mcUeDevs.Get (0),
              ue0->GetNas (),
              ue0->GetImsi (),
              secondSessionTft,
              EpsBearer (
                  EpsBearer::NGBR_VIDEO_TCP_DEFAULT));

      NS_ABORT_MSG_IF (
          secondBearerId != 2,
          "Step6 expected UE1 second bearer BID=2");

      NS_LOG_UNCOND (
          "[STEP6-RADIO-BEARER] IMSI="
          << ue0->GetImsi ()
          << " UDP/6000 -> BID="
          << static_cast<uint32_t> (secondBearerId)
          << " expected-LCID="
          << static_cast<uint32_t> (
                 secondBearerId + 2));
    }

  // Resolve stable IMSI membership to the current radio RNTI after attachment.
  // Retry briefly because RRC attachment is asynchronous.
  Simulator::Schedule (
      MilliSeconds (100),
      &BindCurrentUesToSlices,
      mcUeDevs,
      mmWaveEnbDevs,
      epcHelper,
      registeredImsiToRanUeId,
      staticImsiToSlice);

  // Pure downlink slicing-validation workload.
  // Each UE listens on UDP port 9100. Traffic will be generated
  // independently from the Open5GS container through ogstun/N3.
  ApplicationContainer serverApps;

  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      UdpServerHelper primaryServer (5000);
      serverApps.Add (primaryServer.Install (ueNodes.Get (u)));

      if (u == 0)
        {
          UdpServerHelper secondaryServer (6000);
          serverApps.Add (secondaryServer.Install (ueNodes.Get (u)));
        }
    }

  NS_LOG_UNCOND ("[TRAFFIC] Pure-DL slicing validation: "
                 << ueNodes.GetN ()
                 << " UEs listening on UDP/5000 (UE1 also UDP/6000)");

  // Bootstrap the custom N3 UE-IP -> RNTI/BID route.
  //
  // Step 6 traffic descriptors:
  //   UDP dst-port 5000 -> primary PDU session (PSI 5)
  //   UDP dst-port 6000 -> UE1 second PDU session (PSI 6)
  //
  // Only a few tiny UL packets are sent; the actual slicing workload
  // remains pure downlink.
  ApplicationContainer bootstrapApps;

  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      UdpClientHelper primarySessionClient (
          Ipv4Address (std::getenv ("ORAN_CORE_IP")
                           ? std::getenv ("ORAN_CORE_IP")
                           : "10.53.1.2"), 5000);

      primarySessionClient.SetAttribute (
          "Interval", TimeValue (MilliSeconds (20)));
      primarySessionClient.SetAttribute (
          "MaxPackets", UintegerValue (5));
      primarySessionClient.SetAttribute (
          "PacketSize", UintegerValue (64));

      bootstrapApps.Add (
          primarySessionClient.Install (ueNodes.Get (u)));

      // UE1 has the additional PSI 6 / SST2 session.
      if (u == 0)
        {
          UdpClientHelper secondSessionClient (
              Ipv4Address (std::getenv ("ORAN_CORE_IP")
                           ? std::getenv ("ORAN_CORE_IP")
                           : "10.53.1.2"), 6000);

          secondSessionClient.SetAttribute (
              "Interval", TimeValue (MilliSeconds (20)));
          secondSessionClient.SetAttribute (
              "MaxPackets", UintegerValue (5));
          secondSessionClient.SetAttribute (
              "PacketSize", UintegerValue (64));

          bootstrapApps.Add (
              secondSessionClient.Install (ueNodes.Get (u)));
        }
    }

  NS_LOG_UNCOND (
      "[TRAFFIC] DL-dominant slicing load + Step6 UL probes: "
      "UDP/5000->PSI5, UE1 UDP/6000->PSI6");

  // Start applications
  GlobalValue::GetValueByName ("simTime", doubleValue);
  double simTime = doubleValue.Get ();

  serverApps.Start (MilliSeconds (50));
  serverApps.Stop (Seconds (simTime - 0.1));

  bootstrapApps.Start (MilliSeconds (100));
  bootstrapApps.Stop (Seconds (simTime - 0.1));

  // Report actual packets delivered to each UE UDP server.
  Simulator::Schedule (Seconds (simTime - 0.05),
      [serverApps, simTime] ()
      {
        for (uint32_t i = 0; i < serverApps.GetN (); ++i)
          {
            Ptr<UdpServer> server =
                DynamicCast<UdpServer> (serverApps.Get (i));

            if (server)
              {
                uint64_t packets = server->GetReceived ();
                NS_LOG_UNCOND (
                    "[UE-DL-RX] IMSI=" << (i == 0 ? 1 : i)
                    << " port=" << (i == 1 ? 6000 : 5000) << " appIndex=" << i << " packets=" << packets
                    << " duration_s=" << simTime);
              }
          }
      });


  // int numPrints = 5;
  // for (int i = 0; i < numPrints; i++)
  //   {
  //     for (uint32_t j = 0; j < ueNodes.GetN (); j++)
  //       {
  //         Simulator::Schedule (Seconds (i * simTime / numPrints), &PrintPosition, ueNodes.Get (j));
  //       }
  //   }

  if (enableTraces)
    {
      mmwaveHelper->EnableTraces ();
    }

  // trick to enable PHY traces for the LTE stack
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  lteHelper->Initialize ();
  lteHelper->EnablePhyTraces ();
  lteHelper->EnableMacTraces ();

  // Since nodes are randomly allocated during each run we always need to print their positions
  PrintGnuplottableUeListToFile ("ues.txt");
  PrintGnuplottableEnbListToFile ("enbs.txt");

  // Step6 deterministic scheduler validation.
  // With --ues=1, first physical UE uses RNTI=1 and locally
  // provisioned bearers BID1/LCID3 and BID2/LCID4.
  if (ues == 1)
    {
      Simulator::Schedule (
          MilliSeconds (160),
          &InjectStep6DeterministicDualDl,
          epcHelper);
    }

  bool run = true;
  if (run)
    {
      NS_LOG_UNCOND ("Simulation time is " << simTime << " seconds ");
      Simulator::Stop (Seconds (simTime));
      NS_LOG_INFO ("Run Simulation.");
      Simulator::Run ();
    }

  NS_LOG_INFO (lteHelper);
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
  return 0;
}
