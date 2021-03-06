// Global FWCore clases
#include "FWCore/MessageLogger/interface/MessageLogger.h"

// user include files
#include "HERadDamJets/FullSim/interface/FullSimNoiseAnalyzer.h"
#include <iostream>
#include <sstream>
#include <cmath>

//Ecal Rec hits 
#include "DataFormats/EcalRecHit/interface/EcalRecHitCollections.h"
#include "DataFormats/EcalRecHit/interface/EcalRecHit.h"

//Ecal det id
#include "DataFormats/EcalDetId/interface/EBDetId.h"
#include "DataFormats/EcalDetId/interface/EEDetId.h"

//Hcal Rec hits
#include "DataFormats/HcalRecHit/interface/HcalRecHitCollections.h"
#include "DataFormats/HcalRecHit/interface/HBHERecHit.h"
#include "DataFormats/HcalRecHit/interface/HFRecHit.h"

//Hcal det id
#include "DataFormats/HcalDetId/interface/HcalSubdetector.h"
#include "DataFormats/HcalDetId/interface/HcalDetId.h"

//cell geometry
#include "Geometry/Records/interface/CaloGeometryRecord.h"
#include "Geometry/CaloGeometry/interface/CaloSubdetectorGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloCellGeometry.h"

//CaloTowers
#include "DataFormats/CaloTowers/interface/CaloTower.h"
#include "DataFormats/CaloTowers/interface/CaloTowerCollection.h"

//PFCandidates
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidateFwd.h"

//GenJets
#include "DataFormats/JetReco/interface/GenJet.h"
#include "DataFormats/JetReco/interface/GenJetCollection.h"

#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TMath.h"
#include <Math/VectorUtil.h>

using namespace std;
using namespace edm;
using namespace reco;

double FullSimNoiseAnalyzer::phi(double x, double y) {
	double phi_ = atan2(y, x);
	return (phi_>=0) ?  phi_ : phi_ + 2*TMath::Pi();
}
double FullSimNoiseAnalyzer::DeltaPhi(double phi1, double phi2) {
	double phi1_= phi( cos(phi1), sin(phi1) );
	double phi2_= phi( cos(phi2), sin(phi2) );
	double dphi_= phi1_-phi2_;
	if( dphi_> TMath::Pi() ) dphi_-=2*TMath::Pi();
	if( dphi_<-TMath::Pi() ) dphi_+=2*TMath::Pi();

	return dphi_;
}
double FullSimNoiseAnalyzer::DeltaR(double phi1, double eta1, double phi2, double eta2){
	double dphi = DeltaPhi(phi1,phi2);
	double deta = eta2 - eta1;
	double dR2 = dphi*dphi + deta*deta;
	return sqrt(dR2);
}

FullSimNoiseAnalyzer::FullSimNoiseAnalyzer(const edm::ParameterSet& iConfig) :
outname(iConfig.getParameter<string>("fileName")), dRcut(iConfig.getParameter<double>("dRcut")), debug(iConfig.getParameter<bool>("debug"))
{ }

FullSimNoiseAnalyzer::~FullSimNoiseAnalyzer() { }

