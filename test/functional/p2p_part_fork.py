#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time

from test_framework.test_particl import ParticlTestFramework
from test_framework.authproxy import JSONRPCException


class ForkTest(ParticlTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.extra_args = [ ['-debug',] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.connect_nodes_bi(1, 2)

        self.connect_nodes_bi(3, 4)
        self.connect_nodes_bi(3, 5)
        self.connect_nodes_bi(4, 5)

        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        # Disable staking
        nodes[0].reservebalance(True, 10000000)
        nodes[3].reservebalance(True, 10000000)

        self.import_genesis_coins_b(nodes[0])
        self.import_genesis_coins_a(nodes[3])
        nodes[1].extkeyimportmaster(nodes[1].mnemonic('new')['master'])
        sxaddr1 = nodes[1].getnewstealthaddress()
        p2wpkh1 = nodes[1].getnewaddress('segwit p2wpkh', False, False, False, 'bech32')

        n0_wi_before = nodes[0].getwalletinfo()

        # Start staking
        nBlocksShorterChain = 2
        nBlocksLongerChain = 5

        nodes[3].walletsettings('stakelimit', {'height': nBlocksLongerChain})
        nodes[3].reservebalance(False)

        self.stakeBlocks(1, fSync=False)

        txids = []
        txids.append(nodes[0].sendtypeto('part', 'part', [{'address': sxaddr1, 'amount': 1}, ]))
        txids.append(nodes[0].sendtypeto('part', 'blind', [{'address': sxaddr1, 'amount': 1.1}, ]))
        txids.append(nodes[0].sendtypeto('part', 'anon', [{'address': sxaddr1, 'amount': 1.2}, ]))
        txids.append(nodes[0].sendtypeto('part', 'part', [{'address': p2wpkh1, 'amount': 1.3}, ]))

        # Ensure txns are on node1
        for txid in txids:
            tx = nodes[0].gettransaction(txid)
            nodes[1].sendrawtransaction(tx['hex'])

        self.stakeBlocks(1, fSync=False)

        # Stop group1 from staking
        nodes[0].reservebalance(True, 10000000)

        self.wait_for_height(nodes[3], nBlocksLongerChain, 2000)

        # Stop group2 from staking
        nodes[3].reservebalance(True, 10000000)

        node0_chain = []
        for k in range(1, nBlocksLongerChain+1):
            try:
                ro = nodes[0].getblockhash(k)
            except JSONRPCException as e:
                assert('Block height out of range' in e.error['message'])
                ro = ''
            node0_chain.append(ro)
            print('node0 ', k, ' - ', ro)

        node3_chain = []
        for k in range(1, 6):
            ro = nodes[3].getblockhash(k)
            node3_chain.append(ro)
            print('node3 ', k, ' - ', ro)


        # Connect groups
        self.connect_nodes_bi(0, 3)

        fPass = False
        for i in range(15):
            time.sleep(2)

            fPass = True
            for k in range(1, nBlocksLongerChain + 1):
                try:
                    ro = nodes[0].getblockhash(k)
                except JSONRPCException as e:
                    assert('Block height out of range' in e.error['message'])
                    ro = ''
                if not ro == node3_chain[k]:
                    fPass = False
                    break
            if fPass:
                break
        #assert(fPass)


        node0_chain = []
        for k in range(1, nBlocksLongerChain + 1):
            try:
                ro = nodes[0].getblockhash(k)
            except JSONRPCException as e:
                assert('Block height out of range' in e.error['message'])
                ro = ''
            node0_chain.append(ro)
            print('node0 ', k, ' - ', ro)


        ro = nodes[0].getblockchaininfo()
        assert(ro['blocks'] == 5)
        ro = nodes[3].getblockchaininfo()
        assert(ro['blocks'] == 5)

        # Ensure all valid txns are trusted
        # resendwallettransactions() has a delay
        for txid in txids:
            try:
                tx = nodes[0].gettransaction(txid)
                nodes[0].sendrawtransaction(tx['hex'])
            except Exception:
                pass

        self.stakeBlocks(1, fSync=False)

        n0_ft = nodes[0].filtertransactions()
        num_orphaned = 0
        for tx in n0_ft:
            if tx['category'] == 'orphaned_stake':
                num_orphaned += 1
        assert(num_orphaned == 2)

        n0_lt = nodes[0].listtransactions()
        num_orphaned = 0
        for tx in n0_ft:
            if tx['category'] == 'orphaned_stake':
                num_orphaned += 1
        assert(num_orphaned == 2)

        n0_wi_after = nodes[0].getwalletinfo()
        # Some small amounts were spent.
        difference = float(n0_wi_before['total_balance']) - float(n0_wi_after['total_balance'])
        assert(difference > 0.0 and difference < 5.0)


if __name__ == '__main__':
    ForkTest().main()
