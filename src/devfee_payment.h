/*
 * Copyright (c) 2020 The Raptoreum developer
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * DevfeePayment.h
 *
 *  Created on: Jun 24, 2018
 *      Author: Tri Nguyen
 */

#ifndef SRC_DEVFEE_PAYMENT_H_
#define SRC_DEVFEE_PAYMENT_H_
#include <string>
#include <amount.h>
#include <primitives/transaction.h>
#include <vector>
using namespace std;

static const string DEFAULT_DEVFEE_ADDRESS = "Xgtyuk76vhuFW2iT7UAiHgNdWXCf3J34wh";
struct DevfeeRewardStructure {
	  int blockHeight;
	  int rewardDivisor;
};

class DevfeePayment {
public:
	  DevfeePayment(vector<DevfeeRewardStructure> rewardStructures = {}, int startBlock = 0, const string &address = DEFAULT_DEVFEE_ADDRESS) {
        this->devfeeAddress = address;
        this->startBlock = startBlock;
        this->rewardStructures = rewardStructures;
    }
    ~DevfeePayment(){};
    CAmount getDevfeePaymentAmount(int blockHeight, CAmount blockSubsidy);
    void FillDevfeePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockSubsidy, CTxOut& txoutDevfeeRet);
    bool IsBlockPayeeValid(const CTransaction& txNew, const int height, const CAmount blockSubsidy);
    int getStartBlock() {return this->startBlock;}
private:
    string devfeeAddress;
    int startBlock;
    vector<DevfeeRewardStructure> rewardStructures;
};

#endif /* SRC_DEVFEE_PAYMENT_H_ */