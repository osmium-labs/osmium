#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Utilities for manipulating blocks and transactions."""

from binascii import a2b_hex
from decimal import Decimal
import io
import struct
import time
import unittest

from .messages import (
    CBlock,
    CCbTx,
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    FromHex,
    uint256_to_string,
)
from .script import CScript, CScriptNum, CScriptOp, OP_DUP, OP_EQUALVERIFY, OP_HASH160, OP_TRUE, OP_CHECKSIG
from .util import assert_equal, hex_str_to_bytes
from io import BytesIO

MAX_BLOCK_SIGOPS = 20000

# Osmium is always-auxpow: the chain ID occupies the top 16 bits of nVersion, and validation
# rejects a block whose version is 1 as "late-legacy-block" above height 1 (validation.cpp:3887)
# and anything carrying a different chain ID outright (validation.cpp:3723). Dash's framework
# defaulted to version 1, so every block the framework built was refused.
AUXPOW_CHAIN_ID = 0x0062  # regtest nAuxpowChainId, src/chainparams.cpp
BASE_BLOCK_VERSION = (AUXPOW_CHAIN_ID << 16) | 4

# Genesis block time (regtest)
TIME_GENESIS_BLOCK = 1417713337

# Coinbase transaction outputs can only be spent after this number of new blocks (network rule)
COINBASE_MATURITY = 100

NORMAL_GBT_REQUEST_PARAMS = {"rules": []} # type: ignore[var-annotated]

def create_block(hashprev=None, coinbase=None, ntime=None, *, version=None, tmpl=None, txlist=None, dip4_activated=False, v20_activated=False):
    """Create a block (with regtest difficulty)."""
    block = CBlock()
    if tmpl is None:
        tmpl = {}
    block.nVersion = version or tmpl.get('version') or BASE_BLOCK_VERSION
    block.nTime = ntime or tmpl.get('curtime') or int(time.time() + 600)
    block.hashPrevBlock = hashprev or int(tmpl['previousblockhash'], 0x10)
    if tmpl and not tmpl.get('bits') is None:
        block.nBits = struct.unpack('>I', a2b_hex(tmpl['bits']))[0]
    else:
        block.nBits = 0x207fffff  # difficulty retargeting is disabled in REGTEST chainparams
    if coinbase is None:
        coinbase = create_coinbase(height=tmpl['height'], dip4_activated=dip4_activated, v20_activated=v20_activated)
    block.vtx.append(coinbase)
    if txlist:
        for tx in txlist:
            if not hasattr(tx, 'calc_sha256'):
                txo = CTransaction()
                txo.deserialize(io.BytesIO(tx))
                tx = txo
            block.vtx.append(tx)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    return block

def create_block_with_mnpayments(mninfo, node, vtx=None, mn_payee=None, mn_amount=None):
    if vtx is None:
        vtx = []
    bt = node.getblocktemplate()
    height = bt['height']
    tip_hash = bt['previousblockhash']
    coinbasevalue = bt['coinbasevalue']

    assert len(bt['masternode']) <= 2
    if mn_payee is None:
        mn_payee = bt['masternode'][0]['payee']

    mn_operator_payee = None
    if len(bt['masternode']) == 2:
        mn_operator_payee = bt['masternode'][1]['payee']
    # we can't take the masternode payee amount from the template here as we might have additional fees in vtx

    # calculate fees that the block template included (we'll have to remove it from the coinbase as we won't
    # include the template's transactions
    bt_fees = 0
    for tx in bt['transactions']:
        bt_fees += tx['fee']

    new_fees = 0
    for tx in vtx:
        in_value = 0
        out_value = 0
        for txin in tx.vin:
            txout = node.gettxout(uint256_to_string(txin.prevout.hash), txin.prevout.n, False)
            in_value += int(txout['value'] * COIN)
        for txout in tx.vout:
            out_value += txout.nValue
        new_fees += in_value - out_value

    # fix fees
    coinbasevalue -= bt_fees
    coinbasevalue += new_fees

    operator_reward = 0
    if mn_operator_payee is not None:
        for mn in mninfo:
            if mn.rewards_address == mn_payee:
                operator_reward = mn.operator_reward
                break
        assert operator_reward > 0

    mn_operator_amount = 0
    if mn_amount is None:
        v20_info = node.getblockchaininfo()['softforks']['v20']
        mn_amount_total = get_masternode_payment(height, coinbasevalue, v20_info['active'])
        mn_operator_amount = mn_amount_total * operator_reward // 100
        mn_amount = mn_amount_total - mn_operator_amount
    # Osmium's coinbase must also pay the devfee once past nDevfeePayment.getStartBlock(), or the
    # block is rejected as bad-cb-devfee-payment-not-found. getblocktemplate's coinbasevalue
    # already includes it (devfee_payment.cpp appends the output to the template's coinbase), so
    # take it out of the miner's share rather than adding it on top.
    devfee = bt.get('devfee') or {}
    devfee_amount = devfee.get('amount', 0)
    devfee_payee = devfee.get('payee')

    miner_amount = coinbasevalue - mn_amount - mn_operator_amount - devfee_amount

    miner_address = node.get_deterministic_priv_key().address
    outputs = {miner_address: str(Decimal(miner_amount) / COIN)}
    if devfee_amount > 0:
        assert devfee_payee is not None, "block template reports a devfee amount but no payee"
        assert devfee_payee != miner_address, "devfee payee collides with the miner address"
        outputs[devfee_payee] = str(Decimal(devfee_amount) / COIN)
    if mn_amount > 0:
        outputs[mn_payee] = str(Decimal(mn_amount) / COIN)
    if mn_operator_amount > 0:
        outputs[mn_operator_payee] = str(Decimal(mn_operator_amount) / COIN)

    coinbase = FromHex(CTransaction(), node.createrawtransaction([], outputs))
    coinbase.vin = create_coinbase(height).vin

    # We can't really use this one as it would result in invalid merkle roots for masternode lists
    if len(bt['coinbase_payload']) != 0:
        tip_block = node.getblock(tip_hash)
        cbtx = FromHex(CCbTx(version=1), bt['coinbase_payload'])
        if 'cbTx' in tip_block:
            cbtx.merkleRootMNList = int(tip_block['cbTx']['merkleRootMNList'], 16)
        else:
            cbtx.merkleRootMNList = 0
        coinbase.nVersion = 3
        coinbase.nType = 5 # CbTx
        coinbase.vExtraPayload = cbtx.serialize()

    coinbase.calc_sha256()

    block = create_block(int(tip_hash, 16), coinbase, ntime=bt['curtime'], version=bt['version'])
    block.vtx += vtx

    # Add quorum commitments from template
    for tx in bt['transactions']:
        tx2 = FromHex(CTransaction(), tx['data'])
        if tx2.nType == 6:
            block.vtx.append(tx2)

    block.hashMerkleRoot = block.calc_merkle_root()
    block.solve()
    return block

