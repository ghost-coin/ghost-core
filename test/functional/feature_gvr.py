#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework
from test_framework.messages import COIN

from test_framework.util import (
    assert_equal
)

class GhostVeteranRewardTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-debug', '-anonrestricted=0', '-reservebalance=0',  '-txindex=1'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)
        self.sync_all()

    def accumulate_unspents_to_one_address(self, node, stakingNode):
        stakingNode.walletsettings('stakingoptions', {'enabled': True})
        node.walletsettings('stakingoptions', {'enabled': False})

        addr = node.getnewaddress()
        total_balance = node.getbalances()["mine"]["trusted"]
        outputs = [{'address': addr, 'amount': total_balance, 'subfee': True}]
        tx = node.sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = node.getrawtransaction(tx['txid'])
        txhash = node.sendrawtransaction(rawtx)
        self.wait_for_mempool(stakingNode, txhash)
        self.stakeBlocks(1, 1)

        total_balance = total_balance - tx["fee"]

        unspent = node.listunspent(0, 99999)

        for i in range(0, len(unspent)):
            if unspent[i]["address"] == addr:
                continue
            outputs = [{'address': addr, 'amount': unspent[i]['amount'], 'subfee': True}]
            tx = node.sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
            rawtx = node.getrawtransaction(tx['txid'])
            txhash = node.sendrawtransaction(rawtx)
            self.wait_for_mempool(stakingNode, txhash)
            total_balance = total_balance + unspent[i]['amount'] - tx["fee"]

        self.stakeBlocks(1, 1)
        return addr, total_balance

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

    def run_test(self):
        nodes = self.nodes
        self.stop_node(0)
        self.stop_node(1)
        self.stop_node(2)

        self.start_node(0, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.start_node(1, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.start_node(2, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')

        reward_address0 = nodes[0].getnewaddress()
        reward_address1 = nodes[1].getnewaddress()
        reward_address2 = nodes[2].getnewaddress()
        treas_addr = nodes[2].getnewaddress()

        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': False})
        nodes[1].walletsettings('stakingoptions', {'rewardaddress': reward_address1, 'enabled': False})
        nodes[2].walletsettings('stakingoptions', {'rewardaddress': reward_address2, 'enabled': False})

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)

        for n in nodes:
            n.pushtreasuryfundsetting({'timefrom': 0, 'fundaddress': treas_addr, 'minstakepercent': 10, 'outputperiod': 1})

        balance_node0 = nodes[0].getwalletinfo()
        balance_node1 = nodes[1].getwalletinfo()

        assert_equal(balance_node0["total_balance"], 100000)
        assert_equal(balance_node1["total_balance"], 25000)

        to_addr = nodes[0].getnewaddress()
        outputs = [{'address': to_addr, 'amount': 10, 'subfee': True}]

        tx = nodes[1].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = nodes[1].getrawtransaction(tx['txid'])
        txhash = nodes[1].sendrawtransaction(rawtx)

        # Enable back staking on node 0
        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': True})
        self.wait_for_mempool(nodes[0], txhash)
        self.stakeBlocks(1)

        # Check the balances of the balances tracked till this point
        # 10 = Amount sent
        # 2.04 = Block reward
        # 0.96 = Dev Fund
        tracked_balances = nodes[0].geteligibleaddresses(1, False)
        assert_equal(self.get_balance_of(to_addr, tracked_balances) * COIN, (10 - tx["fee"]) * COIN )
        assert_equal(self.get_balance_of(reward_address0, tracked_balances) * COIN, (2.04 + float(tx["fee"])) * COIN)
        assert_equal(self.get_balance_of(treas_addr, tracked_balances) * COIN, 0.96 * COIN)

        balance_node0 = nodes[0].getwalletinfo()
        balance_node1 = nodes[1].getwalletinfo()
        balance_node2 = nodes[2].getwalletinfo()

        # 10 is the amount sent, (2.04+fees) is the reward
        assert_equal(balance_node0["total_balance"] * COIN, (100000 + 2.04 + 10) * COIN)
        assert_equal(balance_node1["total_balance"] * COIN, (25000 - 10.0) * COIN)
        # Dev fund is set to go to node 2 which is 6*0.16 = 0.96
        assert_equal(balance_node2["total_balance"] * COIN, 0.96 * COIN)

        # Send another output to `to_addr` to make sure the tracking is done properly
        tx = nodes[1].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx2 = nodes[1].getrawtransaction(tx['txid'])
        txhash2 = nodes[1].sendrawtransaction(rawtx2)
        self.wait_for_mempool(nodes[0], txhash2)
        self.stakeBlocks(1)
        tracked_balances2 = nodes[0].geteligibleaddresses(1, False)

        assert_equal(self.get_balance_of(to_addr, tracked_balances2) * COIN, (10 - tx["fee"]) * 2 * COIN )
        assert_equal(self.get_balance_of(reward_address0, tracked_balances2) * COIN, (2.04 + float(tx["fee"])) * 2 * COIN )
        assert_equal(self.get_balance_of(treas_addr, tracked_balances2) * COIN, 0.96 * 2 * COIN)

        # Collect all outputs of node0 to one address that will be constantly chosen for staking
        node0_staking_addr, amount_recv = self.accumulate_unspents_to_one_address(nodes[0], nodes[1])

        tracked_balances3 = nodes[0].geteligibleaddresses(1, False)

        # to_addr balance should have changed because we sent all the outputs to node0_staking_addr
        assert_equal(self.get_balance_of(to_addr, tracked_balances3) * COIN, 0)
        assert_equal(self.get_balance_of(reward_address0, tracked_balances3) * COIN, 0)

        assert_equal(self.get_balance_of(treas_addr, tracked_balances3) * COIN, 0.96 * 4 * COIN)
        assert_equal(self.get_balance_of(node0_staking_addr, tracked_balances3) * COIN, amount_recv * COIN)

        # Nobody is eligible at block 1
        elig = nodes[0].geteligibleaddresses(1)
        assert_equal(len(elig), 0)
        # Since nobody was elig then the carried forward has to be set
        ro = nodes[0].getblock(nodes[0].getblockhash(1), 2)
        assert_equal(float(ro['tx'][0]['vout'][0]['gvr_fund_cfwd']) * COIN, 3 * COIN)


        node2_wallet_info_before_staking = nodes[2].getwalletinfo()
        nodes[2].walletsettings('stakingoptions', {'rewardaddress': reward_address2, 'enabled': True})
        self.stakeBlocks(1, 2)

        node2_wallet_info_after_staking = nodes[2].getwalletinfo()
        block_count = nodes[0].getblockcount()
        block4_details = nodes[0].getblock(nodes[0].getblockhash(block_count - 1), 2, True)
        block4_gvr_cfwd = 0

        try:
            block4_gvr_cfwd = block4_details['tx'][0]['vout'][0]['gvr_fund_cfwd']
        except KeyError as e:
            assert_equal(str(e), "'gvr_fund_cfwd'")


        block5_details = nodes[0].getblock(nodes[0].getblockhash(block_count), 2, True)
        block5_staking_addr = nodes[0].decodescript(block5_details["stakekernelscript"])

        assert_equal(treas_addr, block5_staking_addr["addresses"][0])
        # Now make sure treas_addr was elig
        elig_addresses = nodes[0].geteligibleaddresses(block_count)
        assert_equal(self.is_elig(treas_addr, elig_addresses), True)

        # 3 = gvr from staked block 5
        # 0.96 = dev fund reward
        # 2.04 = block reward
        assert_equal(node2_wallet_info_after_staking["total_balance"] * COIN, (float(node2_wallet_info_before_staking["total_balance"]) + float(block4_gvr_cfwd) + 3 + 0.96 + 2.04) * COIN)

        tracked_balances_block5_before_reorg = nodes[0].geteligibleaddresses(block_count, False)

        # ========================================================================================

        node0_wallet_info_before_staking = nodes[0].getwalletinfo()
        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': True})

        self.stakeBlocks(1)  # Stake 6th block
        node0_wallet_info_after_staking = nodes[0].getwalletinfo()

        block_count = nodes[0].getblockcount()
        # node0 is the staker so node0_staking_addr should be eligible and receives the GVR
        block6_details = nodes[0].getblock(nodes[0].getblockhash(block_count), 2, True)
        block6_staking_addr = nodes[0].decodescript(block6_details["stakekernelscript"])

        assert_equal(node0_staking_addr, block6_staking_addr["addresses"][0])

        elig_addresses = nodes[0].geteligibleaddresses(block_count)
        assert_equal(self.is_elig(node0_staking_addr, elig_addresses), True)

        block5_details = nodes[0].getblock(nodes[0].getblockhash(block_count - 1), 2, True)
        block5_gvr_cfwd = 0

        try:
            block5_gvr_cfwd = block5_details['tx'][0]['vout'][0]['gvr_fund_cfwd']
        except KeyError as e:
            assert_equal(str(e), "'gvr_fund_cfwd'")

        assert_equal(node0_wallet_info_after_staking["total_balance"] * COIN, (float(node0_wallet_info_before_staking["total_balance"]) + float(block5_gvr_cfwd) + 3 + 2.04) * COIN)

        # When there is reorg make sure the balance is untracked

        height_before = nodes[1].getblockcount()

        best_hash = nodes[0].getbestblockhash()
        nodes[0].invalidateblock(best_hash)
        nodes[1].invalidateblock(best_hash)
        nodes[2].invalidateblock(best_hash)
        self.sync_all()

        assert_equal(nodes[1].getblockcount(), height_before - 1)
        tracked_balances_block5_after_reorg = nodes[0].geteligibleaddresses(nodes[1].getblockcount(), False)
        assert_equal(tracked_balances_block5_after_reorg, tracked_balances_block5_before_reorg)

        nodes[0].verifychain()
        self.stop_nodes()

        # Make sure that after nodes restarts the tracked balances are the same
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-gvrthreshold=10000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_all()

        block_count = nodes[0].getblockcount()
        tracked_block5_after_restart = nodes[2].geteligibleaddresses(block_count, False)
        assert_equal(tracked_block5_after_restart, tracked_balances_block5_after_reorg)

if __name__ == '__main__':
    GhostVeteranRewardTest().main()
