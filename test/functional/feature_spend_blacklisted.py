#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error
)


class ControlAnonTest2(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

        self.extra_args = [['-debug', '-anonrestricted=0', '-lastanonindex=100', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1', ] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def init_nodes_with_anonoutputs(self, nodes, node1_receiving_addr, node0_receiving_addr, ring_size):
        anon_tx_txid0 = nodes[0].sendtypeto('ghost', 'anon', node1_receiving_addr, 600, '', '', False, 'node0 -> node1 p->a')
        self.wait_for_mempool(nodes[0], anon_tx_txid0)
        self.stakeBlocks(3)

        unspent_filtered_node1 = nodes[1].listunspentanon(0, 9999, [node1_receiving_addr])

        while True:
            unspent_fil_node0 = nodes[0].listunspentanon(0, 9999, [node0_receiving_addr])
            if len(unspent_fil_node0) < ring_size * len(unspent_filtered_node1):
                nodes[0].sendghosttoanon(node0_receiving_addr, 1000, '', '', False, 'node0 -> node1 p->a')
                self.stakeBlocks(4)
            else:
                break

    def restart_nodes_with_anonoutputs(self):
        nodes = self.nodes
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=100', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=100', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.connect_nodes_bi(0, 1)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 3)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=100', '-debug'])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=100', '-debug'])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        self.restart_nodes_with_anonoutputs()
        sxAddrTo0_1 = nodes[1].getnewstealthaddress()

        last_anon_index = nodes[0].anonoutput()["lastindex"]
        anon_index2 = last_anon_index - 1

        assert_greater_than(last_anon_index, 1)

        # 1 - Try to spend from blacklisted txs inside node 0 and it will be rejected
        # Restart node 0 with last_anon_index and anon_index2 blacklisted
        self.stop_node(0)
        tx_to_blacklist = str(last_anon_index) + ',' + str(anon_index2)
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=100', '-blacklistedanon='+ tx_to_blacklist])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

        lastanoni_details = nodes[0].anonoutput(output=str(last_anon_index))
        coincontrol = {'test_mempool_accept': True, 'inputs': [{'tx': lastanoni_details["txnhash"], 'n': lastanoni_details["n"]}]}
        outputs = [{'address': sxAddrTo0_1, 'amount': 5, 'subfee': True}]
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 5, 1, False, coincontrol)
        assert_equal( tx["mempool-reject-reason"], "anon-blind-tx-blacklisted")

        # 2 - Create a transaction inside node 0 which has no blacklists and submit it to node 1 which has blacklists
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-txindex', '-lastanonindex=100'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-lastanonindex=100', '-txindex', '-blacklistedanon=' + tx_to_blacklist])

        self.connect_nodes_bi(0, 1)
        self.sync_all()
        
        coincontrol = {'inputs': [{'tx': lastanoni_details["txnhash"], 'n': lastanoni_details["n"]}]}
        outputs = [{'address': sxAddrTo0_1, 'amount': 5, 'subfee': True}]
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 5, 1, False, coincontrol)
        assert_equal(self.wait_for_mempool(nodes[0], tx), True)
        self.stakeBlocks(1, 0, False)

        raw_blacklisted_tx_node0 = nodes[0].getrawtransaction(tx)

        assert_raises_rpc_error(None, "anon-blind-tx-blacklisted", nodes[1].sendrawtransaction, raw_blacklisted_tx_node0)

if __name__ == '__main__':
    ControlAnonTest2().main()