def script_BIP34_coinbase_height(height):
    if height <= 16:
        res = CScriptOp.encode_op_n(height)
        # Append dummy to increase scriptSig size above 2 (see bad-cb-length consensus rule)
        return CScript([res, OP_TRUE])
    return CScript([CScriptNum(height)])


# Regtest devfee payee, src/chainparams.cpp (CRegTestParams nDevfeePayment).
DEVFEE_ADDRESS = "sZmtmzxjw7cfsy7SC5CmUHKREH2UYnYnuu"


def devfee_script():
    """scriptPubKey the coinbase must pay the devfee to on regtest."""
    from .address import base58_to_byte
    payload = base58_to_byte(DEVFEE_ADDRESS)[0]
    return CScript([OP_DUP, OP_HASH160, payload, OP_EQUALVERIFY, OP_CHECKSIG])


def create_coinbase(height, pubkey=None, dip4_activated=False, v20_activated=False, nValue=None):
    """Create a coinbase transaction, assuming no miner fees.

    If pubkey is passed in, the coinbase output will be a P2PK output;
    otherwise an anyone-can-spend output.

    Osmium does not pay Dash's flat 500 with bit-shift halvings, and above
    nDevfeePayment.getStartBlock() the coinbase must also carry the devfee output or the block is
    rejected as bad-cb-devfee-payment-not-found. Pass nValue to override the miner's share (for
    tests that deliberately build an invalid amount); the devfee is still added, because it is
    required independently of what the miner pays itself.
    """
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), script_BIP34_coinbase_height(height), 0xffffffff))
    coinbaseoutput = CTxOut()
    devfee = get_devfee(height)
    if nValue is None:
        coinbaseoutput.nValue = get_block_subsidy(height) - devfee
    else:
        coinbaseoutput.nValue = int(nValue * COIN)
    if (pubkey is not None):
        coinbaseoutput.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        coinbaseoutput.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [coinbaseoutput]
    if devfee > 0:
        coinbase.vout.append(CTxOut(devfee, devfee_script()))
    if dip4_activated:
        coinbase.nVersion = 3
        coinbase.nType = 5
        cbtx_version = 3 if v20_activated else 2
        cbtx_payload = CCbTx(cbtx_version, height, 0, 0, 0)
        coinbase.vExtraPayload = cbtx_payload.serialize()
    coinbase.calc_sha256()
    return coinbase

def create_tx_with_script(prevtx, n, script_sig=b"", *, amount, script_pub_key=CScript()):
    """Return one-input, one-output transaction object
       spending the prevtx's n-th output with the given amount.

       Can optionally pass scriptPubKey and scriptSig, default is anyone-can-spend output.
    """
    tx = CTransaction()
    assert n < len(prevtx.vout)
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), script_sig, 0xffffffff))
    tx.vout.append(CTxOut(amount, script_pub_key))
    tx.calc_sha256()
    return tx

