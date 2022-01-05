#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework

from test_framework.messages import (COIN)
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)


class ControlAnonTest2(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1' ] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def init_nodes_with_anonoutputs(self, nodes, node1_receiving_addr, node0_receiving_addr, ring_size):
        anon_tx_txid0 = nodes[0].sendtypeto('ghost', 'anon', node1_receiving_addr, 2000, '', '', False, 'node0 -> node1 p->a')
        self.wait_for_mempool(nodes[0], anon_tx_txid0)
        self.stakeBlocks(3)

        unspent_filtered_node1 = nodes[1].listunspentanon(0, 9999, [node1_receiving_addr])

        while True:
            unspent_fil_node0 = nodes[0].listunspentanon(0, 9999, [node0_receiving_addr])
            if len(unspent_fil_node0) < ring_size * len(unspent_filtered_node1):
                nodes[0].sendghosttoanon(node0_receiving_addr, 1500, '', '', False, 'node0 -> node1 p->a')
                self.stakeBlocks(4)
            else:
                break

    def get_anoninput_greater_than(self, unspents, amount):
        for unspent in unspents:
            if unspent['amount'] > 50:
                return unspent


    def restart_nodes_with_anonoutputs(self):
        nodes = self.nodes
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet',  '-debug', '-anonrestricted=0'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0'])
        self.connect_nodes_bi(0, 1)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 1)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug'])
        self.start_node(1, ['-wallet=default_wallet', '-debug'])
        self.connect_nodes_bi(0, 1)
        self.sync_all()
    
    def get_whole_amount_from_unspent(self, node, tx_hash): 
        uns = node.listunspentanon(0, 9999)
        a = 0
        for s in uns:
            if s["txid"] == tx_hash:
                a = s["amount"]
                break
        return a


    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        self.restart_nodes_with_anonoutputs()
        sxAddrTo0_1 = nodes[1].getnewaddress()

        last_anon_index = nodes[0].anonoutput()["lastindex"]
        anon_index2 = last_anon_index - 1
        anon_index3 = anon_index2 - 1
        anon_index4 = anon_index3 - 1
        tx_to_blacklist = []

        for i in range(1, last_anon_index):
            tx_to_blacklist.append(i)
        tx_to_blacklist.append(last_anon_index)
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))

        # 1 - Try to spend from blacklisted txs inside node 0 and it will be rejected
        # Restart node 0 with last_anon_index, anon_index2 blacklisted
        self.stop_node(0)

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-blacklistedanon='+ tx_to_blacklist])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

        unspent = nodes[0].listunspentanon(0, 9999)
        firstUnspent = unspent[0]
        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]
        coincontrol = {'spend_frozen_blinded': True, 'test_mempool_accept': True, 'inputs': inputs}
        outputs = [{'address': sxAddrTo0_1, 'amount': firstUnspent["amount"], 'subfee': True}]
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-spend-to-non-recovery")

        # 2 - Create a transaction inside node 0 which has no blacklists and submit it to node 1 which has blacklists
        # It will fail because we are not sending the 100% of the amount to the recovery addr
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-txindex'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-txindex', '-blacklistedanon=' + tx_to_blacklist])

        self.connect_nodes_bi(0, 1)
        self.sync_all() 
        
        unspents = nodes[0].listunspentanon(1, 9999)
        unspent2 = unspents[0]
        inputs = [{'tx': unspent2["txid"], 'n': unspent2["vout"]}]

        coincontrol = {'spend_frozen_blinded': True, 'inputs': inputs}
        outputs = [{'address': nodes[1].getnewaddress(), 'amount': unspent2["amount"], 'subfee': True}]
        tx2 = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        self.stakeBlocks(1, 0, False)

        hex_blacklisted_tx_node0 = nodes[0].getrawtransaction(tx2)
        assert_raises_rpc_error(None, "bad-frozen-spend-to-non-recovery", nodes[1].sendrawtransaction, hex_blacklisted_tx_node0)

        # Attempt now to spend blacklisted tx to recovery address and should succeed
        # node 0 holds the recovery address priv/pub key
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-rescan', '-debug', '-anonrestricted=0',  '-txindex', '-lastanonindex=100'])
        self.start_node(1, ['-wallet=default_wallet', '-rescan', '-debug', '-lastanonindex=100', '-txindex', '-blacklistedanon=' + tx_to_blacklist])
        self.connect_nodes_bi(0,1)

        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"
        unspents = nodes[0].listunspentanon(0, 9999)
        unspent2 = unspents[0]
        inputs = [{'tx': unspent2["txid"], 'n': unspent2["vout"]}]
        coincontrol = {'spend_frozen_blinded': True, 'inputs': inputs}
        outputs = [{'address': recovery_addr, 'amount': unspent2["amount"], 'subfee': True}]
        
        tx = nodes[0].sendtypeto('anon', 'ghost', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        self.stakeBlocks(1, 0, False)
        raw_blacklisted_tx_succeed = nodes[0].getrawtransaction(tx)
        accepted_tx = nodes[1].sendrawtransaction(raw_blacklisted_tx_succeed)
        assert accepted_tx != ""

if __name__ == '__main__':
    ControlAnonTest2().main()

