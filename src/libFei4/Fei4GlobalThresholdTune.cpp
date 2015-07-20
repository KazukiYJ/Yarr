// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Fei4 Threshold Scan
// # Comment: Nothing fancy
// ################################

#include "Fei4GlobalThresholdTune.h"

Fei4GlobalThresholdTune::Fei4GlobalThresholdTune(Fei4 *fe, TxCore *tx, RxCore *rx, ClipBoard<RawDataContainer> *data) : ScanBase(fe, tx, rx, data) {
    mask = MASK_16;
    dcMode = QUAD_DC;
    numOfTriggers = 100;
    triggerFrequency = 10e3;
    triggerDelay = 50;

    useScap = true;
    useLcap = true;

    target = 3000;
    verbose = false;
}

Fei4GlobalThresholdTune::Fei4GlobalThresholdTune(Bookkeeper *k) : ScanBase(k) {
    mask = MASK_16;
    dcMode = QUAD_DC;
    numOfTriggers = 100;
    triggerFrequency = 10e3;
    triggerDelay = 50;

    useScap = true;
    useLcap = true;

    target = 3000;
    verbose = false;

	keeper = k;
}


// Initialize Loops
void Fei4GlobalThresholdTune::init() {
    // Loop 0: Feedback, start with max fine threshold
    std::shared_ptr<Fei4GlobalFeedbackBase> fbLoop(Fei4GlobalFeedbackBuilder(&Fei4::Vthin_Fine));
    fbLoop->setStep(16);
    fbLoop->setMax(255);

    // Loop 1: Mask Staging
    std::shared_ptr<Fei4MaskLoop> maskStaging(new Fei4MaskLoop);
    maskStaging->setVerbose(verbose);
    maskStaging->setMaskStage(mask);
    maskStaging->setScap(useScap);
    maskStaging->setLcap(useLcap);
    
    // Loop 2: Double Columns
    std::shared_ptr<Fei4DcLoop> dcLoop(new Fei4DcLoop);
    dcLoop->setVerbose(verbose);
    dcLoop->setMode(dcMode);

    // Loop 3: Trigger
    std::shared_ptr<Fei4TriggerLoop> triggerLoop(new Fei4TriggerLoop);
    triggerLoop->setVerbose(verbose);
    triggerLoop->setTrigCnt(numOfTriggers);
    triggerLoop->setTrigFreq(triggerFrequency);
    triggerLoop->setTrigDelay(triggerDelay);

    // Loop 4: Data gatherer
    std::shared_ptr<StdDataLoop> dataLoop(new StdDataLoop);
    dataLoop->setVerbose(verbose);
    dataLoop->connect(g_data);

    this->addLoop(fbLoop);
    this->addLoop(maskStaging);
    this->addLoop(dcLoop);
    this->addLoop(triggerLoop);
    this->addLoop(dataLoop);

    engine.init();
}

// Do necessary pre-scan configuration
void Fei4GlobalThresholdTune::preScan() {
    // Global config
	g_tx->setCmdEnable(keeper->getTxMask());
    g_fe->writeRegister(&Fei4::Trig_Count, 12);
    g_fe->writeRegister(&Fei4::Trig_Lat, (255-triggerDelay)-4);
    g_fe->writeRegister(&Fei4::CalPulseWidth, 20); // Longer than max ToT 


	for(unsigned int k=0; k<keeper->feList.size(); k++) {
        Fei4 *fe = keeper->feList[k];
        // Set to single channel tx
		g_tx->setCmdEnable(0x1 << fe->getChannel());
        // Set specific pulser DAC
    	fe->writeRegister(&Fei4::PlsrDAC, fe->toVcal(target, useScap, useLcap));
        // Reset all TDACs
        // TODO do not if retune
        for (unsigned col=1; col<81; col++)
            for (unsigned row=1; row<337; row++)
                fe->setTDAC(col, row, 16);
	}
	g_tx->setCmdEnable(keeper->getTxMask());
    while(!g_tx->isCmdEmpty());
}
