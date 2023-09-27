#!/usr/bin/env python3
# Copyright (c) 2023 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal, math

from test_framework.test_particl import GhostTestFramework
from test_framework.messages import COIN

from test_framework.util import (
    assert_equal
)

class GhostSupplyLimitTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-debug', '-anonrestricted=0', '-txindex=1'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)
        self.sync_all()


    def get_balance_of(self, addr, tracked_balances):
        for i in range(0, len(tracked_balances)):
            if tracked_balances[i]["Address"] == addr:
                return tracked_balances[i]["Balance"]
        return False

    def is_elig(self, addr, balances):
        found = False
        for eligAddress in balances:
            if addr == eligAddress["Address"]:
                found = True
                break
        return found

    
    def _check_original_expected_distribution(self):
        original_stakereward_expected = 6
        original_devfund_expected = 0.96

        assert_equal(original_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward'])
        assert(math.isclose(original_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    def _check_original_expected_distribution_agvr(self):
        original_stakereward_expected = 6
        original_agvrreward_expected = 3
        original_devfund_expected = 0.96

        assert_equal(original_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward'])
        assert_equal(original_agvrreward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['gvrreward'])
        assert(math.isclose(original_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    def _check_corrected_expected_distribution(self):
        corrected_stakereward_expected = 9
        corrected_devfund_expected = 1.89

        assert_equal(corrected_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward'])
        assert(math.isclose(corrected_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    def _check_corrected_expected_distribution_agvr(self):
        corrected_stakereward_expected = 9
        corrected_agvrreward_expected = 2.97
        corrected_devfund_expected = 1.89

        assert_equal(corrected_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward'])
        assert(math.isclose(corrected_agvrreward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['gvrreward']))
        assert(math.isclose(corrected_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    
    def _check_final_corrected_expected_distribution(self):
        corrected_stakereward_expected = 0.9
        corrected_devfund_expected = 0.189

        assert(math.isclose(corrected_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward']))
        assert(math.isclose(corrected_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    def _check_final_corrected_expected_distribution_agvr(self):
        corrected_stakereward_expected = 0.9
        corrected_agvrreward_expected = 0.297
        corrected_devfund_expected = 0.189

        assert(math.isclose(corrected_stakereward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['stakereward']))
        assert(math.isclose(corrected_agvrreward_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['gvrreward']))
        assert(math.isclose(corrected_devfund_expected, self.nodes[0].getblockreward(self.nodes[0].getblockcount())['treasuryreward']))

    
    def run_test(self):
        nodes = self.nodes
        self.stop_node(0)
        self.stop_node(1)
        self.stop_node(2)
        

        # Start nodes with threshold set to 1000000000000
        # With rangespan sets to 5 => Means an address has to hold a balance for at least 5 blocks
        # 

        self.start_node(0, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0', '-rewardcorrectionheight=25', '-moneysupplycap=125171'])
        self.start_node(1, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0', '-rewardcorrectionheight=25', '-moneysupplycap=125171'])
        self.start_node(2, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0', '-rewardcorrectionheight=25', '-moneysupplycap=125171'])

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')

        assert_equal(nodes[0].getwalletinfo()["total_balance"], 100000)
        assert_equal(nodes[1].getwalletinfo()["total_balance"], 25000)
        assert_equal(nodes[2].getwalletinfo()["total_balance"], 0)

        reward_address0 = nodes[0].getnewaddress()
        reward_address1 = nodes[1].getnewaddress()
        reward_address2 = nodes[2].getnewaddress()
        treas_addr = nodes[0].getnewaddress()

        nodes[0].walletsettings('stakingoptions', {'enabled': False})
        nodes[1].walletsettings('stakingoptions', {'rewardaddress': reward_address1, 'enabled': False})
        nodes[2].walletsettings('stakingoptions', {'rewardaddress': reward_address2, 'enabled': False})


        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)

        for n in nodes:
            n.pushtreasuryfundsetting({'timefrom': 0, 'fundaddress': treas_addr, 'minstakepercent': 10, 'outputperiod': 1})

        node2_to_addr = reward_address2
        amount = 10_002

        nodes[0].walletsettings('stakingoptions', {'enabled': True})
        
        # send a vet amount to node 2.
        # This sets up one of two veteran accounts.
        
        outputs = [{'address': node2_to_addr, 'amount': amount, 'subfee': True}]
        tx = nodes[0].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = nodes[0].getrawtransaction(tx['txid'])
        txhash = nodes[0].sendrawtransaction(rawtx)

        self.wait_for_mempool(nodes[2], txhash)


        # Node 1 sends its balance to itself to start agvr tracking.
        # This is the second of two veteran accounts.

        total_balance = nodes[1].getbalances()["mine"]["trusted"]

        outputs = [{'address': reward_address1, 'amount': total_balance, 'subfee': True}]
        tx = nodes[1].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = nodes[1].getrawtransaction(tx['txid'])
        txhash = nodes[1].sendrawtransaction(rawtx)

        self.wait_for_mempool(nodes[1], txhash)


        # Node 0 stakes six blocks to include our transactions and for the agvr amounts to mature
        # We check here that our stake reward and that it's distribution is as expected
        
        self.stakeToHeight(10)
        self._check_original_expected_distribution()

        
        # Node 1 staking is activated. We will then stake a block. 
        # We Check to make sure the agvr carry forward is as expected.
        # Our block count here should be at 11.
        # This puts our total expected agvr amount at 33 (11 * 3)

        nodes[1].walletsettings('stakingoptions', {'enabled': True})
        #print(nodes[1].getbalances())
        #print(nodes[1].getstakinginfo())
        self.stakeBlocks(1, 1)

        #print(nodes[1].getblockreward(nodes[1].getblockcount()))
        
        assert_equal(33, nodes[1].getblockreward(nodes[1].getblockcount())['gvrreward'])

        # Node 2 will now stake a block.
        # We will check that the original distribution with agvr is as expected
        

        nodes[2].walletsettings('stakingoptions', {'enabled': True})
        self.stakeBlocks(1, 2)
        self._check_original_expected_distribution_agvr()


        # Node 0 will now stake 13 blocks to bring up to the correction fork at block 25.

        self.stakeToHeight(25)

        self._check_corrected_expected_distribution()

        # Node 1 will now stake an agvr block.
        
        self.nodes[0].reservebalance(False)
        self.nodes[1].reservebalance(False)
        self.nodes[2].reservebalance(False)

        self.stakeBlocks(1, 1)


        # Now Node 2 will stake an agvr block.
        # This will be checked that the amounts are as expected

        self.stakeBlocks(1, 2)
        self._check_corrected_expected_distribution_agvr()

        # Now we stake to the max supply and check distribution

        self.stakeBlocks(1, 0)
        self._check_final_corrected_expected_distribution()

        # Now we will stake 2 agvr blocks, then check the distribution

        self.nodes[0].reservebalance(False)
        self.nodes[1].reservebalance(False)
        self.nodes[2].reservebalance(False)

        self.stakeBlocks(1, 1)
        self.stakeBlocks(1, 2)
        self._check_final_corrected_expected_distribution_agvr()


if __name__ == '__main__':
    GhostSupplyLimitTest().main()