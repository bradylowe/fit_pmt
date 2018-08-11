// Brady Lowe // lowebra2@isu.edu

#include <TFile.h>
#include <TDirectory.h>
#include <TTree.h>
#include <TROOT.h>
#include <TMath.h>
#include <TChain.h>
#include <TH1F.h>
#include <TF1.h>
#include <TTimeStamp.h>

#include "Math/SpecFunc.h"
#include "dataAnalyzer.c"
#include <mysql/mysql.h>

#include <TMinuit.h>
#include <TApplication.h>
#include <TCanvas.h>
#include <TStyle.h>
#include <TAxis.h>
#include <TLine.h>


// modified version due to clock trigger (pedestal injection)
Double_t the_real_deal_yx(Double_t *x, Double_t *par); //signal+background 
Double_t the_real_deal_yx_pe(Double_t *x, Double_t *par);  //individual PE contributions
Double_t the_real_deal_yx_bg(Double_t *x, Double_t *par); // background
// Define global variables to be used in fitting
Int_t MIN_PE = 1;
Int_t MAX_PE = 20;
Int_t NPE = MAX_PE - MIN_PE + 1;

// define constants for use with the above functions
static const double degtorad = 3.141592653589793 / 180.;
static const double twopi = 2 * 3.141592653589793;

