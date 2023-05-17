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

from test_framework.script import (
    CScript,
    OP_RETURN,
)

class GhostVeteranReward3Test(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-debug', '-anonrestricted=0', '-reservebalance=0',  '-txindex=1',] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def accumulate_unspents_to_one_address(self, node, stakingNode):
        stakingNode.walletsettings('stakingoptions', {'enabled': True})

        addr = node.getnewaddress()
        total_balance = node.getbalances()["mine"]["trusted"]
        outputs = [{'address': addr, 'amount': total_balance, 'subfee': True}]
        tx = node.sendtypeto('ghost', 'ghost', outputs, '', '', 1, 1, False, {'show_fee': True})
        rawtx = node.getrawtransaction(tx['txid'])
        txhash = node.sendrawtransaction(rawtx)
        self.wait_for_mempool(stakingNode, txhash)
        self.stakeBlocks(1)

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

        self.stakeBlocks(1)
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

        self.start_node(0, ['-wallet=default_wallet', '-reservebalance=0', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=100000000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])
        self.start_node(1, ['-wallet=default_wallet', '-reservebalance=0', '-txindex', '-debug', '-anonrestricted=0', '-gvrthreshold=100000000', '-minrewardrangespan=1', '-automatedgvrstartheight=0'])

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        assert_equal(nodes[0].getwalletinfo()["total_balance"], 100000)

        reward_address0 = nodes[0].getnewaddress()
        reward_address1 = nodes[1].getnewaddress()
        treas_addr = nodes[1].getnewaddress()

        nodes[0].walletsettings('stakingoptions', {'rewardaddress': reward_address0, 'enabled': True})
        nodes[1].walletsettings('stakingoptions', {'rewardaddress': reward_address1, 'enabled': False})
        self.connect_nodes_bi(0, 1)

        self.sync_all()

        for n in nodes:
            n.pushtreasuryfundsetting({'timefrom': 2, 'fundaddress': treas_addr, 'minstakepercent': 10, 'outputperiod': 4})
        self.stakeBlocks(1)
        self.stakeBlocks(1)

        node1_receiving_addr = nodes[1].getnewstealthaddress()
        nodes[0].sendghosttoanon(node1_receiving_addr, 1000, '', '', False, 'node0 -> node1 p->a')
        self.stakeBlocks(1)

        # Spending to DATA_OUTPUT 
        burn_script = CScript([OP_RETURN, ])
        nodes[0].sendtypeto('ghost', 'ghost', [{'address': 'script', 'amount': 10, 'script': burn_script.hex()},])
        self.stakeBlocks(1)

        res = nodes[0].verifychain(3)
        assert_equal(res, True)

if __name__ == '__main__':
    GhostVeteranReward3Test().main()
