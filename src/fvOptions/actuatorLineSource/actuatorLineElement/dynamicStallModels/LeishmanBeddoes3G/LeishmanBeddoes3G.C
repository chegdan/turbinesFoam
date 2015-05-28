/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2013 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "LeishmanBeddoes3G.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{
    defineTypeNameAndDebug(LeishmanBeddoes3G, 0);
    addToRunTimeSelectionTable
    (
        dynamicStallModel, 
        LeishmanBeddoes3G,
        dictionary
    );
}
}


// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void Foam::fv::LeishmanBeddoes3G::calcAlphaEquiv()
{
    scalar beta = 1 - M_*M_;
    X_ = XPrev_*exp(-b1_*beta*deltaS_) 
       + A1_*deltaAlpha_*exp(b1_*beta*deltaS_/2);
    Y_ = YPrev_*exp(-b2_*beta*deltaS_) 
       + A2_*deltaAlpha_*exp(b2_*beta*deltaS_/2);
    alphaEquiv_ = alpha_ - X_ - Y_;
}


void Foam::fv::LeishmanBeddoes3G::evalStaticData
(
    List<scalar> alphaDegList,
    List<scalar> clList,
    List<scalar> cdList
)
{
    // Create lists for normal and chordwise coefficients
    scalar pi = Foam::constant::mathematical::pi;
    List<scalar> alphaRadList(alphaDegList.size());
    List<scalar> cnList(clList.size());
    List<scalar> ctList(cdList.size());
    
    forAll(alphaDegList, i)
    {
        alphaRadList[i] = alphaDegList[i]/180*pi;
        cnList[i] = clList[i]*cos(alphaRadList[i]) 
                  - cdList[i]*sin(alphaRadList[i]);
        ctList[i] = clList[i]*sin(alphaRadList[i])
                  - cdList[i]*cos(alphaRadList[i]);
    }
    
    // Calculate lift slope CNAlpha
    scalar alphaLow = 0.0;
    scalar alphaHigh = 2.0;
    scalar cnLow = interpolate(alphaLow, alphaDegList, cnList);
    scalar cnHigh = interpolate(alphaHigh, alphaDegList, cnList);
    scalar dAlpha = (alphaHigh - alphaLow)/180.0*pi;
    CNAlpha_ = (cnHigh - cnLow)/dAlpha;
    
    // Calculate critical normal force coefficient CN1, where the slope of the
    // curve slope first breaks 0.02 per degree
    scalar alpha=GREAT, cd0, cd1, slope;
    forAll(alphaDegList, i)
    {
        alpha = alphaDegList[i];
        if (alpha > 2 && alpha < 30)
        {
            cd1 = interpolate(alpha + 1.0, alphaDegList, cdList);
            cd0 = interpolate(alpha, alphaDegList, cdList);
            dAlpha = 1.0;
            slope = (cd1 - cd0)/dAlpha;
            if (slope > 0.02)
            {
                alphaSS_ = alpha/180.0*pi;
                break;
            }
        }
    }
    
    // Calculate CN1 using normal coefficient slope and critical f value
    scalar f = 0.7;
    alpha1_ = alphaSS_;
    CN1_ = CNAlpha_*alpha1_*pow((1 + sqrt(f))/2.0, 2);
    
    if (debug)
    {
        Info<< endl << "Evaluating static foil data" << endl;
        scalar cn = CNAlpha_*alpha_;
        Info<< "    Static stall angle (deg): " << alpha << endl;
        Info<< "    Critical normal force coefficient: " << CN1_ << endl;
        Info<< "    Normal coefficient slope: " << CNAlpha_ << endl;
        Info<< "    Normal coefficient from slope: " << cn << endl;
    }

    // Calculate CD0
    CD0_ = interpolate(0, alphaDegList, cdList);
    
    if (debug)
    {
        Info<< "    Cd_0: " << CD0_ << endl;
        Info<< "    alpha1: " << alpha1_ << endl;
    }
}


void Foam::fv::LeishmanBeddoes3G::calcUnsteady()
{
    // Calculate the circulatory normal force coefficient
    CNC_ = CNAlpha_*alphaEquiv_;
    
    // Calculate the impulsive normal force coefficient
    scalar pi = Foam::constant::mathematical::pi;
    scalar kAlpha = 0.75/(1 - M_ + pi*(1 - M_*M_)*M_*M_*(A1_*b1_ + A2_*b2_));
    TI_ = c_/a_;
    D_ = DPrev_*exp(-deltaT_/(kAlpha*TI_)) 
       - ((deltaAlpha_ - deltaAlphaPrev_)/deltaT_)
       *exp(-deltaT_/(2*kAlpha*TI_));
    CNI_ = 4*kAlpha*TI_/M_*(deltaAlpha_/deltaT_ - D_);
    
    // Calculate total normal force coefficient
    CNP_ = CNC_ + CNI_;
    
    // Apply first-order lag to normal force coefficient
    DP_ = DPPrev_*exp(-deltaS_/Tp_) + (CNP_ - CNPPrev_)*exp(-deltaS_/(2*Tp_));
    CNPrime_ = CNP_ - DP_;
    
    // Calculate lagged angle of attack
    alphaPrime_ = CNPrime_/CNAlpha_;
    
    // Set stalled switch
    stalled_ = (mag(CNPrime_) > CN1_);
}


