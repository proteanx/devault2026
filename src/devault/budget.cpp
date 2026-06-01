// Copyright (c) 2019 The DeVault developers
// Copyright (c) 2019 Jon Spock
// Copyright (c) 2026 The DeVault developers (V2 port)
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <devault/budget.h>

#include <chainparams.h>
#include <consensus/params.h>
#include <key_io.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/standard.h>

#include <limits>
#include <string>
#include <vector>

namespace {

// Group Address with % for safety/clarity
struct BudgetStruct {
    std::string MainNetAddress;
    std::string TestNetAddress;
    std::string Purpose;
    int64_t percent;
};

struct BudgetPayouts {
    int32_t changeSuperBlock; // Where to switch between old DAO addresses and new DAO addresses
    std::vector<BudgetStruct> budget;
};

// Reward Address & Percentage (verbatim from legacy DeVault devault/budget.cpp:30-55).
const BudgetPayouts Payouts[] = {
    {
        5, // change on 5th Superblock
        {{"devault:pp2ghv9ya7fs98rvz3gzuqmen608dh6g2y5d5dxrtp", "dvtest:pr2jg83w445mrwqwkczgdevyeetatckhg5nrmvd67c", "Community", 15},
         {"devault:prg2wlmzj7kzy8ps7pfnkf39nze49yh8fsk0yfslw0", "dvtest:pr49tdjhuktpp440edg9cej7d9hcvnrluvdu38d59d", "CoreDevs", 10},
         {"devault:pqws2sgc2y22x2gkcnmw72edpa0u0kscdsqp29e530", "dvtest:qzlg7mlrz56hnnwddc8k0a2w397sqjgeggmdsg5np3", "WebDevs", 5},
         {"devault:pzgux6zlzpw45hm45fwcj5d7mf5fn7pa2ydjzx5nxw", "dvtest:qpzn5j40a3r0kaznf8y6jpaa4z7stmg9luk4t8x8nv", "BusDevs", 5},
         {"devault:prutq74qks5aez2a4mhrcm26t0r3pjzpxyapq0kdjk", "dvtest:qqltstfypfuftlgrvqdfml4js0y3yxdpgya2krkhk5", "Marketing", 5},
         {"devault:prcljsfamr0hsc2jn4mr5et9xx5u9lm8rvl976n0zm", "dvtest:qpw43t63nxmufurn5qn9jyurc27xntvdz5n7ujhdaq", "Support", 5}},
    },
    {
        15,
        {{"devault:pp2ghv9ya7fs98rvz3gzuqmen608dh6g2y5d5dxrtp", "dvtest:pr2jg83w445mrwqwkczgdevyeetatckhg5nrmvd67c", "Community", 15},
         {"devault:prg2wlmzj7kzy8ps7pfnkf39nze49yh8fsk0yfslw0", "dvtest:pr49tdjhuktpp440edg9cej7d9hcvnrluvdu38d59d", "CoreDevs", 10},
         {"devault:pqws2sgc2y22x2gkcnmw72edpa0u0kscdsqp29e530", "dvtest:qzlg7mlrz56hnnwddc8k0a2w397sqjgeggmdsg5np3", "WebDevs/Support", 10},
         {"devault:pzgux6zlzpw45hm45fwcj5d7mf5fn7pa2ydjzx5nxw", "dvtest:qpzn5j40a3r0kaznf8y6jpaa4z7stmg9luk4t8x8nv", "BusDev/Marketing", 10}},
    },
    {
        std::numeric_limits<int32_t>::max(), // change if we add more to superblock change
        {{"devault:pqqqaf843zj992fkqr483zptyp0r8kfg7g5enjt05d", "dvtest:pr2jg83w445mrwqwkczgdevyeetatckhg5nrmvd67c", "Community", 15},
         {"devault:prg2wlmzj7kzy8ps7pfnkf39nze49yh8fsk0yfslw0", "dvtest:pr49tdjhuktpp440edg9cej7d9hcvnrluvdu38d59d", "CoreDevs", 10},
         {"devault:prgtl5yust3v2m76c3t4vsnuwuufaht0euqm2smja2", "dvtest:qzlg7mlrz56hnnwddc8k0a2w397sqjgeggmdsg5np3", "WebDevs/Support", 10},
         {"devault:pz3htku32554kntjvzpwp8nuhmksvp73f5wwmxts8h", "dvtest:qpzn5j40a3r0kaznf8y6jpaa4z7stmg9luk4t8x8nv", "BusDev/Marketing", 10}},
    }};

// Get Array Size at Compile time for Loops
const int ChangeSize = sizeof(Payouts) / sizeof(Payouts[0]);

int getPayoutIndexFromHeight(int SuperBlockNumber) {
    int i = 0;
    for (i = 0; i < ChangeSize; i++) {
        if (Payouts[i].changeSuperBlock > SuperBlockNumber) {
            break;
        }
    }
    return i;
}

} // namespace

bool CheckSuperBlockBudget(const CBlock &block, int nHeight, const Amount &nBlockSubsidy,
                           const CChainParams &chainparams, Amount &nBudgetReward) {
    const Consensus::Params &consensus = chainparams.GetConsensus();
    if (!consensus.IsSuperBlock(nHeight)) {
        nBudgetReward = Amount::zero();
        return true;
    }

    const int64_t nBlocksPerPeriod = consensus.nBlocksPerYear / 12;
    const bool fTestNet = (chainparams.NetworkIDString() != "main");
    const int Index = getPayoutIndexFromHeight(int(nHeight / nBlocksPerPeriod));
    const int BudgetSize = int(Payouts[Index].budget.size());

    // Sum of the %s -> scale factor (DeVault budget is a fraction of the miner's portion).
    int PerCentSum = 0;
    for (const auto &p : Payouts[Index].budget) {
        PerCentSum += p.percent;
    }
    const int ScaleFactor = (100 - PerCentSum);

    // Compute the expected budget scripts + payments (CalculateSuperBlockRewards, ported verbatim;
    // DeVault's `.toInt()` is BCHN's `/ SATOSHI`, both the raw satoshi int64; trailing `* COIN`
    // converts the integer coin amount back to an Amount).
    std::vector<CScript> Scripts(BudgetSize);
    std::vector<Amount> nPayment(BudgetSize);
    Amount refRewards = Amount::zero();
    for (int i = 0; i < BudgetSize; i++) {
        const std::string &addr = fTestNet ? Payouts[Index].budget[i].TestNetAddress
                                           : Payouts[Index].budget[i].MainNetAddress;
        Scripts[i] = GetScriptForDestination(DecodeDestination(addr, chainparams));
        nPayment[i] = (((Payouts[Index].budget[i].percent * nBlocksPerPeriod * (nBlockSubsidy / SATOSHI)) /
                        (ScaleFactor * (COIN / SATOSHI))) *
                       COIN);
        refRewards += nPayment[i];
    }

    // Verify the coinbase pays each budget address exactly its expected amount (CBudget::Validate).
    bool fPaymentOK = true;
    Amount nSumReward = Amount::zero();
    for (const auto &out : block.vtx[0]->vout) {
        for (int i = 0; i < BudgetSize; i++) {
            if (out.scriptPubKey == Scripts[i]) {
                if (out.nValue != nPayment[i]) {
                    fPaymentOK = false;
                } else {
                    nSumReward += nPayment[i];
                }
            }
        }
    }
    if (nSumReward != refRewards) {
        fPaymentOK = false;
    }
    nBudgetReward = nSumReward;
    return fPaymentOK;
}
