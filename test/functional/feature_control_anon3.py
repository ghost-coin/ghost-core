#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)


class ControlAnonTest3(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [['-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1', ] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(2, 3)
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
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0'])
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-anonrestricted=0'])
        self.start_node(3, ['-wallet=default_wallet', '-debug', '-anonrestricted=0'])

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(2, 3)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 3)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug'])
        self.start_node(1, ['-wallet=default_wallet', '-debug'])
        self.start_node(2, ['-wallet=default_wallet', '-debug'])
        self.start_node(3, ['-wallet=default_wallet', '-debug'])

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(2, 3)
        self.sync_all()

    def run_test(self):
        # The test in this file basically spend an anon ouput inside node 1
        # And then attempt to send that tx and the block it's included in
        # to node 0 which is has anon restriction enabled
        # Previously we were expecting a "bad-anonin-extract-i" but since now all our
        # index set is valid a part from the one blacklisted, we don't expect that anymore to happen
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-stakethreadconddelayms=500', '-txindex=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-stakethreadconddelayms=500', '-txindex=1'])

        self.start_node(2, ['-wallet=default_wallet', '-debug', '-stakethreadconddelayms=500', '-txindex=1'])
        self.start_node(3, ['-wallet=default_wallet', '-debug', '-stakethreadconddelayms=500', '-txindex=1'])

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(2, 3)
        self.sync_all()
        receiving_addr = nodes[1].getnewaddress()

        receiving_addr_node1 = nodes[1].getnewstealthaddress()
        anon_tx_txid0 = nodes[0].sendtypeto('ghost', 'anon', receiving_addr_node1, 600, '', '', False, 'node0 -> node1 p->a')
        self.wait_for_mempool(nodes[0], anon_tx_txid0)
        self.stakeBlocks(2, 1, False)
        self.stakeBlocks(2, 0, False)
        self.sync_all([nodes[1], nodes[0]])

        unspent = nodes[1].listunspentanon(0, 9999, [receiving_addr_node1])

        firstUnspent = unspent[0]
        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]
        # Sending a transaction from anon to ghost being inside node 1 will succeed
        coincontrol = {'spend_frozen_blinded': True, 'inputs': inputs}
        bad_anon_tx_txid = nodes[1].sendtypeto('anon', 'ghost', [{'address': receiving_addr, 'amount': firstUnspent['amount'], 'subfee': True}], '', '', 1, 1, False, coincontrol)
        self.stakeBlocks(1, 1, False)
        self.sync_mempools()

        lastanonindex = nodes[0].anonoutput()['lastindex']
        tx_to_blacklist = (list)(range(1, lastanonindex + 1))
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))
        self.stop_node(2)
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-stakethreadconddelayms=500', '-txindex=1',
                            '-blacklistedanon=' + tx_to_blacklist])

        rawtx_block = nodes[1].getblock(nodes[1].getbestblockhash())
        assert bad_anon_tx_txid in rawtx_block['tx']

        height_node1 = nodes[1].getblockcount()

        reached = False
        for i in range(1, height_node1 + 1):
            block_to_submit_hash = nodes[1].getblockhash(i)
            block_to_submit = nodes[1].getblock(block_to_submit_hash, 0)
            res = nodes[2].submitblock(block_to_submit)

            if i == height_node1:
                assert_equal(res, "bad-frozen-spend-to-non-recovery")
                reached = True
            else:
                assert_equal(res, None)

        assert reached

if __name__ == '__main__':
    ControlAnonTest3().main()
