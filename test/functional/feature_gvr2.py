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

class GhostVeteranReward2Test(GhostTestFramework):
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

        # Start nodes with threshold set to 1000000000000
        # With rangespan sets to 5 => Means an address has to hold a balance for at least 5 blocks

        self.start_node(0, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])
        self.start_node(1, ['-wallet=default_wallet', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])
        self.start_node(2, ['-wallet=default_wallet', '-reservebalance=1025', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')

        assert_equal(nodes[0].getwalletinfo()["total_balance"], 100000)
        assert_equal(nodes[1].getwalletinfo()["total_balance"], 25000)
        assert_equal(nodes[2].getwalletinfo()["total_balance"], 0)

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
            n.pushtreasuryfundsetting({'timefrom': 0, 'fundaddress': treas_addr, 'minstakepercent': 10, 'outputperiod': 10})

        node2_to_addr = treas_addr
        amount = 10_002

        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': True})

        outputs = [{'address': node2_to_addr, 'amount': amount, 'subfee': True}]
        tx = nodes[0].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = nodes[0].getrawtransaction(tx['txid'])
        txhash = nodes[0].sendrawtransaction(rawtx)

        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        # block_details = nodes[0].getblock(nodes[0].getblockhash(1), 2)
        tracked_balances = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses = nodes[0].geteligibleaddresses(block_count)

        # Only `node2_to_addr with the staking addr of block 1 will appear here, because
        # Ranges with multiplier equals to 0 are removed.
        tracked_balance_node2_to_addr = (amount + 0.96 - float(tx["fee"])) * COIN
        assert_equal(self.get_balance_of(node2_to_addr, tracked_balances) * COIN, tracked_balance_node2_to_addr)
        assert_equal(len(eligible_addresses), 0)

        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        tracked_balances_block2 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block2 = nodes[0].geteligibleaddresses(block_count)
        assert_equal(len(eligible_addresses_block2), 0)


        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        tracked_balances_block3 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block3 = nodes[0].geteligibleaddresses(block_count)
        assert_equal(len(eligible_addresses_block3), 0)


        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        tracked_balances_block4 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block4 = nodes[0].geteligibleaddresses(block_count)
        assert_equal(len(eligible_addresses_block4), 0)

        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        tracked_balances_block5 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block5 = nodes[0].geteligibleaddresses(block_count)
        assert_equal(len(eligible_addresses_block5), 0)


        # At block 6 node2_to_addr should be eligible until its balance drops down
        self.stakeBlocks(1)
        block_count = nodes[0].getblockcount()
        tracked_balances_block6 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block6 = nodes[0].geteligibleaddresses(block_count)
        assert_equal(len(eligible_addresses_block6), 1)

        block_details = nodes[0].getblock(nodes[0].getblockhash(block_count), 2)
        assert_equal(float(block_details['tx'][0]['vout'][0]['gvr_fund_cfwd']) * COIN, 3 * 6 * COIN)

        tracked_balance_node2_to_addr = tracked_balance_node2_to_addr + (5 * 0.96) * COIN
        assert_equal(self.get_balance_of(node2_to_addr, eligible_addresses_block6) * COIN, tracked_balance_node2_to_addr)

        # Since node2_to_addr has kept its balance till 6 blocks and he is eligible then he will receives
        # The GVR after staking

        nodes[2].walletsettings('stakingoptions', {'rewardaddress': reward_address2, 'enabled': True})
        self.stakeBlocks(1, 2)

        block_count = nodes[0].getblockcount()
        tracked_balances_block7 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block7 = nodes[0].geteligibleaddresses(block_count)

        block_details = nodes[0].getblock(nodes[0].getblockhash(block_count), 2, True)
        block7_staking_addr = nodes[0].decodescript(block_details["stakekernelscript"])

        assert_equal(block7_staking_addr["addresses"][0], node2_to_addr)

        # 3 * 7 == GVR we do time 7 because no one is eligible since block 1 and the carried forward is set
        # 0.96 == dev fund
        tracked_balance_node2_to_addr = tracked_balance_node2_to_addr + (3 * 7 + 0.96) * COIN
        assert_equal(self.get_balance_of(node2_to_addr, eligible_addresses_block7) * COIN, tracked_balance_node2_to_addr)


        # Restart the nodes and check the tracked balances
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-reservebalance=10000000', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])
        self.start_node(1, ['-wallet=default_wallet', '-reservebalance=10000000', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])
        self.start_node(2, ['-wallet=default_wallet', '-reservebalance=10000000', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=1000000000000', '-minrewardrangespan=5', '-automatedgvrstartheight=0'])

        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': False})
        nodes[1].walletsettings('stakingoptions', {'rewardaddress': reward_address1, 'enabled': False})
        nodes[2].walletsettings('stakingoptions', {'rewardaddress': reward_address2, 'enabled': False})

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)
        self.sync_all()

        for n in nodes:
            n.pushtreasuryfundsetting({'timefrom': 0, 'fundaddress': treas_addr, 'minstakepercent': 10, 'outputperiod': 1})

        block_count = nodes[0].getblockcount()
        tracked_balances_block7_after_restart = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block7_after_restart = nodes[0].geteligibleaddresses(block_count)

        assert_equal(tracked_balances_block7_after_restart, tracked_balances_block7)
        assert_equal(eligible_addresses_block7_after_restart, eligible_addresses_block7)

        # Now node2_addr will spend and its balance will go below 10k

        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': True})

        another_node2_addr = nodes[2].getnewaddress()

        amount = 1000
        outputs = [{'address': another_node2_addr, 'amount': amount, 'subfee': True}]
        print(nodes[2].getstakinginfo())
        print(nodes[2].getbalances())
        tx = nodes[2].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True, 'changeaddress': node2_to_addr})
        rawtx = nodes[2].getrawtransaction(tx['txid'])
        txhash = nodes[2].sendrawtransaction(rawtx)
        self.wait_for_mempool(nodes[0], txhash)
        self.stakeBlocks(1)

        block_details = nodes[2].getblock(nodes[2].getblockhash(8), 2, True)
        vin_txid = block_details['tx'][1]['vin'][0]['txid']
        vin_tx_details = nodes[2].gettransaction(vin_txid, True, True)
        tracked_balances_block8 = nodes[0].geteligibleaddresses(8, False)
        eligible_addresses_block8 = nodes[0].geteligibleaddresses(8)

        change_amount = 0

        if block_details['tx'][1]['vout'][0]['scriptPubKey']['addresses'][0] == node2_to_addr:
            change_amount = float(block_details['tx'][1]['vout'][0]['value']) * COIN
        else:
            change_amount = float(block_details['tx'][1]['vout'][1]['value']) * COIN

        input_amount = float(vin_tx_details["amount"]) * COIN

        # gvr_fund_cfwd should be set since the staker is not eligible
        assert_equal(float(block_details['tx'][0]['vout'][0]['gvr_fund_cfwd']) * COIN, 3 * COIN)

        expected_balance = tracked_balance_node2_to_addr + 96000000 + change_amount
        print("INPUT AMOUNT=", input_amount)
        print("TRACKED BALANCE=", tracked_balance_node2_to_addr)
        print("CHANGE AMOUNT=", change_amount)
        expected_balance = expected_balance - input_amount

        assert_equal(self.get_balance_of(node2_to_addr, tracked_balances_block8) * COIN, expected_balance)
        assert_equal(len(eligible_addresses_block8), 0)

        # After the balance goes below 10k we will make the amount reach 10k again and
        # It has to wait 5 blocks again before becoming elig

        # Send some ghosts to tracked_balances_block8 so that it comes elig again

        amount = 1020
        outputs = [{'address': node2_to_addr, 'amount': amount, 'subfee': True}]
        tx = nodes[0].sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = nodes[0].getrawtransaction(tx['txid'])
        txhash = nodes[0].sendrawtransaction(rawtx)
        self.wait_for_mempool(nodes[0], txhash)
        self.stakeBlocks(1)

        block_count = nodes[0].getblockcount()
        tracked_balances_block9 = nodes[0].geteligibleaddresses(block_count, False)
        eligible_addresses_block9 = nodes[0].geteligibleaddresses(block_count)

        amount_fee_excluded = (amount - float(tx['fee'])) + 0.96
        balance_expected_block9 = expected_balance + (amount_fee_excluded*COIN)
        assert_equal(self.get_balance_of(node2_to_addr, tracked_balances_block9) * COIN, balance_expected_block9)

        assert_equal(len(eligible_addresses_block9), 0)

        # Stake 6 blocks again before node2_to_addr becomes eligible again
        self.stakeBlocks(4)
        eligible_addresses_block13 = nodes[0].geteligibleaddresses(12)
        assert_equal(len(eligible_addresses_block13), 0)

        self.stakeBlocks(3)

        eligible_addresses_block16 = nodes[0].geteligibleaddresses(16)
        assert_equal(len(eligible_addresses_block16), 1)
        assert_equal(self.get_balance_of(node2_to_addr, eligible_addresses_block16) * COIN, (0.96*7*COIN) + balance_expected_block9)


if __name__ == '__main__':
    GhostVeteranReward2Test().main()
