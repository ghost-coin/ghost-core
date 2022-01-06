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
                nodes[0].sendghosttoanon(node0_receiving_addr, 1000, '', '', False, 'node0 -> node1 p->a')
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

        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-maxtxfee=1'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-lastanonindex=0', '-stakethreadconddelayms=500', '-maxtxfee=1'])

        self.connect_nodes_bi(0, 1)
        receiving_addr = nodes[1].getnewaddress()

        unspent = nodes[1].listunspentanon(0, 9999)
        firstUnspent = unspent[0]
        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]
        coincontrol = {'spend_frozen_blinded': True, 'inputs': inputs}

        outputs = [{
            'address': receiving_addr,
            'type': 'standard',
            'amount': firstUnspent["amount"],
            'subfee': True
        }]

        # Anon is restricted but there are non blacklisted tx so this should pass
        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', ring_size, 1, False, coincontrol)
        assert_equal(self.wait_for_mempool(nodes[1], tx), True)

        # Now trying to spend to the recovery address, node 0 owns the recovery address
        # Send to recovery address

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-debug', '-lastanonindex=10000', '-stakethreadconddelayms=500'])
        self.start_node(1, ['-wallet=default_wallet', '-debug', '-lastanonindex=10000', '-stakethreadconddelayms=500'])
        self.connect_nodes_bi(0, 1)

        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"
        unspent = nodes[1].listunspentanon(0, 9999)
        firstUnspent = unspent[0]
        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]

        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': firstUnspent["amount"],
            'subfee': True
        }]

        coincontrol = {'spend_frozen_blinded': True, 'test_mempool_accept': True, 'inputs': inputs}
        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal( tx["mempool-allowed"], True)

        # This will fail due to the output size being greater than anonMaxOutputSize

        tx_to_blacklist = []
        lastanonindex = nodes[0].anonoutput()['lastindex']
        tx_to_blacklist = (list)(range(1, lastanonindex + 1))

        self.restart_nodes_with_anonoutputs()
        self.stop_nodes()

        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))
        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=1000', '-debug', '-stakethreadconddelayms=500', '-blacklistedanon=' + tx_to_blacklist])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=1000', '-debug', '-stakethreadconddelayms=500', '-blacklistedanon=' + tx_to_blacklist])
        self.connect_nodes_bi(0, 1)

        self.sync_all()
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"

        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]

        amount_to_send_other = int(firstUnspent["amount"] * decimal.Decimal(0.90))
        amount_to_send_recovery = int(firstUnspent["amount"] - amount_to_send_other)

        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': amount_to_send_recovery,
            'subfee': True
        },{
        'address': receiving_addr,
        'type': 'standard',
        'amount': amount_to_send_other,
        'subfee': True
        }]

        coincontrol = {'test_mempool_accept': True, 'spend_frozen_blinded': True, 'inputs': inputs}
        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-reject-reason"], "bad-frozen-spend-toomany-outputs")

        # Sending the amount to recovery address
        tx_to_blacklist = []
        lastanonindex = nodes[0].anonoutput()['lastindex']
        tx_to_blacklist = (list)(range(1, lastanonindex + 1))
        tx_to_blacklist = ','.join(map(str, tx_to_blacklist))

        self.stop_nodes()
        self.start_node(0, ['-wallet=default_wallet', '-lastanonindex=1000', '-debug', '-stakethreadconddelayms=500', '-blacklistedanon=' + tx_to_blacklist])
        self.start_node(1, ['-wallet=default_wallet', '-lastanonindex=1000', '-debug', '-stakethreadconddelayms=500', '-blacklistedanon=' + tx_to_blacklist])
        self.connect_nodes_bi(0, 1)

        # Node 0 holds the recovery addr private key
        self.sync_all()
        recovery_addr = "pX9N6S76ZtA5BfsiJmqBbjaEgLMHpt58it"
        non_recovery_addr = nodes[1].getnewaddress()

        unspent = nodes[1].listunspentanon(0, 9999)
        firstUnspent = unspent[0]
        inputs = [{'tx': firstUnspent["txid"], 'n': firstUnspent["vout"]}]


        outputs = [{
            'address': recovery_addr,
            'type': 'standard',
            'amount': firstUnspent["amount"],
            'subfee': True
        }]

        non_recovery_outputs = [{
            'address': non_recovery_addr,
            'type': 'standard',
            'amount': firstUnspent["amount"],
            'subfee': True
        }]

        coincontrol = {'test_mempool_accept': True, 'spend_frozen_blinded': True, 'inputs': inputs}

        # First attempt to spend it to non recovery address
        tx_to_non_recov = nodes[1].sendtypeto('anon', 'part', non_recovery_outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx_to_non_recov["mempool-reject-reason"], "bad-frozen-spend-to-non-recovery")

        # Now spend to recovery address this should succeed
        outputs[0]["amount"] = firstUnspent["amount"]
        tx = nodes[1].sendtypeto('anon', 'part', outputs, 'comment', 'comment-to', 1, 1, False, coincontrol)
        assert_equal(tx["mempool-allowed"], True)

if __name__ == '__main__':
    ControlAnonTest2().main()
