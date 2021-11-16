#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_raises_rpc_error,
    assert_equal
)

class ControlAnonTest(GhostTestFramework):
    def set_test_params(self):
         # Start three nodes both of them with anon enabled
        self.setup_clean_chain = True
        self.num_nodes = 3
        # We don't pass -anonrestricted param here and let the default value to be used which is true
        self.extra_args = [['-debug', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1', ] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.sync_all()

    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        self.import_genesis_coins_b(nodes[2])

        sx0 = nodes[0].getnewstealthaddress()

        # Create a transaction with anon output
        # Note: Anon output is disabled so this should fail
    
        anon_tx_txid1 = nodes[1].sendtypeto('ghost', 'anon', [{'address': sx0, 'amount': 15}])
        print("ANON TXID 1 " + anon_tx_txid1)
        # This is False because the tx was created inside the wallet but
        # unable to add it inside the mempool
        assert_equal(self.wait_for_mempool(nodes[1], anon_tx_txid1), False)

        blind_tx_txid1 = nodes[1].sendtypeto('ghost', 'blind', [{'address': sx0, 'amount': 15}])
        print("ANON TXID 2" + blind_tx_txid1)
        assert_equal(self.wait_for_mempool(nodes[1], blind_tx_txid1), False)

        # Restart the nodes with anon tx enabled
        # Note: Anon output is enabled so this should pass both nodes should be able to sync together
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])

        self.connect_nodes_bi(0, 1) # Connect the two nodes
        self.connect_nodes_bi(1, 2) # Connect the two nodes
        self.sync_all()

        # With -anonrestricted=0 the tx will be added to the mempool
        anon_tx_txid2 = nodes[1].sendtypeto('ghost', 'anon', [{'address': sx0, 'amount': 15}])
        print("ANON TXID 2 " + anon_tx_txid2)
        assert_equal(self.wait_for_mempool(nodes[1], anon_tx_txid2), True)

        blind_tx_txid2 = nodes[1].sendtypeto('ghost', 'blind', [{'address': sx0, 'amount': 15}])
        print("BLIND TXID 2" + blind_tx_txid2)
        assert_equal(self.wait_for_mempool(nodes[1], blind_tx_txid2), True)
        self.sync_all()

        # Restart the nodes: node 0 with anon enabled and node 1 with anon disabled
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(1, 2)

        anon_tx_txid3 = nodes[0].sendtypeto('ghost', 'anon', [{'address': sx0, 'amount': 15}])
        assert anon_tx_txid3 != ""
        blind_tx_txid3 = nodes[0].sendtypeto('ghost', 'blind', [{'address': sx0, 'amount': 15}])
        assert blind_tx_txid3 != ""

        rawtx_anon_txid3 = nodes[0].getrawtransaction(anon_tx_txid3)
        assert_raises_rpc_error(None, "anon-blind-tx-invalid", nodes[1].sendrawtransaction, rawtx_anon_txid3)

        rawtx_blind_txid3 = nodes[0].getrawtransaction(blind_tx_txid3)
        assert_raises_rpc_error(None, "anon-blind-tx-invalid", nodes[1].sendrawtransaction, rawtx_blind_txid3)
        # Now try to sync node 0 and node 1 it will fail
        try:
            self.sync_all()
        except Exception as e:
            assert "Mempool sync timed out" in str(e)

        
        # Attempt to spend normal outputs and it should succeed
        # Note: standard == part == ghost
        standard_tx_txid = nodes[0].sendtypeto('standard', 'standard', [{'address': sx0, 'amount': 15}])
        assert_equal(self.wait_for_mempool(nodes[0], standard_tx_txid), True)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])
        # This is just so that node 0 can stake
        self.start_node(2, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1'])

        self.connect_nodes_bi(0, 2)
        assert_equal(nodes[0].getblockcount(), 0)
        assert_equal(nodes[1].getblockcount(), 0)
        assert_equal(nodes[2].getblockcount(), 0)

        anon_tx_txid4 = nodes[0].sendtypeto('ghost', 'anon', [{'address': sx0, 'amount': 15}])
        assert anon_tx_txid4 != ""
        self.stakeToHeight(1, nStakeNode=0, nSyncCheckNode=False, fSync=False)

        # Attempt to submit block with invalid transactions
        node0Block1 = nodes[0].getblock(nodes[0].getblockhash(1))
        node0Block1Hex = nodes[0].getblock(nodes[0].getblockhash(1), 0)

        assert anon_tx_txid4 in node0Block1['tx']

        assert_equal(nodes[0].getblockcount(), 1)
        assert_equal(nodes[1].getblockcount(), 0)
        assert_equal(nodes[2].getblockcount(), 1)

        res = nodes[1].submitblock(node0Block1Hex)
        assert_equal("anon-blind-tx-invalid", res)

if __name__ == '__main__':
    ControlAnonTest().main()