def create_transaction(node, txid, to_address, *, amount):
    """ Return signed transaction spending the first output of the
        input txid. Note that the node must be able to sign for the
        output that is being spent, and the node must not be running
        multiple wallets.
    """
    raw_tx = create_raw_transaction(node, txid, to_address, amount=amount)
    tx = CTransaction()
    tx.deserialize(BytesIO(hex_str_to_bytes(raw_tx)))
    return tx

def create_raw_transaction(node, txid, to_address, *, amount):
    """ Return raw signed transaction spending the first output of the
        input txid. Note that the node must be able to sign for the
        output that is being spent, and the node must not be running
        multiple wallets.
    """
    rawtx = node.createrawtransaction(inputs=[{"txid": txid, "vout": 0}], outputs={to_address: amount})
    signresult = node.signrawtransactionwithwallet(rawtx)
    assert_equal(signresult["complete"], True)
    return signresult['hex']

def get_legacy_sigopcount_block(block, accurate=True):
    count = 0
    for tx in block.vtx:
        count += get_legacy_sigopcount_tx(tx, accurate)
    return count

def get_legacy_sigopcount_tx(tx, accurate=True):
    count = 0
    for i in tx.vout:
        count += i.scriptPubKey.GetSigOpCount(accurate)
    for j in tx.vin:
        # scriptSig might be of type bytes, so convert to CScript for the moment
        count += CScript(j.scriptSig).GetSigOpCount(accurate)
    return count

# Identical to GetMasternodePayment in C++ code
def get_block_subsidy(nHeight, nSubsidyHalvingInterval=150, nSuperblockStartBlock=1500):
    """Block subsidy in muffs at `nHeight`, per src/validation.cpp GetBlockSubsidyHelper.

    Osmium does not pay Dash's flat 500 per regtest block. Height 1 mints an 8000-coin premine,
    heights 2..500 pay 0.1, and later blocks pay 1 -- then every nSubsidyHalvingInterval blocks
    the subsidy is reduced by 1/7 (the C++ `reductionRatio` is 1210000/172800, which is integer
    division and therefore exactly 7). Superblock share is carved out above nSuperblockStartBlock.

    Defaults are the regtest values from src/chainparams.cpp (CRegTestParams).
    """
    nPrevHeight = nHeight - 1
    if nPrevHeight == 0:
        base = 8000
    elif nPrevHeight <= 500:
        base = 0.1
    else:
        base = 1
    nSubsidy = int(base * COIN)

    # The C++ does this in floating point -- `reductionRatio` is a double (1210000/172800, an
    # integer division that yields exactly 7.0) and `nSubsidy -= nSubsidy / reductionRatio`
    # truncates back to CAmount. Integer division here is off by a few muffs per halving.
    i = nSubsidyHalvingInterval
    while i <= nPrevHeight:
        nSubsidy = int(nSubsidy - nSubsidy / 7.0)
        i += nSubsidyHalvingInterval

    nSuperblockPart = nSubsidy // 20 if nPrevHeight > nSuperblockStartBlock else 0
    return nSubsidy - nSuperblockPart


def get_devfee(nHeight, start_block=50, divisor=19):
    """Devfee carved out of the coinbase at `nHeight` (src/devfee_payment.cpp).

    Zero at or below nDevfeePayment.getStartBlock(); otherwise blockSubsidy / rewardDivisor,
    integer division as in the C++. Defaults are the regtest values from src/chainparams.cpp.
    """
    if nHeight <= start_block:
        return 0
    return get_block_subsidy(nHeight) // divisor


def get_miner_reward(nHeight):
    """What the miner actually receives at `nHeight`: the subsidy less the devfee."""
    return get_block_subsidy(nHeight) - get_devfee(nHeight)


def get_masternode_payment(nHeight, blockValue, fV20Active=None):
    """Osmium's masternode share of the block value.

    Osmium replaced Dash's gradual MN_RR reallocation with a fixed, height-stepped split
    (src/validation.cpp GetMasternodePayment): 9/19 of the block value, rising at
    nMasternodePaymentsIncreaseBlock and again at nMasternodePaymentsIncreaseBlock2. There is no
    reallocation schedule and no superblock-cycle interpolation here, and fV20Active is ignored by
    the node -- it is accepted only so callers can keep passing it.

    The divisions truncate exactly as the C++ integer arithmetic does (multiply, then divide).
    """
    # Regtest values from src/chainparams.cpp (CRegTestParams).
    nMNPIBlock = 350
    nMNPIBlock2 = 650

    ret = blockValue * 9 // 19
    if nHeight >= nMNPIBlock:
        ret = blockValue * 72 // 95
    if nHeight >= nMNPIBlock2:
        ret = blockValue * 72 // 85
    return ret
