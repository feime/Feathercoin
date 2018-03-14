// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    int nHeight = pindexLast->nHeight + 1;
    int nTargetTimespan = params.nPowTargetTimespan;
    int nTargetSpacing = params.nPowTargetSpacing;

    if (nHeight >= params.nForkOne)
        nTargetTimespan = (7 * 24 * 60 * 60) / 8; // 7/8 days

    if (nHeight >= params.nForkTwo)
        nTargetTimespan = (7 * 24 * 60 * 60) / 32; // 7/32 days

    int64_t nInterval = nTargetTimespan / nTargetSpacing;

    bool fHardFork = nHeight == params.nForkOne || nHeight == params.nForkTwo;

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % nInterval != 0 && !fHardFork)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % nInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (nInterval-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();

    if (nHeight >= params.nForkTwo) {
        nInterval *= 4;
        const CBlockIndex* pindexFirst = pindexLast;
        for(int i = 0; pindexFirst && i < nInterval; i++)
            pindexFirst = pindexFirst->pprev;

        int nActualTimespanLong = (pindexLast->GetBlockTime() - pindexFirst->GetBlockTime())/4;

        // Average between short and long windows
        int nActualTimespanAvg = (nActualTimespan + nActualTimespanLong) / 2;

        // Apply .25 damping
        nActualTimespan = nActualTimespanAvg + 3 * nTargetTimespan;
        nActualTimespan /= 4;
    }

    // The initial settings (4.0 difficulty limiter)
    int nActualTimespanMax = nTargetTimespan*4;
    int nActualTimespanMin = nTargetTimespan/4;

    // The 1st hard fork (1.4142857 aka 41% difficulty limiter)
    if (nHeight >= params.nForkOne && nHeight < params.nForkTwo) {
        nActualTimespanMax = nTargetTimespan*99/70;
        nActualTimespanMin = nTargetTimespan*70/99;
    }

    // The 2nd hard fork (1.0905077 aka 9% difficulty limiter)
    if (nHeight >= params.nForkTwo) {
        nActualTimespanMax = nTargetTimespan*494/453;
        nActualTimespanMin = nTargetTimespan*453/494;
    }

    // Limit adjustment step
    if(nActualTimespan < nActualTimespanMin)
        nActualTimespan = nActualTimespanMin;
    if(nActualTimespan > nActualTimespanMax)
        nActualTimespan = nActualTimespanMax;

    // Retarget
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;
    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