void Foam::fv::LeishmanBeddoes3G::calcS1S2
(
    List<scalar> alphaDegList,
    List<scalar> clList,
    List<scalar> cdList
)
{
    scalar pi = Foam::constant::mathematical::pi;
    scalar sumY = 0.0;
    scalar sumXYLnY = 0.0;
    scalar sumXY = 0.0;
    scalar sumYLnY = 0.0;
    scalar sumX2Y = 0.0;
    scalar alphaLowerLimit;
    scalar alphaUpperLimit;
    if (mag(alphaPrime_) < alpha1_)
    {
        alphaLowerLimit = 0.0;
        alphaUpperLimit = alpha1_;
    }
    else
    {
        alphaLowerLimit = alpha1_ - 1e-3;
        alphaUpperLimit = pi/6;
    }
    forAll(alphaDegList, i)
    {
        scalar alphaRad = alphaDegList[i]/180*pi;
        scalar cn = clList[i]*cos(alphaRad) - cdList[i]*sin(alphaRad);
        scalar f = 1;
        if (alphaRad > alphaLowerLimit and alphaRad < alphaUpperLimit)
        {
            f = pow((sqrt(mag(cn)/CNAlpha_/mag(alphaRad))
                    *2 - 1), 2);
            scalar x;
            scalar y;
            if (mag(alphaPrime_) < alpha1_) 
            {
                x = mag(alphaRad) - alpha1_;
                y = (f - 1)/(-0.4);
            }
            else 
            {
                x = alpha1_ - mag(alphaRad);
                y = (f - 0.02)/0.58;
            }
            if (f > 0 and f < 1 and y > 0)
            {
                sumY += y;
                sumXYLnY += x*y*log(y);
                sumXY += x*y;
                sumYLnY += y*log(y);
                sumX2Y += x*x*y;
            }
        }
    }
    scalar b = (sumY*sumXYLnY - sumXY*sumYLnY)/(sumY*sumX2Y - sumXY*sumXY);
    if (mag(alphaPrime_) < alpha1_)
    {
        S1_ = 1/b;
        S2_ = 0.0;
    }
    else
    {
        S1_ = 0.0;
        S2_ = 1/b;
    }
    
    if (debug)
    {
        Info<< "    S1: " << S1_ << endl;
        Info<< "    S2: " << S2_ << endl;
    }
}


void Foam::fv::LeishmanBeddoes3G::calcSeparated()
{
    // Calculate trailing-edge separation point
    if (mag(alphaPrime_) < alpha1_)
    {
        fPrime_ = 1.0 - 0.3*exp((mag(alphaPrime_) - alpha1_)/S1_);
    }
    else
    {
        fPrime_ = 0.04 + 0.66*exp((alpha1_ - mag(alphaPrime_))/S2_);
    }
    
    // Modify Tf time constant if necessary
    scalar Tf = Tf_;
    if (tau_ > 0 and tau_ <= Tvl_) Tf = 0.5*Tf_;
    else if (tau_ > Tvl_ and tau_ <= 2*Tvl_) Tf = 4*Tf_;
    if (mag(alpha_) < mag(alphaPrev_) and mag(CNPrime_) < CN1_)
    {
        Tf = 0.5*Tf_;
    }
    
    // Calculate dynamic separation point
    DF_ = DFPrev_*exp(-deltaS_/Tf) 
        + (fPrime_ - fPrimePrev_)*exp(-deltaS_/(2*Tf));
    fDoublePrime_ = mag(fPrime_ - DF_);
    
    // Calculate normal force coefficient including dynamic separation point
    CNF_ = CNAlpha_*alphaEquiv_*pow(((1 + sqrt(fDoublePrime_))/2), 2) + CNI_;
    
    // Calculate tangential force coefficient
    if (fDoublePrime_ < 0.7)
    {
        CT_ = eta_*CNAlpha_*alphaEquiv_*alphaEquiv_*sqrt(fDoublePrime_);
    }
    else
    {
        CT_ = eta_*CNAlpha_*alphaEquiv_*alphaEquiv_*pow(fDoublePrime_, 1.5);
    }
    
    // Compute vortex shedding process if stalled
    // Evaluate vortex tracking time
    if (not stalledPrev_) tau_ = 0.0;
    else 
    {
        if (tau_ == tauPrev_)
        {
            tau_ = tauPrev_ + deltaS_;
        }
    }
    
    // Calculate Strouhal number time constant and set tau to zero to 
    // allow multiple vortex shedding
    scalar Tst = 2*(1 - fDoublePrime_)/0.19;
    if (tau_ > (Tvl_ + Tst)) tau_ = 0.0;
    
    // Evaluate vortex lift contributions, which are only nonzero if angle
    // of attack increased in magnitude
    if (mag(alpha_) > mag(alphaPrev_) and mag(alpha_ - alphaPrev_) > 0.01)
    {
        scalar Tv = Tv_;
        if (tau_ < Tvl_)
        {
            // Halve Tv if dAlpha/dt changes sign
            if (sign(deltaAlpha_) != sign(deltaAlphaPrev_)) Tv = 0.5*Tv_;
            CV_ = CNC_*(1 - pow(((1 + sqrt(fDoublePrime_))/2), 2));
            CNV_ = CNVPrev_*exp(-deltaS_/Tv) 
                 + (CV_ - CVPrev_)*exp(-deltaS_/(2*Tv));
        }
        else
        {
            Tv = 0.5*Tv_;
            CNV_ = CNVPrev_*exp(-deltaS_/Tv);
        }
    }
    else
    {
        CNV_ = 0.0;
    }

    // Total normal force coefficient is the combination of that from
    // circulatory effects, impulsive effects, dynamic separation, and vortex 
    // lift
    CN_ = CNF_ + CNV_;
}


