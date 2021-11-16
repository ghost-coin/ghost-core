#!/usr/bin/env python3
# Copyright (c) 2021 Ghost Core Team
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import decimal

from test_framework.test_particl import GhostTestFramework

from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


class ControlAnonTest2(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

        self.extra_args = [['-debug', '-anonrestricted=0', '-lastanonindex=0', '-reservebalance=10000000', '-stakethreadconddelayms=500', '-txindex=1', '-maxtxfee=1', ] for i in range(self.num_nodes)]

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
                nodes[0].sendparttoanon(node0_receiving_addr, 1000, '', '', False, 'node0 -> node1 p->a')
                self.stakeBlocks(4)
            else:
                break

    def restart_nodes_with_anonoutputs(self):
        nodes = self.nodes
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-anonrestricted=0', '-lastanonindex=0'])
        self.connect_nodes_bi(0, 1)
        node1_receiving_addr = nodes[1].getnewstealthaddress()
        node0_receiving_addr = nodes[0].getnewstealthaddress()
        self.init_nodes_with_anonoutputs(nodes, node1_receiving_addr, node0_receiving_addr, 3)
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug'])
        self.start_node(1, ['-wallet=default_wallet', '-debug'])
        self.connect_nodes_bi(0, 1)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes
        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])
        ring_size = 3

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])

        self.connect_nodes_bi(0, 1)
        receiving_addr = nodes[1].getnewaddress()

        anon_balance = nodes[1].getwalletinfo()["anon_balance"]

        outputs = [{
            'address': receiving_addr,
            'type': 'standard',
            'amount': anon_balance,
            'subfee': True
        }]

        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', ring_size, 1, False)
        assert_equal(self.wait_for_mempool(nodes[1], tx), False)

        # Now trying to spend to the recovery address, node 0 owns the recovery address
        # Send to recovery address

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.connect_nodes_bi(0, 1)

        nodes[0].importprivkey("7shnesmjFcQZoxXCsNV55v7hrbQMtBfMNscuBkYrLa1mcJNPbXhU")
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"

        anon_balance = nodes[1].getwalletinfo()["anon_balance"]

        assert_greater_than(anon_balance, 0)

        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': anon_balance,
            'subfee': True
        }]

        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', ring_size, 1, False)
        assert_equal(self.wait_for_mempool(nodes[1], tx), True)

        # Sending less than 99.5% to recovery address and random amount to other address
        # This will fail

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=0', '-debug', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=0', '-debug', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.connect_nodes_bi(0, 1)


        nodes[0].importprivkey("7shnesmjFcQZoxXCsNV55v7hrbQMtBfMNscuBkYrLa1mcJNPbXhU")
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"

        anon_balance = nodes[1].getwalletinfo()["anon_balance"]

        assert_greater_than(anon_balance, 0)

        amount_to_send_recovery_addr = int(anon_balance * decimal.Decimal(0.90))
        remaining = int(anon_balance - amount_to_send_recovery_addr)

        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': amount_to_send_recovery_addr,
            'subfee': True
        },{
        'address': receiving_addr,
        'type': 'standard',
        'amount': remaining,
        'subfee': True
        }]

        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', ring_size, 1, False)
        assert_equal(self.wait_for_mempool(nodes[1], tx), False)

        # Sending 99.5% to recovery address and 0.5% to any other address
        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=0', '-debug', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=0', '-debug', '-stakethreadconddelayms=500', '-rescan', '-maxtxfee=1'])
        self.connect_nodes_bi(0, 1)

        nodes[0].importprivkey("7shnesmjFcQZoxXCsNV55v7hrbQMtBfMNscuBkYrLa1mcJNPbXhU")
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"

        anon_balance = nodes[1].getwalletinfo()["anon_balance"]

        assert_greater_than(anon_balance, 0)

        amount_to_send_recovery_addr = int(anon_balance * decimal.Decimal(0.95))
        # Add another 5% from remaining amount in order to cover fees
        amount_to_send_recovery_addr += int(remaining * decimal.Decimal(0.5))

        remaining = int(anon_balance - amount_to_send_recovery_addr)

        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': amount_to_send_recovery_addr,
            'subfee': True
        }, {
            'address': receiving_addr,
            'type': 'standard',
            'amount': remaining, # 0.5% of 600
            'subfee': True
        }]

        self.stakeBlocks(4)

        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', ring_size, 1, False)
        assert_equal(self.wait_for_mempool(nodes[1], tx), True)
        
if __name__ == '__main__':
    ControlAnonTest2().main()
