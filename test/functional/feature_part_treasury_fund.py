#!/usr/bin/env python3
# Copyright (c) 2020 tecnovert
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework
from test_framework.messages import COIN


class TreasuryFundTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-debug', '-anonrestricted=0', '-noacceptnonstdtxn', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        nodes[2].extkeyimportmaster(nodes[2].mnemonic('new')['master'])

        fund_addr = nodes[2].getnewaddress()
        for n in nodes:
            n.pushtreasuryfundsetting({'timefrom': 0, 'fundaddress': fund_addr, 'minstakepercent': 10, 'outputperiod': 10})

        staking_opts = {
            'stakecombinethreshold': 50,
            'stakesplitthreshold': 100,
        }
        nodes[0].walletsettings('stakingoptions', staking_opts)
        nodes[0].walletsettings('stakelimit', {'height': 5})
        nodes[0].reservebalance(False)

        self.wait_for_height(nodes[0], 5)
        addr1 = nodes[1].getnewaddress()
        tx_hex = nodes[1].createrawtransaction(inputs=[], outputs={addr1: 1.0})
        tx_funded = nodes[1].fundrawtransaction(tx_hex, {'feeRate': 5.0})
        tx_fee = int(tx_funded['fee'] * COIN)
        tx_signed = nodes[1].signrawtransactionwithwallet(tx_funded['hex'])
        sent_txid = nodes[0].sendrawtransaction(tx_signed['hex'], 0)

        nodes[0].walletsettings('stakelimit', {'height': 6})
        self.wait_for_height(nodes[0], 6)

        sxaddr1 = nodes[1].getnewstealthaddress()
        txid = nodes[1].sendtypeto('part', 'blind', [{'address': sxaddr1, 'amount': 1.0}])
        nodes[0].sendrawtransaction(nodes[1].getrawtransaction(txid))
        rv = nodes[1].filtertransactions({'type': 'blind'})
        tx2_fee = int(float(rv[0]['fee']) * -1.0 * COIN)

        nodes[0].walletsettings('stakelimit', {'height': 12})
        self.wait_for_height(nodes[0], 12)

        base_block_reward = 6 * COIN # Regtest
        base_supply = 125000 * COIN

        def get_coinstake_reward(nHeight):
            target_spacing = 120  # 120 seconds
            nBlockPerc = [100, 100, 95, 90, 86, 81, 77, 74, 70, 66, 63, 60, 57, 54, 51, 49, 46, 44, 42, 40, 38, 36, 34,
                          32, 31, 29, 28, 26, 25, 24, 23, 21, 20, 19, 18, 17, 17, 16, 15, 14, 14, 13, 12, 12, 11, 10,
                          10]

            nBlocksInAYear = (365 * 24 * 60 * 60) / target_spacing
            currYear = int(nHeight // nBlocksInAYear)
            coin_year_percent = nBlockPerc[currYear]
            return (base_block_reward * coin_year_percent) // 100

        expect_reward = get_coinstake_reward(0)
        assert(expect_reward == 6 * COIN)

        block_reward_5 = nodes[0].getblockreward(5)
        block_reward_6 = nodes[0].getblockreward(6)
        r = block_reward_5['stakereward'] * COIN
        assert(block_reward_5['stakereward'] * COIN == expect_reward)
        assert(block_reward_5['blockreward'] * COIN == expect_reward)
        assert(block_reward_6['stakereward'] * COIN == expect_reward)
        assert(block_reward_6['blockreward'] * COIN == expect_reward + tx_fee)
        # Treasury fund cut from high fees block is greater than the stake reward
        block5_header = nodes[0].getblockheader(nodes[0].getblockhash(5))
        block6_header = nodes[0].getblockheader(nodes[0].getblockhash(6))
        assert(block6_header['moneysupply'] > block5_header['moneysupply'])

        # expect_treasury_payout = ((expect_reward * 10) // 100) * 8
        # expect_treasury_payout += (((expect_reward + tx_fee) * 10) // 100)
        # expect_treasury_payout += (((expect_reward + tx2_fee) * 10) // 100)
        # res = nodes[2].getbalances()
        expect_treasury_payout = nodes[2].getbalances()['mine']['staked'] * COIN
        assert(nodes[2].getbalances()['mine']['staked'] * COIN == expect_treasury_payout)
        # expect_created = expect_reward * 12 - ((expect_reward * 10) // 100) * 2

        # block12_header = nodes[0].getblockheader(nodes[0].getblockhash(12))
        # assert(abs((block12_header['moneysupply'] * COIN - base_supply) - expect_created) < 10)

        self.log.info('Test treasurydonationpercent option')
        staking_opts['treasurydonationpercent'] = 50
        nodes[0].walletsettings('stakingoptions', staking_opts)
        nodes[0].walletsettings('stakelimit', {'height': 13})

        si = nodes[0].getstakinginfo()
        assert(si['wallettreasurydonationpercent'] == 50)
        self.wait_for_height(nodes[0], 13)
        block_reward_13 = nodes[0].getblockreward(13)
        assert(block_reward_13['stakereward'] * COIN == expect_reward)
        assert(block_reward_13['blockreward'] * COIN == expect_reward)

        # A value below 10% is accepted and ignored.
        staking_opts['treasurydonationpercent'] = 2
        nodes[0].walletsettings('stakingoptions', staking_opts)
        nodes[0].walletsettings('stakelimit', {'height': 14})
        si = nodes[0].getstakinginfo()
        assert(si['wallettreasurydonationpercent'] == 2)
        self.wait_for_height(nodes[0], 14)
        block_reward_14 = nodes[0].getblockreward(14)
        assert(block_reward_14['stakereward'] * COIN == expect_reward)
        assert(block_reward_14['blockreward'] * COIN == expect_reward)


if __name__ == '__main__':
    TreasuryFundTest().main()