// ------------ method called for each event  ------------
void
FullSimNoiseAnalyzer::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) {
	iSetup.get<CaloGeometryRecord>().get(geometry);
	
	//COLLECT RECHITS
	//---------------
	
	//Access to RecHits information
	Handle<EcalRecHitCollection> EERecHits;
	bool bEEr = iEvent.getByLabel("ecalRecHit","EcalRecHitsEE", EERecHits);
	Handle<EcalRecHitCollection> EBRecHits;
	bool bEBr = iEvent.getByLabel("ecalRecHit","EcalRecHitsEB", EBRecHits);
	
	Handle<HBHERecHitCollection> HBHERecHits;
	Handle<HFRecHitCollection> HFRecHits;
	bool bHBHEr = false;
	bool bHFr = false;
	//try run1 case first
	bHBHEr = iEvent.getByLabel("hbhereco", HBHERecHits);
	bHFr = iEvent.getByLabel("hfreco", HFRecHits);
	//then try upgrade case
	if(!bHBHEr) bHBHEr = iEvent.getByLabel("hbheUpgradeReco", HBHERecHits);
	if(!bHFr) bHFr = iEvent.getByLabel("hfUpgradeReco", HFRecHits);
	
	//COLLECT CALOTOWERS
	//------------------
	Handle<CaloTowerCollection> CaloTowers;
	bool bCT = iEvent.getByLabel("towerMaker", CaloTowers);
	
	//COLLECT PFCANDS
	//------------------
	Handle<PFCandidateCollection> pfCandidates;
	bool bPC = iEvent.getByLabel("particleFlow", pfCandidates);
	
	//MATCH JETS
	//----------
	Handle<GenJetCollection> h_GenJets;
	iEvent.getByLabel("ak5GenJets", h_GenJets);
	GenJetCollection::const_iterator genIt = h_GenJets->begin();
	GenJetCollection::const_iterator gen1[2] = {h_GenJets->end(),h_GenJets->end()}; //for barrel and endcap
	
	//find leading genjet (pt cut corresponding to gen filter)
	double min_pt[2] = {10,10};
	
	for(; genIt != h_GenJets->end(); genIt++){
		double curr_pt = genIt->pt();
		double curr_eta = genIt->eta();
		if(curr_pt > min_pt[0] && fabs(curr_eta)<1.8) { //require barrel eta cuts corresponding to gen filter
			min_pt[0] = curr_pt;
			gen1[0] = genIt;
		}
		else if(curr_pt > min_pt[1] && fabs(curr_eta)>1.8 && fabs(curr_eta)<3.0) { //require endcap eta cuts corresponding to gen filter
			min_pt[1] = curr_pt;
			gen1[1] = genIt;
		}
	}
	
	//if there is a GenJet, find the offset from a cone around negative eta
	for(int g = 0; g < 2; g++){
		if(gen1[g] == h_GenJets->end()) continue;

		//reset variables
		e_hcal_noise_en = e_hcal_noise_pt = e_ecal_noise_en = e_ecal_noise_pt = e_rec_noise_en = e_rec_noise_pt = 0;
		e_calo_noise_en = e_calo_noise_pt = e_pf_noise_en = e_pf_noise_pt = 0;
		e_gen_eta = e_gen_phi = 0;
		
		//store the genjet vars
		e_gen_eta = gen1[g]->eta();
		e_gen_phi = gen1[g]->phi();
		
		//check that no jets exist in the opposite side of the detector
		double cone_eta = -e_gen_eta;
		double cone_phi = -e_gen_phi;
		bool match_gen = true;
		int phi_ctr = 0;
		while(match_gen && phi_ctr<6){
			match_gen = false;
			for(genIt = h_GenJets->begin(); genIt != h_GenJets->end(); genIt++){
				double dR = DeltaR(cone_phi,cone_eta,genIt->phi(),genIt->eta());
				if(dR<2*dRcut) { match_gen = true; break; }
			}
			if(match_gen) {
				//rotate phi cone by dRcut*2 to get away from intruding genjet
				cone_phi += dRcut*2;
				cone_phi = phi( cos(cone_phi), sin(cone_phi) );
				++phi_ctr;
			}
		}
		
		if(!match_gen){
			//loop over ECAL rechits
			if(bEEr){
				for (EcalRecHitCollection::const_iterator hit = EERecHits->begin(); hit!=EERecHits->end(); ++hit) {
					EEDetId cell(hit->id());
					const CaloCellGeometry* cellGeometry = geometry->getSubdetectorGeometry(cell)->getGeometry(cell);
					double h_eta = cellGeometry->getPosition().eta();
					double h_phi = cellGeometry->getPosition().phi();
					double dR = DeltaR(cone_phi,cone_eta,h_phi,h_eta);
					if(dR < dRcut) { e_ecal_noise_en += hit->energy(); e_ecal_noise_pt += hit->energy()/TMath::CosH(h_eta); }
				}
			}
			if(bEBr){
				for (EcalRecHitCollection::const_iterator hit = EBRecHits->begin(); hit!=EBRecHits->end(); ++hit) {
					EBDetId cell(hit->id());
					const CaloCellGeometry* cellGeometry = geometry->getSubdetectorGeometry(cell)->getGeometry(cell);
					double h_eta = cellGeometry->getPosition().eta();
					double h_phi = cellGeometry->getPosition().phi();
					double dR = DeltaR(cone_phi,cone_eta,h_phi,h_eta);
					if(dR < dRcut) { e_ecal_noise_en += hit->energy(); e_ecal_noise_pt += hit->energy()/TMath::CosH(h_eta); }
				}
			}
			
			//loop over HCAL rechits
			if(bHBHEr){
				for (HBHERecHitCollection::const_iterator hit = HBHERecHits->begin(); hit!=HBHERecHits->end(); ++hit) {
					HcalDetId cell(hit->id());
					const CaloCellGeometry* cellGeometry = geometry->getSubdetectorGeometry(cell)->getGeometry(cell);
					double h_eta = cellGeometry->getPosition().eta();
					double h_phi = cellGeometry->getPosition().phi();
					double dR = DeltaR(cone_phi,cone_eta,h_phi,h_eta);
					if(dR < dRcut) { e_hcal_noise_en += hit->energy(); e_hcal_noise_pt += hit->energy()/TMath::CosH(h_eta); }
				}
			}
			if(bHFr){
				for (HFRecHitCollection::const_iterator hit = HFRecHits->begin(); hit!=HFRecHits->end(); ++hit) {
					HcalDetId cell(hit->id());
					const CaloCellGeometry* cellGeometry = geometry->getSubdetectorGeometry(cell)->getGeometry(cell);
					double h_eta = cellGeometry->getPosition().eta();
					double h_phi = cellGeometry->getPosition().phi();
					double dR = DeltaR(cone_phi,cone_eta,h_phi,h_eta);
					if(dR < dRcut) { e_hcal_noise_en += hit->energy(); e_hcal_noise_pt += hit->energy()/TMath::CosH(h_eta); }
				}
			}

			//sum up all rechits
			e_rec_noise_en = e_ecal_noise_en + e_hcal_noise_en;
			e_rec_noise_pt = e_ecal_noise_pt + e_hcal_noise_pt;

			//loop over calotowers
			if(bCT){
				for (CaloTowerCollection::const_iterator hit = CaloTowers->begin(); hit!=CaloTowers->end(); ++hit) {
					//collect noise from cone around negative eta, phi
					double dR = DeltaR(cone_phi,cone_eta,hit->phi(),hit->eta());
					if(dR < dRcut) { e_calo_noise_en += hit->energy(); e_calo_noise_pt += hit->pt(); }
				}
			}
			
			//loop over pfcands
			if(bPC){
				for (PFCandidateCollection::const_iterator pit = pfCandidates->begin(); pit!=pfCandidates->end(); ++pit) {
					//collect noise from cone around negative eta, phi
					double dR = DeltaR(cone_phi,cone_eta,pit->phi(),pit->eta());
					if(dR < dRcut) { e_pf_noise_en += pit->energy(); e_pf_noise_pt += pit->pt(); }
				}
			}
			
			tree_tot->Fill();
		}
		else {
			if(debug) cout << "Couldn't find good noise cone for GenJet with eta = " << e_gen_eta << ", phi = " << e_gen_phi << endl;
		}
	}
	
}