void Foam::fv::LeishmanBeddoes3G::update()
{
    LeishmanBeddoes::update();
    ZPrev_ = Z_;
    etaLPrev_ = etaL_;
    HPrev_ = H_;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fv::LeishmanBeddoes3G::LeishmanBeddoes3G
(
    const dictionary& dict,
    const word& modelName,
    const Time& time
)
:
    LeishmanBeddoes(dict, modelName, time),
    Z_(0.0),
    etaL_(0.0),
    H_(0.0)
{
    fCrit_ = 0.6;
    
    if (debug)
    {
        Info<< modelName << " dynamic stall model created" << endl
            << "    Coeffs:" << endl << coeffs_ << endl;
    }
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::fv::LeishmanBeddoes3G::~LeishmanBeddoes3G()
{}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

void Foam::fv::LeishmanBeddoes3G::correct
(
    scalar magU,
    scalar alphaDeg,
    scalar& cl,
    scalar& cd,
    List<scalar> alphaDegList,
    List<scalar> clList,
    List<scalar> cdList
)
{
    scalar pi = Foam::constant::mathematical::pi;
    scalar time = time_.value();
    deltaT_ = time_.deltaT().value();
    
    // Update previous values if time has changed
    if (time != timePrev_)
    {
        nNewTimes_++;
        if (nNewTimes_ > 1) 
        {
            update();
        }
    }
    
    if (nNewTimes_ <= 1)
    {
        alpha_ = alphaDeg/180*pi;
        alphaPrev_ = alpha_;
    }
    
    alpha_ = alphaDeg/180*pi;
    M_ = magU/a_;
    deltaAlpha_ = alpha_ - alphaPrev_;
    deltaS_ = 2*magU*deltaT_/c_;
    
    if (debug)
    {
        scalar cn0 = cl*cos(alpha_) - cd*sin(alpha_);
        Info<< "Leishman-Beddoes dynamic stall model correcting" << endl;
        Info<< "    New times: " << nNewTimes_ << endl;
        Info<< "    Time: " << time << endl;
        Info<< "    deltaT: " << deltaT_ << endl;
        Info<< "    deltaS: " << deltaS_ << endl;
        Info<< "    Angle of attack (deg): " << alphaDeg << endl;
        Info<< "    deltaAlpha: " << deltaAlpha_ << endl;
        Info<< "    Initial normal force coefficient: " << cn0 << endl;
        Info<< "    Initial lift coefficient: " << cl << endl;
        Info<< "    Initial drag coefficient: " << cd << endl;
    }
    
    calcAlphaEquiv();
    evalStaticData(alphaDegList, clList, cdList);
    calcUnsteady();
    calcS1S2(alphaDegList, clList, cdList);
    calcSeparated();
    
    // Modify lift and drag coefficients based on new normal force coefficient
    cl = CN_*cos(alpha_) + CT_*sin(alpha_);
    cd = CN_*sin(alpha_) - CT_*cos(alpha_) + CD0_;
    
    if (debug)
    {
        scalar alphE = alphaEquiv_/pi*180.0;
        Info<< "    Stalled: " << stalled_ << endl;
        Info<< "    tau: " << tau_ << endl;
        Info<< "    Equivalent angle of attack: " << alphE << endl;
        Info<< "    alphaPrime: " << alphaPrime_ << endl;
        Info<< "    fPrime: " << fPrime_ << endl;
        Info<< "    fDoublePrime: " << fDoublePrime_ << endl;
        Info<< "    Corrected normal force coefficient: " << CN_ << endl;
        Info<< "    Circulatory normal force coefficient: " << CNC_ << endl;
        Info<< "    Impulsive normal force coefficient: " << CNI_ << endl;
        Info<< "    Tangential force coefficient: " << CT_ << endl;
        Info<< "    Corrected lift coefficient: " << cl << endl;
        Info<< "    Corrected drag coefficient: " << cd << endl << endl;
    }
}

// ************************************************************************* //