// FIT A DATA RUN TO FIND THE CONTRIBUTING INDIVIDUAL 
// PHOTO-ELECTRON (PE) DISTRIBUTION [FOR LOW LIGHT LEVEL
// PMT DATA]
//
// Take in a large number of extra parameters to have an algorithm that
// is callable and fully capable, and yet able to remain hard-coded for
// a long period of time.
int fit_pmt(
	// The first 3 parameters must be passed in
	string rootFile, 	// File name (something.root)
	Int_t runID,		// Unique identifier
	Int_t runNum, 		// Run number
	Int_t daq, 		// Which data acquisition system (3, 4, 5)
	// These params can be read in from the .info file
	Int_t chan, 		// Which ADC channel (0 - 15)
	Int_t amp, 		// How much post-amplification (1x - ~10x)
	Int_t dataRate, 	// Frequency of incoming real data events
	Int_t pedRate, 		// Frequency of incoming injected pedestal events
	Int_t hv, 		// High voltage used
	Int_t ll, 		// Light level used (light level 4,60 becomes 46)
	Int_t filter, 		// Filter used (0 - 8)(0 is closed shutter)
	// These params can be used to force the function onto solutions
	// These params are some functionality flags
	Bool_t saveResults, 	// Save output png with stats
	Bool_t saveNN, 		// Save neural network output png and txt
	Int_t fitEngine, 	// Switch for choosing minimizing technique
	// The next 2 parameters are the min and max bin to consider in fit
	Int_t low, 		// Lowest bin to consider in fit
	Int_t high, 		// Highest bin to consider in fit
	const int minPE, 	// Lowest #PEs to consider in fit
	const int maxPE, 	// Highest #PEs to consider in fit
	//^^ Add const minPe, maxPe
	// The next 9 parameters are initial conditions.
	Double_t w0, 		Double_t ped0,		Double_t pedrms0, 
	Double_t alpha0,	Double_t mu0,		Double_t sig0, 
	Double_t sigrms0,	Double_t inj0,		Double_t real0,
	// Below are the lower bounds to all 9 parameters
	Double_t wmin, 		Double_t pedmin,	Double_t pedrmsmin, 
	Double_t alphamin, 	Double_t mumin,		Double_t sigmin, 
	Double_t sigrmsmin, 	Double_t injmin,	Double_t realmin,
	// Below are the upper bounds to all 9 parameters
	Double_t wmax, 		Double_t pedmax,	Double_t pedrmsmax, 
	Double_t alphamax, 	Double_t mumax,		Double_t sigmax, 
	Double_t sigrmsmax, 	Double_t injmax,	Double_t realmax
){

	printf("\nEntering fit_pmt.c\n");

	// CHECK CONST PE SETUP
	const int nPE = maxPE - minPE + 1;
	if ( minPE < 1 || nPE < 0 ) {
		printf("ERROR\n");
		return -1;
	} else {
		// Update global value for external loops
		MIN_PE = minPE;
		MAX_PE = maxPE;
		NPE = nPE;
	}

	printf("MIN_PE, MAX_PE, NPE  = %d, %d, %d\n", MIN_PE, MAX_PE, NPE);

/* Doesnt work
	// Set up database for fetching and saving values
	MYSQL_RES *result;
	MYSQL_ROW row;
	MYSQL *connection, mysql;

	const char* server="127.0.0.1";
	const char* user = "brady";
	const char* password = "thesis";
	const char* database = "gaindb";

	int state;

	mysql_init(&mysql);
	connection = mysql_real_connect(&mysql,server,user,password,database,0,0,0);
	
	if (connection == NULL) {
		printf("Null connection mysql\n");
		return 1;
	} 
	state = mysql_query(connection, "SELECT * FROM exp_params");
	if (state != 0) {
		printf("error mysql\n");
		return 1;
	}

	result = mysql_store_result(connection);
	row = mysql_fetch_row(result);
	
	mysql_free_result(result);
	mysql_close(connection);
*/

	// Pack parameters into nice arrays
	Double_t initial[9] = {w0, ped0, pedrms0, alpha0, mu0, sig0, sigrms0, inj0, real0};
	Double_t min[9] = {wmin, pedmin, pedrmsmin, alphamin, mumin, sigmin, sigrmsmin, injmin, realmin};
	Double_t max[9] = {wmax, pedmax, pedrmsmax, alphamax, mumax, sigmax, sigrmsmax, injmax, realmax};

        // OPEN DATA FILE
        printf("Opening file %s\n", rootFile.c_str());
        TFile* f1 = new TFile(rootFile.c_str());
        if ( f1->IsZombie() ) {
                printf("ERROR ==> Couldn't open file %s\n", rootFile.c_str());
                return -1;
        }

	// Define info file for use later 
	string infoFile = rootFile.substr(0, rootFile.length() - 5);
	infoFile.append(".info");

        // EXTRACT DATA FROM FILE
        TTree* t1 = (TTree*) f1->Get("ntuple");
        if (t1 == NULL) {
                printf("ERROR ==> Couldn't find \"ntuple\"\n");
                return -1;
        }

        // Get number of events
        Int_t nentries = (Int_t) t1->GetEntries();
        printf("Number of entries:  %d\n", nentries);

	// Define which channel and what data to grab
        char chanStr[32];
        char selection[64];
        sprintf(chanStr, "ADC%dl", chan);
        sprintf(selection, "%s>2&&%s<4096", chanStr, chanStr);

        // CREATE LEAF AND BRANCH
        TLeaf* l1 = t1->GetLeaf(chanStr);
        TBranch* b1 = l1->GetBranch();

        // INITIALIZE HISTOGRAM
        Int_t binWidth = 1;
        Int_t maxbins = 4096;
        Int_t bins = maxbins / binWidth + 1;
        Float_t minR = -0.5 * (Float_t) binWidth;
        Float_t maxR = maxbins + 0.5 * (Float_t) binWidth;
        TH1F *h_QDC = new TH1F("h_QDC", "QDC spectrum", bins, minR, maxR);

	// SETUP X,Y TITLES OF GRAPH AND COLOR
        char xTitle[64];
        char yTitle[64];
        char Title[64];
	// x title
        sprintf(xTitle, "ADC channels");
        h_QDC->GetXaxis()->SetTitle(xTitle);
        // y title
	if (binWidth > 1) sprintf(yTitle, "Events/%dchs", binWidth);
        else sprintf(yTitle, "Relative amplitude");
        h_QDC->GetYaxis()->SetTitle(yTitle);
        // main title
	sprintf(Title, "Low-light PE fit of r%d", runNum);
        h_QDC->SetTitle(Title);
        // fit color?
	h_QDC->SetLineColor(1);

        // FILL HISTOGRAM
        for (Int_t entry = 0; entry < b1->GetEntries(); entry++) {
                b1->GetEntry(entry);
                h_QDC->Fill(l1->GetValue());
        }

        // NORMALIZE HISTOGRAM
        Int_t sum = h_QDC->GetSum();
        for (Int_t curBin = 0; curBin < bins; curBin++) {
		Float_t curVal = h_QDC->GetBinContent(curBin); 
                h_QDC->SetBinContent(curBin, curVal / sum);
                h_QDC->SetBinError(curBin, sqrt(curVal) / sum);
        }

	///////////////////////////////
	// define fitting function
	/////////////////////////////
	TF1 *fit_func=new TF1("fit_func",the_real_deal_yx,0,4093,9); 
	// 9 parameters
	fit_func->SetLineColor(4);
	fit_func->SetNpx(2000);
	fit_func->SetLineWidth(2);
	fit_func->SetParName(0,"W");
	fit_func->SetParName(1,"Q0");
	fit_func->SetParName(2,"S0");
	fit_func->SetParName(3,"alpha");
	fit_func->SetParName(4,"mu");
	fit_func->SetParName(5,"Q1");
	fit_func->SetParName(6,"S1");
	fit_func->SetParName(7,"inj");
	fit_func->SetParName(8,"real");

	// Set initial parameters
	fit_func->SetParameters(initial);

	// Constrain the parameters that need to be constrained
	for (int i = 0; i < 9; i++) {
		// Check if there is restraint on this param
		if (min[i] >= 0.0 && max[i] >= 0.0) {
			// Check if param needs to be fixed
			if (min[i] == max[i]) fit_func->FixParameter(i, initial[i]);
			// Otherwise, just bound the param
			else fit_func->SetParLimits(i, min[i], max[i]);
			printf("setparlimits: %d, %.3f, %.3f\n", i, min[i], max[i]);
		}
	}

	///////////////////////////////
	// set minimization engine
	///////////////////////////////
/*	double arglist[0]=2;
	int ierflg=0;
	TMinuit minuit(9);
	minuit.mnexcm("SET STR",arglist,1,ierflg);
*/	
	// Fit to pedestal
	TF1 *fit_gaus_ped = new TF1("fit_gaus_ped", "gaus", initial[1] - initial[2], initial[1] + initial[2]);
	h_QDC->Fit(fit_gaus_ped, "RON", "");
	
	// Initialize canvas
	TCanvas *can=new TCanvas("can","can");
	can->cd();
	gStyle->SetOptFit(1);
	h_QDC->SetMarkerSize(0.7);
	h_QDC->SetMarkerStyle(20);
	h_QDC->GetXaxis()->SetTitle("QDC channel");
	h_QDC->GetYaxis()->SetTitle("Normalized yield");
	h_QDC->Draw("AC");

	// PERFORM FIT, GET RESULTS
	h_QDC->GetXaxis()->SetRangeUser(low, high);
	if (fitEngine == 0) 
		// user range, return fit results, use improved fitter
		TFitResultPtr res = h_QDC->Fit(fit_func, "RSM", "", low, high);
	else if (fitEngine == 1)
		// Log likelihood, user range, return fit results, use improved fitter
		TFitResultPtr res = h_QDC->Fit(fit_func, "LRSM", "", low, high);
	else if (fitEngine == 2)
		// return fit results, use improved fitter
		TFitResultPtr res = h_QDC->Fit(fit_func, "SM", "");
	else if (fitEngine == 3)
		// Log likelihood, return fit results, use improved fitter
		TFitResultPtr res = h_QDC->Fit(fit_func, "LSM", "");
	else if (fitEngine == 4)
		// Better errors w/minos, return fit results, use improved fitter, user range
		TFitResultPtr res = h_QDC->Fit(fit_func, "ESMR", "");
	else if (fitEngine == 5)
		// Better errors w/minos, return fit results, use improved fitter, user range, loglike..
		TFitResultPtr res = h_QDC->Fit(fit_func, "ESMRL", "");
	double back[10];
	fit_func->GetParameters(back);

	// FIT GAUSSIAN TO ALL PE PEAKS IN CONSIDERATION
	TF1 *fis_from_fit_pe[nPE];
	char fitname[20];
	for ( int bb = minPE; bb < maxPE; bb++) {
		back[9]=bb + minPE - 1;
		fit_func->GetParameters(back);
		sprintf(fitname, "fis_from_fit_pe_%d", bb + minPE - 1);
        	fis_from_fit_pe[bb] = new TF1(fitname,the_real_deal_yx_pe,0,4093,10);
        	fis_from_fit_pe[bb]->SetParameters(back);
        	fis_from_fit_pe[bb]->SetLineStyle(2);
		fis_from_fit_pe[bb]->SetLineColor(2);
		fis_from_fit_pe[bb]->SetNpx(2000);
		fis_from_fit_pe[bb]->Draw("same");
	}

	// FIT BACKGROUND GAUSSIAN CONVOLUTED WITH EXPONENTIAL
	TF1 *fis_from_fit_bg = new TF1("fis_from_fit_bg",the_real_deal_yx_bg,0,4000,9);
	fis_from_fit_bg->SetParameters(back);
        fis_from_fit_bg->SetLineStyle(2);
        fis_from_fit_bg->SetLineColor(7);
        fis_from_fit_bg->SetNpx(2000);
	fis_from_fit_bg->Draw("same");

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////	DONE FITTING	//////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	printf("\nDone fitting. \n");

	// CREATE TIMESTAMP FOR UNIQUELY IDENTIFYING ALL OUTPUTS
	// OF THIS ITERATION OF FIT.
	TTimeStamp *ts = new TTimeStamp();
	char timestamp[16];
	sprintf(timestamp, "%d%d%d", ts->GetDate(), ts->GetTime(), (int)(ts->GetNanoSec() / 1000000));

	// Grab some stats info from the fit
	Double_t chi = fit_func->GetChisquare();
	Int_t ndf = fit_func->GetNDF();
	Int_t nfitpoints = fit_func->GetNumberFitPoints();

	// Get output values
	Double_t wout = back[0];
	Double_t pedout = back[1];
	Double_t pedrmsout = back[2];
	Double_t alphaout = back[3];
	Double_t muout = back[4];
	Double_t sigout = back[5];
	Double_t sigrmsout = back[6];
	Double_t injout = back[7];
	Double_t realout = back[8];
	Double_t wouterr = fit_func->GetParError(0);
	Double_t pedouterr = fit_func->GetParError(1);
	Double_t pedrmsouterr = fit_func->GetParError(2);
	Double_t alphaouterr = fit_func->GetParError(3);
	Double_t muouterr = fit_func->GetParError(4);
	Double_t sigouterr = fit_func->GetParError(5);
	Double_t sigrmsouterr = fit_func->GetParError(6);
	Double_t injouterr = fit_func->GetParError(7);
	Double_t realouterr = fit_func->GetParError(8);

	// CALCULATE PMT GAIN USING AMPLIFICATION SETTING AND ADC CONVERSION FACTOR
	Double_t gain = sigout * 25.0 / 160.2 / (double)(amp);
	Double_t gainError = sigouterr * 25.0 / 160.2 / (double)(amp);
	gainError = gainError * (double)(chi / ndf);

	// Set title of graph to display gain measurement
	sprintf(Title, "gain: (%.2f, %.3f, %.1f%%)", gain, gainError, gainError / gain * 100);
        h_QDC->SetTitle(Title);

	// DEFINE USER IMAGE FILE AND NN IMAGE FILE
	char humanImFile[256];
	char nnImFile[256];
	sprintf(humanImFile, "fit_pmt__run%d_chi%d_time%s.png", runNum, int(chi / ndf), timestamp);
	sprintf(nnImFile, "fit_pmt_nn__run%d_chi%d_time%s.png", runNum, int(chi / ndf), timestamp);
	//char nnImFile[256];
	//sprintf(nnImFile, "fit_pmt_nn__run%d_daq%d_chi%d_time%s.png", runNum, daq, int(chi) / ndf, timestamp);

	// IF SAVING HUMAN OUTPUT IMAGE ...
	if (saveResults) can->Print(humanImFile);

	// IF SAVING OUTPUT FOR INPUTTING INTO NN ...
	if (saveNN) {
		// OUTPUT IMAGE FOR NEURAL NETWORK TO USE
		// (BARE BONES)
		can->SetFrameFillColor(0);
		can->SetFrameFillStyle(0);
		can->SetFrameLineColor(0);
		can->SetFrameBorderMode(0);
		can->cd();
		gStyle->SetOptFit(0);
		gStyle->SetOptStat(0);
		h_QDC->GetYaxis()->SetLabelSize(0);
		h_QDC->GetYaxis()->SetTickLength(0);
		h_QDC->GetXaxis()->SetLabelSize(0);
		h_QDC->GetXaxis()->SetTickLength(0);
		h_QDC->SetTitle("");
		h_QDC->SetMarkerSize(0.7);
		h_QDC->SetMarkerStyle(20);
		can->Update();
	    	can->Print(nnImFile);
	}

	// Compute summary string for this instance of fit
	char summaryLine[1024];
        sprintf(summaryLine,
                "macro:fit_pmt time:%s run:%d daq:%d chan:%d amp:%d dataRate:%d pedRate:%d hv:%d ll:%d filter:%d fitEngine:%d low:%d high:%d minPE:%d maxPE:%d w0:%.4f ped0:%.1f pedrms0:%.1f alpha0:%.4f mu0:%.2f sig0:%.1f sigrms0:%.1f inj0:%.2f real0:%.2f wmin:%.4f pedmin:%.1f pedrmsmin:%.1f alphamin:%.4f mumin:%.2f sigmin:%.1f sigrmsmin:%.1f injmin:%.2f realmin:%.2f wmax:%.4f pedmax:%.1f pedrmsmax:%.1f alphamax:%.4f mumax:%.2f sigmax:%.1f sigrmsmax:%.1f injmax:%.2f realmax:%.2f wout:%.4f pedout:%.1f pedrmsout:%.1f alphaout:%.4f muout:%.2f sigout:%.1f sigrmsout:%.1f injout:%.2f realout:%.2f wouterr:%.4f pedouterr:%.1f pedrmsouterr:%.1f alphaouterr:%.4f muouterr:%.2f sigouterr:%.1f sigrmsouterr:%.1f injouterr:%.2f realouterr:%.2f chi:%.2f ndf:%d nfitpoints:%d gain:%.2f, gainerr:%.4f",
                timestamp, runNum, daq, chan, amp, dataRate, 
		pedRate, hv, ll, filter,  
		fitEngine, low, high, minPE, maxPE,
		w0, ped0, pedrms0, alpha0, mu0,
		sig0, sigrms0, inj0, real0,
		wmin, pedmin, pedrmsmin, alphamin, mumin,
		sigmin, sigrmsmin, injmin, realmin,
		wmax, pedmax, pedrmsmax, alphamax, mumax,
		sigmax, sigrmsmax, injmax, realmax,
		wout, pedout, pedrmsout, alphaout, muout,
		sigout, sigrmsout, injout, realout,
		wouterr, pedouterr, pedrmsouterr, alphaouterr, muouterr,
		sigouterr, sigrmsouterr, injouterr, realouterr,
		chi, ndf, nfitpoints, gain, gainError
        );
	// Save summary strings to .info file
	// Open file for appending
	ofstream file;
	file.open(infoFile.c_str(), std::ofstream::out | std::ofstream::app);
	// If file opens properly ...
	if (file.is_open()) {
		// Write line to file and close
		file << summaryLine << endl;
		file.close();
	} else printf("\nUnable to open .info file\n");

	// Create SQL query for storing all the output of this run in the gaindb
	char queryLine[1024];
	sprintf(queryLine, 
		"USE gaindb; INSERT INTO fit_results (run_id, fit_engine, fit_low, fit_high, min_pe, max_pe, w_0, ped_0, ped_rms_0, alpha_0, mu_0, sig_0, sig_rms_0, inj_0, real_0, w_min, ped_min, ped_rms_min, alpha_min, mu_min, sig_min, sig_rms_min, inj_min, real_min, w_max, ped_max, ped_rms_max, alpha_max, mu_max, sig_max, sig_rms_max, inj_max, real_max, w_out, ped_out, ped_rms_out, alpha_out, mu_out, sig_out, sig_rms_out, inj_out, real_out, w_out_error, ped_out_error, ped_rms_out_error, alpha_out_error, mu_out_error, sig_out_error, sig_rms_out_error, inj_out_error, real_out_error, chi, gain, gain_error) VALUES('%d', '%d', '%d', '%d', '%d', '%d', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f', '%.4f');",
		runID, fitEngine, low, high, minPE, maxPE,
		w0, ped0, pedrms0, alpha0, mu0, sig0, sigrms0, inj0, real0,
        	wmin, pedmin, pedrmsmin, alphamin, mumin, sigmin, sigrmsmin, injmin, realmin,
        	wmax, pedmax, pedrmsmax, alphamax, mumax, sigmax, sigrmsmax, injmax, realmax,
        	wout, pedout, pedrmsout, alphaout, muout, sigout, sigrmsout, injout, realout,
        	wouterr, pedouterr, pedrmsouterr, alphaouterr, muouterr, sigouterr, sigrmsouterr, injouterr, realouterr,
        	chi/double(ndf), gain, gainError
	);
	file.open("sql_output.txt", std::ofstream::out);
	if (file.is_open()) {
		file << queryLine << endl;
		file.close();
	} else printf("\nUnable to output the following to SQL file:\n%s\n", queryLine);


	// Return chi squared per number of degrees of freedom (floored)
	return (int)(chi / ndf);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////// sub functions ////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

Double_t the_real_deal_yx(Double_t *x, Double_t *par){

	// [0] = w
	// [1] = Q0, mean of PED
	// [2] = sigma_0, RMS of PED
	// [3] = alpha, decay constant of discrete background
	// [4] = mu, mean of the entire ADC distribution
	// [5] = q1, peak of SPE
	// [6] = sigma1, RMS of SPE
	// [8] =norm of 1-w term
	Double_t xx = x[0];
	Double_t s_real_sum = 0.;
	Double_t qn, sigma_n, term_1, term_11, term_2, term_3, igne, s_real, igne_is;

	// Loop through all PE peaks to consider
	for(Int_t i = MIN_PE; i < MAX_PE; i++){
		// Initialize terms to use in mathematics
		qn = 0.;
		sigma_n = 0.;
		term_1 = 0.;
		term_11 = 0.;
		term_2 = 0.;
		term_3 = 0.;
		igne = 0.;
		s_real = 0.;
		// Calculate values for this iteration
		qn = par[1] + i * par[5];				// Mean of this PE
		sigma_n = sqrt(pow(par[2], 2) + i * pow(par[6], 2));	// Sigma of this PE
		term_11 = xx - qn - par[3] * pow(sigma_n, 2) / 2;	// 
		term_1 = xx - qn - par[3] * pow(sigma_n, 2);		// 
		term_2 = par[1] - qn - par[3] * pow(sigma_n, 2);	// 
		term_3 = xx - 1 * par[1] - i * par[5];			// 
		if (term_1 >= 0.) 
			igne = par[3] / 2.0 * exp(-par[3] * term_11) * (TMath::Erf(fabs(term_2) / sqrt(2.0) / sigma_n) + TMath::Erf(fabs(term_1) / sqrt(2.0) / sigma_n));
		else 
			igne = par[3] / 2.0 * exp(-par[3] * term_11) * (TMath::Erf(fabs(term_2) / sqrt(2.0) / sigma_n) - TMath::Erf(fabs(term_1) / sqrt(2.0) / sigma_n));

		s_real = TMath::PoissonI(i, par[4]) * ((1 - par[0]) / sqrt(twopi) / sigma_n * exp(-pow(term_3, 2) / 2 / pow(sigma_n, 2)) + par[0] * igne) * par[8];
		// Sum up contribution from all PE's
		s_real_sum += s_real; 
	}
	// Calculate background portion
	Double_t poisson_is = exp(-par[4]);
	Double_t gaus_is = exp(-pow(xx - par[1], 2) / 2.0 / pow(par[2], 2)) / par[2] / sqrt(twopi);
	if(xx >= par[1])
		igne_is = par[3] * exp(-par[3] * (xx - par[1]));
	else 
		igne_is = 0.;
	Double_t s_real_sum_bg = poisson_is * ((1 - par[0]) * gaus_is + par[0] * igne_is) * par[8]; 
	// add in clock contribution
	double clock_contribution=par[7]*((1 - par[0]) * gaus_is + par[0] * igne_is);
	return s_real_sum + s_real_sum_bg + clock_contribution;
} 


Double_t the_real_deal_yx_pe(Double_t *x, Double_t *par){

  // [0] = w, weight of discrete background
  // [1] = Q0, mean of PED
  // [2] = sigma_0, RMS of PED
  // [3] = alpha, decay constant of discrete background
  // [4] = mu, mean of the entire ADC distribution
  // [5] = q1, peak of SPE
  // [6] = sigma1, RMS of SPE
  
  // [7] = Norm of clock contribution
  // [8] =norm of 1-w term
 
 //[9] P.E components
  Double_t xx = x[0];

  Double_t s_real_sum = 0.;

  Double_t qn, sigma_n, term_1, term_11, term_2, term_3, igne, s_real, igne_is;

  for(Int_t i = par[9]; i < par[9]+1; i++){

    qn = 0.;
    sigma_n = 0.;
    term_1 = 0.;
    term_11 = 0.;
    term_2 = 0.;
    term_3 = 0.;
    igne = 0.;
    s_real = 0.;

    qn = par[1] + i * par[5];
    sigma_n = sqrt(pow(par[2],2) + i * pow(par[6],2));
    term_1 = xx - qn - par[3] * pow(sigma_n,2);
    term_11 = xx - qn - par[3] * pow(sigma_n,2)/2.0;     
    term_2 = par[1] - qn - par[3] * pow(sigma_n,2);
    term_3 = xx - 1 * par[1] - i * par[5];
    
    if (term_1 >= 0.){
      
      igne = par[3]/2 * exp(-par[3] * term_11) * (TMath::Erf(fabs(term_2)/sqrt(2)/sigma_n) +
						  TMath::Erf(fabs(term_1)/sqrt(2)/sigma_n));
    }
    else{
      
      igne = par[3]/2 * exp(-par[3] * term_11) * (TMath::Erf(fabs(term_2)/sqrt(2)/sigma_n) -
						  TMath::Erf(fabs(term_1)/sqrt(2)/sigma_n));
    }
    
    s_real = TMath::PoissonI(i,par[4]) * ((1 - par[0])/sqrt(twopi)/sigma_n * 
					  exp(-pow(term_3,2)/2/pow(sigma_n,2)) +
					  par[0] * igne)*par[8]; 
    s_real_sum += s_real; 
  }
  
  
  return s_real_sum;
    
}

Double_t the_real_deal_yx_bg(Double_t *x, Double_t *par){

  // [0] = w, weight of discrete background
  // [1] = Q0, mean of PED
  // [2] = sigma_0, RMS of PED
  // [3] = alpha, decay constant of discrete background
  // [4] = mu, mean of the entire ADC distribution
  // [5] = q1, peak of SPE
  // [6] = sigma1, RMS of SPE
   
   // [7] = Norm of clock contribution
  // [8] =norm of 1-w term
  
  
  Double_t xx = x[0];

  Double_t s_real_sum = 0.;

  Double_t qn, sigma_n, term_1, term_2, term_3, igne, s_real, igne_is;

  Double_t poisson_is = exp(-par[4]);
  Double_t gaus_is = exp(-pow(xx-par[1],2)/2/pow(par[2],2))/par[2]/sqrt(twopi);
  
  if(xx >= par[1]){

    igne_is = par[3] * exp(-par[3]*(xx - par[1]));
    
  }
  
  else {
    
    igne_is = 0.;
    
  }
  
  Double_t s_real_sum_bg = poisson_is * ((1 - par[0]) * gaus_is + 
					 par[0] * igne_is)*par[8]; 
  
  // add in clock contribution
 double clock_contribution=par[7]*((1-par[0]) * gaus_is +
                                          par[0] * igne_is);

 
  return s_real_sum_bg+clock_contribution;
  //return s_real_sum_bg;
}


Double_t the_real_deal_yx_clock_bg(Double_t *x, Double_t *par){

  // [0] = w, weight of discrete background
  // [1] = Q0, mean of PED
  // [2] = sigma_0, RMS of PED
  // [3] = alpha, decay constant of discrete background
  // [4] = mu, mean of the entire ADC distribution
  // [5] = q1, peak of SPE
  // [6] = sigma1, RMS of SPE
   
   // [7] = Norm of clock contribution
  // [8] =norm of 1-w term
  
  
  Double_t xx = x[0];

  Double_t s_real_sum = 0.;

  Double_t qn, sigma_n, term_1, term_2, term_3, igne, s_real, igne_is;

  Double_t poisson_is = exp(-par[4]);
  Double_t gaus_is = exp(-pow(xx-par[1],2)/2/pow(par[2],2))/par[2]/sqrt(twopi);
  
  if(xx >= par[1]){

    igne_is = par[3] * exp(-par[3]*(xx - par[1]));
    
  }
  
  else {
    
    igne_is = 0.;
    
  }
  
  Double_t s_real_sum_bg = poisson_is * ((1 - par[0]) * gaus_is + 
					 par[0] * igne_is)*par[8]; 
  
  // add in clock contribution
 double clock_contribution=par[7]*((1-par[0]) * gaus_is +
                                          par[0] * igne_is);

 
  return clock_contribution;
}