// ------------ method called once each job just before starting event loop  ------------
void 
FullSimNoiseAnalyzer::beginJob() {
	out_file = new TFile(outname.c_str(), "RECREATE");
	tree_tot = new TTree("Total", "Energy Calorimeter info");
	tree_tot->Branch("GenJetEta",&e_gen_eta,"e_gen_eta/D");
	tree_tot->Branch("GenJetPhi",&e_gen_phi,"e_gen_phi/D");
	tree_tot->Branch("EcalNoiseEnergy",&e_ecal_noise_en,"e_ecal_noise_en/D");
	tree_tot->Branch("EcalNoisePt",&e_ecal_noise_pt,"e_ecal_noise_pt/D");
	tree_tot->Branch("HcalNoiseEnergy",&e_hcal_noise_en,"e_hcal_noise_en/D");
	tree_tot->Branch("HcalNoisePt",&e_hcal_noise_pt,"e_hcal_noise_pt/D");
	tree_tot->Branch("RecHitNoiseEnergy",&e_rec_noise_en,"e_rec_noise_en/D");
	tree_tot->Branch("RecHitNoisePt",&e_rec_noise_pt,"e_rec_noise_pt/D");
	tree_tot->Branch("CaloNoiseEnergy",&e_calo_noise_en,"e_calo_noise_en/D");
	tree_tot->Branch("CaloNoisePt",&e_calo_noise_pt,"e_calo_noise_pt/D");
	tree_tot->Branch("PFNoiseEnergy",&e_pf_noise_en,"e_pf_noise_en/D");
	tree_tot->Branch("PFNoisePt",&e_pf_noise_pt,"e_pf_noise_pt/D");
}

// ------------ method called once each job just after ending the event loop  ------------
void 
FullSimNoiseAnalyzer::endJob() {
	out_file->cd();
	
	tree_tot->Write();
	
	out_file->Close();
}

// ------------ method called when starting to processes a run  ------------
void 
FullSimNoiseAnalyzer::beginRun(edm::Run const&, edm::EventSetup const&) {
}

// ------------ method called when ending the processing of a run  ------------
void 
FullSimNoiseAnalyzer::endRun(edm::Run const&, edm::EventSetup const&) { 
}

// ------------ method called when starting to processes a luminosity block  ------------
void 
FullSimNoiseAnalyzer::beginLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&) { }

// ------------ method called when ending the processing of a luminosity block  ------------
void 
FullSimNoiseAnalyzer::endLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&) { }

// ------------ method fills 'descriptions' with the allowed parameters for the module  ------------
void
FullSimNoiseAnalyzer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  //The following says we do not know what parameters are allowed so do no validation
  // Please change this to state exactly what you do use, even if it is no parameters
  edm::ParameterSetDescription desc;
  desc.setUnknown();
  descriptions.addDefault(desc);
}



