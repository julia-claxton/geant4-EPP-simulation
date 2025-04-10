//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
// $Id: SteppingAction.cc 74483 2013-10-09 13:37:06Z gcosmo $
//
/// \file SteppingAction.cc
/// \brief Implementation of the SteppingAction class

#include "SteppingAction.hh"
#include "EventAction.hh"
#include "DetectorConstruction.hh"
// #include "DetectorAnalysis.hh"
#include "G4Step.hh"
#include "G4Track.hh"
#include "G4Event.hh"
#include "G4RunManager.hh"
#include "RunAction.hh"
#include "G4LogicalVolume.hh"
#include "G4SystemOfUnits.hh"
#include "SteppingActionMessenger.hh"
#include "G4AutoLock.hh"

// Initialize autolock for multiple threads writing into a single file
namespace{ G4Mutex aMutex=G4MUTEX_INITIALIZER; } 

SteppingAction::SteppingAction(EventAction* eventAction, RunAction* RuAct)
: G4UserSteppingAction(),
  fEventAction(eventAction),
  fRunAction(RuAct),
  //fEnergyThreshold_keV(0.),
  fBackscatterFilename(),
  fSteppingMessenger()
{
  fSteppingMessenger = new SteppingActionMessenger(this);
}

SteppingAction::~SteppingAction(){delete fSteppingMessenger;}

void SteppingAction::UserSteppingAction(const G4Step* step)
{
  // ===========================
  // Guard Block
  // ===========================
  G4Track* track = step->GetTrack();

  // Check for NaN energy
  if( std::isnan(step->GetPostStepPoint()->GetKineticEnergy()) )
  {  
    track->SetTrackStatus(fStopAndKill);
    G4cout << "Particle killed for negative energy." << G4endl;
    G4cout << "Particle killed at: " << step->GetPreStepPoint()->GetKineticEnergy()/keV << " keV , Process: " << step->GetPostStepPoint()->GetProcessDefinedStep()->GetProcessName() << G4endl;
  }

  // Check for exceeding 1 second of simulation time
  G4double time = track->GetProperTime();
  if(time/second > 1)
  {
    track->SetTrackStatus(fStopAndKill);
    G4cout << "Particle killed for time > 1s." << G4endl;
    G4cout << "Particle killed at: " << step->GetPreStepPoint()->GetKineticEnergy()/keV << " keV , Process: " << step->GetPostStepPoint()->GetProcessDefinedStep()->GetProcessName() << G4endl;
  }

  // ===========================
  // Data Logging
  // ===========================
  // Dividing by a unit outputs data in that unit, so divisions by keV result in outputs in keV
  // https://geant4-internal.web.cern.ch/sites/default/files/geant4/collaboration/working_groups/electromagnetic/gallery/units/SystemOfUnits.html
  const G4String      particleName      = track->GetDynamicParticle()->GetDefinition()->GetParticleName();
  const G4ThreeVector position          = track->GetPosition();
  const G4ThreeVector momentumDirection = track->GetMomentumDirection();

  // ===========================
  // Energy Deposition Tracking
  // ===========================
  // Add energy deposition to vector owned by RunAction, which is written to a results file at the end of each thread's simulation run  
  G4double zPos = position.z(); // Particle altitude in world coordinates
  G4int altitudeAddress = std::floor(500.0 + zPos/km); // Index to write to. Equal to altitude above sea level in km, to nearest whole km
  
  if(altitudeAddress > 0 && altitudeAddress < 1000) 
  {
    const G4double energyDeposition = step->GetPreStepPoint()->GetKineticEnergy() - step->GetPostStepPoint()->GetKineticEnergy();
    LogEnergy(altitudeAddress, energyDeposition/keV); // Threadlocking occurs inside LogEnergy
  }

  // ===========================
  // Backscatter Tracking
  // =========================== 
  // Write backscatter directly to file if detected       
  if( (position.z()/km > 450.0-500.0) && (momentumDirection.z() > 0) ) // If particle is above 450 km and moving upwards. Subtract 500 because 0.0 in world coordinates = +500 km above sea level
  {
    // Lock scope to stop threads from overwriting data in same file
    G4AutoLock lock(&aMutex);

    // Get particle energy
    const G4double preStepEnergy =  step->GetPreStepPoint()->GetKineticEnergy();

    // Write position and directional kinetic energy to file
    std::ofstream dataFile;
    dataFile.open(fBackscatterFilename, std::ios_base::app); // Open file in append mode
    dataFile 
      << particleName << ','
      << position.x()/m << ',' 
      << position.y()/m << ','
      << (position.z()/m) + 500000 << ',' // Shift so we are writing altitude above sea level to file rather than the world coordinates
      << momentumDirection.x() * preStepEnergy/keV << ','
      << momentumDirection.y() * preStepEnergy/keV << ','
      << momentumDirection.z() * preStepEnergy/keV << '\n'; 
    dataFile.close();

    // Kill particle after data collection
    track->SetTrackStatus(fStopAndKill);
    G4cout << "Recorded and killed upgoing " << particleName << " at 450 km" << G4endl; // Status message
  }
}

void SteppingAction::LogEnergy(G4int histogramAddress, G4double energy)
{
  // This is in a different function so the threadlock isn't in scope for all of every step
  G4AutoLock lock(&aMutex);
  fRunAction->fEnergyDepositionHistogram->AddCountToBin(histogramAddress, energy/keV);
}